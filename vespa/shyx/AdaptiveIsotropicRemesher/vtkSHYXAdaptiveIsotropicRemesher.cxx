#include "vtkSHYXAdaptiveIsotropicRemesher.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkBoundingBox.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractSelection.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>

#include <CGAL/Polygon_mesh_processing/Adaptive_sizing_field.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Polygon_mesh_processing/smooth_shape.h>
#include <CGAL/Kernel/global_functions.h>

#include <boost/property_map/property_map.hpp>

#include <exception>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

vtkStandardNewMacro(vtkSHYXAdaptiveIsotropicRemesher);

namespace pmp = CGAL::Polygon_mesh_processing;

namespace
{
void CollectCellsFromExtracted(vtkPolyData* mesh, vtkDataSet* extracted, std::set<vtkIdType>& selected)
{
  if (!mesh || !extracted)
  {
    return;
  }
  const vtkIdType nMeshCells = mesh->GetNumberOfCells();

  vtkDataArray* ocid = extracted->GetCellData()->GetArray("vtkOriginalCellIds");
  if (auto* cellIds = vtkIdTypeArray::SafeDownCast(ocid))
  {
    if (cellIds->GetNumberOfTuples() == extracted->GetNumberOfCells())
    {
      for (vtkIdType i = 0; i < cellIds->GetNumberOfTuples(); ++i)
      {
        const vtkIdType cid = cellIds->GetValue(i);
        if (cid >= 0 && cid < nMeshCells)
        {
          selected.insert(cid);
        }
      }
    }
  }
  if (!selected.empty())
  {
    return;
  }

  vtkDataArray* opid = extracted->GetPointData()->GetArray("vtkOriginalPointIds");
  auto* ptIds = vtkIdTypeArray::SafeDownCast(opid);
  if (!ptIds || ptIds->GetNumberOfTuples() == 0)
  {
    return;
  }
  std::set<vtkIdType> selPt;
  for (vtkIdType i = 0; i < ptIds->GetNumberOfTuples(); ++i)
  {
    const vtkIdType pid = ptIds->GetValue(i);
    if (pid >= 0 && pid < mesh->GetNumberOfPoints())
    {
      selPt.insert(pid);
    }
  }
  if (selPt.empty())
  {
    return;
  }
  vtkIdType npts;
  const vtkIdType* pids;
  for (vtkIdType cid = 0; cid < nMeshCells; ++cid)
  {
    mesh->GetCellPoints(cid, npts, pids);
    for (vtkIdType k = 0; k < npts; ++k)
    {
      if (selPt.count(pids[k]) != 0u)
      {
        selected.insert(cid);
        break;
      }
    }
  }
}

/**
 * After detect_sharp_edges, clear feature bits on interior edges selected by signed
 * dihedral (CGAL approximate_dihedral_angle for triangles (p,q,r) and (p,q,s)).
 * mode 0: no-op; 1: clear concave (signed &lt; 0); 2: clear convex (signed &gt; 0).
 * Boundary edges are left as marked by detect_sharp_edges.
 */
template <typename EdgeBoolMap>
void ApplySharpFeatureSideFilter(CGAL_Surface& sm, EdgeBoolMap featureEdges, int mode)
{
  if (mode != 1 && mode != 2)
  {
    return;
  }
  for (CGAL_Surface::Edge_index e : sm.edges())
  {
    if (!boost::get(featureEdges, e))
    {
      continue;
    }
    const CGAL_Surface::Halfedge_index h0 = sm.halfedge(e);
    const CGAL_Surface::Halfedge_index ho = sm.opposite(h0);
    if (sm.is_border(h0) || sm.is_border(ho))
    {
      continue;
    }
    const auto p = sm.point(sm.source(h0));
    const auto q = sm.point(sm.target(h0));
    const auto r = sm.point(sm.target(sm.next(h0)));
    const auto s = sm.point(sm.target(sm.next(ho)));
    const double signedDeg = CGAL::approximate_dihedral_angle(p, q, r, s);
    const bool dropConcave = (mode == 1 && signedDeg < 0.0);
    const bool dropConvex   = (mode == 2 && signedDeg > 0.0);
    if (dropConcave || dropConvex)
    {
      boost::put(featureEdges, e, false);
    }
  }
}

template <typename EdgeBoolMap>
void FillFeaturePolyDataFromSharpEdges(const CGAL_Surface& sm, const EdgeBoolMap& featureEdges, vtkPolyData* out)
{
  out->Initialize();
  vtkNew<vtkPoints> pts;
  std::unordered_map<std::size_t, vtkIdType> vidToPid;
  const std::size_t nv = static_cast<std::size_t>(sm.number_of_vertices());
  if (nv > 0)
  {
    vidToPid.reserve(nv);
  }

  const auto ensurePoint = [&](CGAL_Surface::Vertex_index v) -> vtkIdType {
    const std::size_t k = static_cast<std::size_t>(v.idx());
    const auto it = vidToPid.find(k);
    if (it != vidToPid.end())
    {
      return it->second;
    }
    const auto& p = sm.point(v);
    const vtkIdType id = pts->InsertNextPoint(p.x(), p.y(), p.z());
    vidToPid.emplace(k, id);
    return id;
  };

  vtkNew<vtkCellArray> lines;
  vtkNew<vtkCellArray> verts;
  std::unordered_set<std::size_t> vertexEmitted;
  vertexEmitted.reserve(nv);

  for (CGAL_Surface::Edge_index e : sm.edges())
  {
    if (!boost::get(featureEdges, e))
    {
      continue;
    }
    const CGAL_Surface::Halfedge_index h0 = sm.halfedge(e);
    const CGAL_Surface::Vertex_index v0 = sm.source(h0);
    const CGAL_Surface::Vertex_index v1 = sm.target(h0);
    const vtkIdType p0 = ensurePoint(v0);
    const vtkIdType p1 = ensurePoint(v1);
    vtkIdType c[2] = { p0, p1 };
    lines->InsertNextCell(2, c);

    const std::size_t k0 = static_cast<std::size_t>(v0.idx());
    const std::size_t k1 = static_cast<std::size_t>(v1.idx());
    if (vertexEmitted.insert(k0).second)
    {
      verts->InsertNextCell(1, &p0);
    }
    if (vertexEmitted.insert(k1).second)
    {
      verts->InsertNextCell(1, &p1);
    }
  }

  out->SetPoints(pts);
  out->SetLines(lines);
  out->SetVerts(verts);
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::~vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetSelectionCellArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "MinEdgeLength: " << this->MinEdgeLength << std::endl;
  os << indent << "MaxEdgeLength: " << this->MaxEdgeLength << std::endl;
  os << indent << "AdaptiveTolerance: " << this->AdaptiveTolerance << std::endl;
  os << indent << "ProtectAngle: " << this->ProtectAngle << std::endl;
  os << indent << "NumberOfIterations: " << this->NumberOfIterations << std::endl;
  os << indent << "NumberOfRelaxationSteps: " << this->NumberOfRelaxationSteps << std::endl;
  os << indent << "ShapeSmoothingIterations: " << this->ShapeSmoothingIterations << std::endl;
  os << indent << "ShapeSmoothingTimeStep: " << this->ShapeSmoothingTimeStep << std::endl;
  os << indent << "SharpFeatureSideFilter: " << this->SharpFeatureSideFilter << std::endl;
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  if (port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input          = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output         = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outputFeatures = vtkPolyData::GetData(outputVector, 1);

  if (!input || !output || !outputFeatures)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }

  if (this->AdaptiveTolerance <= 0.0)
  {
    vtkErrorMacro("AdaptiveTolerance must be positive, got " << this->AdaptiveTolerance);
    return 0;
  }
  if (this->NumberOfIterations < 1)
  {
    vtkErrorMacro("NumberOfIterations must be >= 1.");
    return 0;
  }
  if (this->NumberOfRelaxationSteps < 0)
  {
    vtkErrorMacro("NumberOfRelaxationSteps must be >= 0, got " << this->NumberOfRelaxationSteps);
    return 0;
  }
  if (this->ShapeSmoothingIterations < 0)
  {
    vtkErrorMacro("ShapeSmoothingIterations must be >= 0, got " << this->ShapeSmoothingIterations);
    return 0;
  }
  if (this->ShapeSmoothingIterations > 0 && this->ShapeSmoothingTimeStep <= 0.0)
  {
    vtkErrorMacro("ShapeSmoothingTimeStep must be positive when shape smoothing is enabled.");
    return 0;
  }

  double b[6];
  input->GetBounds(b);
  vtkBoundingBox box;
  box.SetBounds(b);
  const double L = box.GetMaxLength();
  if (L <= 0.0)
  {
    vtkErrorMacro("Input has zero bounding-box extent.");
    return 0;
  }

  const double minLen = this->MinEdgeLength;
  const double maxLen = this->MaxEdgeLength;
  if (!(minLen > 0.0 && maxLen > minLen))
  {
    vtkErrorMacro("Need 0 < MinEdgeLength < MaxEdgeLength (got "
      << minLen << " / " << maxLen
      << "). In ParaView use scale/Reset on Min and Max Edge Length "
         "(BoundsDomain); non-ParaView callers must set positive lengths explicitly.");
    return 0;
  }

  std::set<vtkIdType> selected;
  if (this->GetNumberOfInputConnections(1) > 0)
  {
    vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
    if (selInfo && selInfo->Has(vtkDataObject::DATA_OBJECT()))
    {
      vtkSelection* inputSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
      if (inputSel && inputSel->GetNumberOfNodes() > 0)
      {
        vtkNew<vtkExtractSelection> extractSelection;
        extractSelection->SetInputData(0, input);
        extractSelection->SetInputData(1, inputSel);
        extractSelection->Update();
        vtkDataSet* extracted = vtkDataSet::SafeDownCast(extractSelection->GetOutputDataObject(0));
        if (extracted &&
          (extracted->GetNumberOfCells() > 0 || extracted->GetNumberOfPoints() > 0))
        {
          CollectCellsFromExtracted(input, extracted, selected);
        }
      }
    }
  }

  if (selected.empty() && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0')
  {
    vtkDataArray* arr = input->GetCellData()->GetArray(this->SelectionCellArrayName);
    if (!arr)
    {
      vtkWarningMacro("SelectionCellArrayName \""
        << this->SelectionCellArrayName << "\" not found on input cell data.");
    }
    else
    {
      const vtkIdType nc = input->GetNumberOfCells();
      for (vtkIdType cid = 0; cid < nc; ++cid)
      {
        bool take = false;
        if (arr->IsIntegral())
        {
          take = (arr->GetTuple1(cid) != 0.0);
        }
        else
        {
          take = (arr->GetTuple1(cid) > 0.5);
        }
        if (take)
        {
          selected.insert(cid);
        }
      }
    }
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  std::vector<Graph_Faces> vtkCellToCgalFace;
  vtkCGALHelper::toCGAL(input, cgalMesh.get(), &vtkCellToCgalFace);

  std::vector<Graph_Faces> remeshFaces;
  remeshFaces.reserve(selected.size());
  if (!selected.empty())
  {
    for (const vtkIdType cid : selected)
    {
      if (cid < 0 || cid >= static_cast<vtkIdType>(vtkCellToCgalFace.size()))
      {
        continue;
      }
      const auto fd = vtkCellToCgalFace[static_cast<size_t>(cid)];
      if (cgalMesh->surface.is_valid(fd))
      {
        remeshFaces.push_back(fd);
      }
    }
  }

  const bool patchRemesh = !remeshFaces.empty();
  if (!selected.empty() && !patchRemesh)
  {
    vtkWarningMacro(
      "Active selection did not resolve to any remeshable faces on the input; remeshing globally.");
  }

  try
  {
    auto featureEdges = get(CGAL::edge_is_feature, cgalMesh->surface);
    pmp::detect_sharp_edges(cgalMesh->surface, this->ProtectAngle, featureEdges);
    ApplySharpFeatureSideFilter(cgalMesh->surface, featureEdges, this->SharpFeatureSideFilter);

    const auto np = pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->NumberOfIterations))
                      .number_of_relaxation_steps(static_cast<unsigned int>(this->NumberOfRelaxationSteps))
                      .protect_constraints(true)
                      .edge_is_constrained_map(featureEdges);

    if (patchRemesh)
    {
      pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
        std::make_pair(minLen, maxLen), remeshFaces, cgalMesh->surface);
      pmp::isotropic_remeshing(remeshFaces, sizing, cgalMesh->surface, np);
    }
    else
    {
      pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
        std::make_pair(minLen, maxLen), cgalMesh->surface.faces(), cgalMesh->surface);
      pmp::isotropic_remeshing(cgalMesh->surface.faces(), sizing, cgalMesh->surface, np);
    }

    if (this->ShapeSmoothingIterations > 0)
    {
      pmp::detect_sharp_edges(cgalMesh->surface, this->ProtectAngle, featureEdges);
      ApplySharpFeatureSideFilter(cgalMesh->surface, featureEdges, this->SharpFeatureSideFilter);

      CGAL_Surface& sm = cgalMesh->surface;
      auto vertexConstrained =
        sm.template add_property_map<Graph_Verts, bool>("v:vespa_shape_smooth_c", false).first;

      for (CGAL_Surface::Edge_index e : sm.edges())
      {
        if (boost::get(featureEdges, e))
        {
          const CGAL_Surface::Halfedge_index h = sm.halfedge(e);
          boost::put(vertexConstrained, sm.source(h), true);
          boost::put(vertexConstrained, sm.target(h), true);
        }
      }

      pmp::smooth_shape(sm, this->ShapeSmoothingTimeStep,
        pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->ShapeSmoothingIterations))
          .vertex_is_constrained_map(vertexConstrained));

      sm.remove_property_map(vertexConstrained);
    }
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  vtkCGALHelper::toVTK(cgalMesh.get(), output);
  this->interpolateAttributes(input, output);

  {
    CGAL_Surface& sm = cgalMesh->surface;
    auto featOutMap = get(CGAL::edge_is_feature, sm);
    pmp::detect_sharp_edges(sm, this->ProtectAngle, featOutMap);
    ApplySharpFeatureSideFilter(sm, featOutMap, this->SharpFeatureSideFilter);
    FillFeaturePolyDataFromSharpEdges(sm, featOutMap, outputFeatures);
  }

  return 1;
}
