#include "vtkSHYXAdaptiveIsotropicRemesher.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkBoundingBox.h>
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
#include <vtkPolyData.h>
#include <vtkSelection.h>

#include <CGAL/Polygon_mesh_processing/Adaptive_sizing_field.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>

#include <exception>
#include <memory>
#include <set>
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
} // namespace

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetNumberOfInputPorts(2);
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
int vtkSHYXAdaptiveIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  if (!input || !output)
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

    const auto np = pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->NumberOfIterations))
                      .number_of_relaxation_steps(3)
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
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  vtkCGALHelper::toVTK(cgalMesh.get(), output);
  this->interpolateAttributes(input, output);

  return 1;
}
