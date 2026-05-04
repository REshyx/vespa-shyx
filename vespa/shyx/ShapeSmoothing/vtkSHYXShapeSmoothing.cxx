#include "vtkSHYXShapeSmoothing.h"

#include "vtkCGALHelper.h"

#include <vtkBoundingBox.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

#include <CGAL/boost/graph/helpers.h>
#include <CGAL/Polygon_mesh_processing/angle_and_area_smoothing.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/fair.h>
#include <CGAL/Polygon_mesh_processing/smooth_shape.h>
#include <CGAL/Kernel/global_functions.h>
#include <CGAL/boost/graph/iterator.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

vtkStandardNewMacro(vtkSHYXShapeSmoothing);

namespace pmp = CGAL::Polygon_mesh_processing;

namespace
{
//------------------------------------------------------------------------------
// Helpers replicated from vtkSHYXAdaptiveIsotropicRemesher (file-local). The
// behaviour mirrors the AIR helpers so that "fixed vertices / fixed regions"
// follow the same convention: ProtectAngle + SharpFeatureSideFilter + FeatureMask.
//------------------------------------------------------------------------------

double TupleMagnitude(vtkDataArray* arr, vtkIdType tupleIdx)
{
  if (!arr || tupleIdx < 0 || tupleIdx >= arr->GetNumberOfTuples())
  {
    return 0.0;
  }
  const int nc = arr->GetNumberOfComponents();
  if (nc <= 0)
  {
    return 0.0;
  }
  if (nc == 1)
  {
    return arr->GetTuple1(tupleIdx);
  }
  double sum = 0.0;
  for (int c = 0; c < nc; ++c)
  {
    const double v = arr->GetComponent(tupleIdx, c);
    sum += v * v;
  }
  return std::sqrt(sum);
}

bool ComputeFeatureFaceMask(vtkPolyData* pd, const char* arrayName, double threshold,
  bool allScalars, std::vector<char>& outMask)
{
  outMask.assign(static_cast<size_t>(pd->GetNumberOfCells()), 0);
  if (!pd || !arrayName || arrayName[0] == '\0')
  {
    return false;
  }

  vtkDataArray* const cellArr = pd->GetCellData()->GetArray(arrayName);
  vtkDataArray* const ptArr   = pd->GetPointData()->GetArray(arrayName);
  const vtkIdType nCells      = pd->GetNumberOfCells();
  const vtkIdType nPts        = pd->GetNumberOfPoints();

  const bool cellOk  = (cellArr != nullptr && cellArr->GetNumberOfTuples() == nCells);
  const bool pointOk = (ptArr != nullptr && ptArr->GetNumberOfTuples() == nPts);

  vtkDataArray* arrOnCell = nullptr;
  bool usePointCorners    = false;

  if (cellOk)
  {
    arrOnCell       = cellArr;
    usePointCorners = false;
  }
  else if (pointOk)
  {
    arrOnCell       = ptArr;
    usePointCorners = true;
  }
  else
  {
    return false;
  }

  if (!usePointCorners)
  {
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      const double m = TupleMagnitude(arrOnCell, cid);
      outMask[static_cast<size_t>(cid)] = (m > threshold) ? 1 : 0;
    }
    return true;
  }

  vtkIdType npts;
  const vtkIdType* pids;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    pd->GetCellPoints(cid, npts, pids);
    bool anyPass = false;
    bool allPass = (npts > 0);
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const double m  = TupleMagnitude(arrOnCell, pids[k]);
      const bool pass = (m > threshold);
      anyPass         = anyPass || pass;
      allPass         = allPass && pass;
    }
    const bool cellPass = allScalars ? allPass : anyPass;
    outMask[static_cast<size_t>(cid)] = cellPass ? 1 : 0;
  }
  return true;
}

void ExtractMaskedFacesPolyData(
  vtkPolyData* inMesh, const std::vector<char>& keepMask, vtkPolyData* out)
{
  out->Initialize();
  if (!inMesh || keepMask.size() != static_cast<size_t>(inMesh->GetNumberOfCells()))
  {
    return;
  }

  const vtkIdType nPts   = inMesh->GetNumberOfPoints();
  const vtkIdType nCells = inMesh->GetNumberOfCells();
  if (nCells == 0)
  {
    out->ShallowCopy(inMesh);
    return;
  }

  std::vector<char> usedPt(static_cast<size_t>(nPts), 0);
  vtkIdType npts;
  const vtkIdType* pids;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (!keepMask[static_cast<size_t>(cid)])
    {
      continue;
    }
    inMesh->GetCellPoints(cid, npts, pids);
    for (vtkIdType k = 0; k < npts; ++k)
    {
      if (pids[k] >= 0 && pids[k] < nPts)
      {
        usedPt[static_cast<size_t>(pids[k])] = 1;
      }
    }
  }

  vtkPoints* inPts = inMesh->GetPoints();
  vtkNew<vtkPoints> newPts;
  std::vector<vtkIdType> old2new(static_cast<size_t>(nPts), -1);
  if (inPts)
  {
    double x[3];
    for (vtkIdType i = 0; i < nPts; ++i)
    {
      if (!usedPt[static_cast<size_t>(i)])
      {
        continue;
      }
      inPts->GetPoint(i, x);
      old2new[static_cast<size_t>(i)] = newPts->InsertNextPoint(x);
    }
  }

  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkIdList> remapped;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (!keepMask[static_cast<size_t>(cid)])
    {
      continue;
    }
    inMesh->GetCellPoints(cid, npts, pids);
    remapped->SetNumberOfIds(npts);
    bool ok = true;
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const vtkIdType m = old2new[static_cast<size_t>(pids[k])];
      if (m < 0)
      {
        ok = false;
        break;
      }
      remapped->SetId(k, m);
    }
    if (ok)
    {
      newPolys->InsertNextCell(remapped);
    }
  }

  out->SetPoints(newPts);
  out->SetPolys(newPolys);
}

void BuildCgalFaceIndexToVtkCell(const CGAL_Surface& sm, std::vector<vtkIdType>& faceIdxToVtkCell)
{
  faceIdxToVtkCell.assign(static_cast<size_t>(sm.number_of_faces()), static_cast<vtkIdType>(-1));
  vtkIdType vtkCell = 0;
  for (CGAL_Surface::Face_index f : sm.faces())
  {
    const std::size_t idx = static_cast<std::size_t>(f.idx());
    if (idx < faceIdxToVtkCell.size())
    {
      faceIdxToVtkCell[idx] = vtkCell++;
    }
  }
}

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
    const auto p          = sm.point(sm.source(h0));
    const auto q          = sm.point(sm.target(h0));
    const auto r          = sm.point(sm.target(sm.next(h0)));
    const auto s          = sm.point(sm.target(sm.next(ho)));
    const double signedDeg = CGAL::approximate_dihedral_angle(p, q, r, s);
    const bool dropConcave = (mode == 1 && signedDeg < 0.0);
    const bool dropConvex  = (mode == 2 && signedDeg > 0.0);
    if (dropConcave || dropConvex)
    {
      boost::put(featureEdges, e, false);
    }
  }
}

template <typename EdgeBoolMap>
void ApplyFeatureRegionMaskToSharpEdges(const CGAL_Surface& sm, EdgeBoolMap featureEdges,
  const std::vector<char>& cellPass, const std::vector<vtkIdType>& faceIdxToVtkCell)
{
  if (cellPass.empty() || faceIdxToVtkCell.empty())
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
    const bool b0                         = sm.is_border(h0);
    const bool b1                         = sm.is_border(ho);
    if (b0 && b1)
    {
      continue;
    }
    if (b0 || b1)
    {
      const CGAL_Surface::Face_index f = b0 ? sm.face(ho) : sm.face(h0);
      const std::size_t fi             = static_cast<std::size_t>(f.idx());
      if (fi >= faceIdxToVtkCell.size())
      {
        boost::put(featureEdges, e, false);
        continue;
      }
      const vtkIdType vtkC = faceIdxToVtkCell[fi];
      if (vtkC < 0 || static_cast<std::size_t>(vtkC) >= cellPass.size() ||
        !cellPass[static_cast<std::size_t>(vtkC)])
      {
        boost::put(featureEdges, e, false);
      }
      continue;
    }
    const CGAL_Surface::Face_index fA = sm.face(h0);
    const CGAL_Surface::Face_index fB = sm.face(ho);
    const std::size_t ia              = static_cast<std::size_t>(fA.idx());
    const std::size_t ib              = static_cast<std::size_t>(fB.idx());
    if (ia >= faceIdxToVtkCell.size() || ib >= faceIdxToVtkCell.size())
    {
      boost::put(featureEdges, e, false);
      continue;
    }
    const vtkIdType va = faceIdxToVtkCell[ia];
    const vtkIdType vb = faceIdxToVtkCell[ib];
    const bool passA   = (va >= 0 && static_cast<std::size_t>(va) < cellPass.size() &&
      cellPass[static_cast<std::size_t>(va)]);
    const bool passB   = (vb >= 0 && static_cast<std::size_t>(vb) < cellPass.size() &&
      cellPass[static_cast<std::size_t>(vb)]);
    if (!passA || !passB)
    {
      boost::put(featureEdges, e, false);
    }
  }
}

template <typename EdgeBoolMap>
void AddPatchBoundaryFeatureEdges(
  const CGAL_Surface& sm, EdgeBoolMap featureEdges, const std::vector<char>& faceInPatch)
{
  if (faceInPatch.empty())
  {
    return;
  }
  for (CGAL_Surface::Edge_index e : sm.edges())
  {
    const CGAL_Surface::Halfedge_index h0 = sm.halfedge(e);
    const CGAL_Surface::Halfedge_index ho = sm.opposite(h0);
    const bool b0                         = sm.is_border(h0);
    const bool b1                         = sm.is_border(ho);
    if (!b0 && !b1)
    {
      const CGAL_Surface::Face_index fA = sm.face(h0);
      const CGAL_Surface::Face_index fB = sm.face(ho);
      const std::size_t ia              = static_cast<std::size_t>(fA.idx());
      const std::size_t ib              = static_cast<std::size_t>(fB.idx());
      if (ia >= faceInPatch.size() || ib >= faceInPatch.size())
      {
        continue;
      }
      const bool inA = faceInPatch[ia] != 0;
      const bool inB = faceInPatch[ib] != 0;
      if (inA != inB)
      {
        boost::put(featureEdges, e, true);
      }
      continue;
    }
    if (b0 && b1)
    {
      continue;
    }
    const CGAL_Surface::Face_index f = b0 ? sm.face(ho) : sm.face(h0);
    const std::size_t fi             = static_cast<std::size_t>(f.idx());
    if (fi < faceInPatch.size() && faceInPatch[fi])
    {
      boost::put(featureEdges, e, true);
    }
  }
}

void CollectPatchSharpFeatureWorldPoints(vtkPolyData* maskPatch, double protectAngle,
  int sharpSideFilter, std::vector<std::array<double, 3>>& outAppend)
{
  if (!maskPatch || maskPatch->GetNumberOfCells() == 0)
  {
    return;
  }
  vtkCGALHelper::Vespa_surface patch;
  if (!vtkCGALHelper::toCGAL(maskPatch, &patch, nullptr))
  {
    return;
  }
  auto feat = get(CGAL::edge_is_feature, patch.surface);
  pmp::detect_sharp_edges(patch.surface, protectAngle, feat);
  ApplySharpFeatureSideFilter(patch.surface, feat, sharpSideFilter);
  for (CGAL_Surface::Edge_index e : patch.surface.edges())
  {
    if (!boost::get(feat, e))
    {
      continue;
    }
    const CGAL_Surface::Halfedge_index h = patch.surface.halfedge(e);
    for (CGAL_Surface::Vertex_index vx : { patch.surface.source(h), patch.surface.target(h) })
    {
      const auto& p = patch.surface.point(vx);
      outAppend.push_back(
        { CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()) });
    }
  }
}

template <typename VertBoolMap>
void MarkVerticesNearAnchorPoints(CGAL_Surface& sm, VertBoolMap vertConstrained,
  const std::vector<std::array<double, 3>>& anchors, double tolSq)
{
  if (anchors.empty() || tolSq <= 0.0)
  {
    return;
  }
  for (CGAL_Surface::Vertex_index v : sm.vertices())
  {
    const auto& p   = sm.point(v);
    const double px = CGAL::to_double(p.x());
    const double py = CGAL::to_double(p.y());
    const double pz = CGAL::to_double(p.z());
    for (const auto& a : anchors)
    {
      const double dx = px - a[0];
      const double dy = py - a[1];
      const double dz = pz - a[2];
      if (dx * dx + dy * dy + dz * dz <= tolSq)
      {
        boost::put(vertConstrained, v, true);
        break;
      }
    }
  }
}

template <typename EdgeBoolMap, typename VertBoolMap>
void FillFeatureDiagnosticPolyData(const CGAL_Surface& sm, const EdgeBoolMap& featureEdges,
  const VertBoolMap& vertConstrained, vtkPolyData* out)
{
  out->Initialize();
  vtkNew<vtkPoints> pts;
  std::unordered_map<std::size_t, vtkIdType> vidToPid;
  vidToPid.reserve(static_cast<size_t>(sm.number_of_vertices()));

  const auto ensurePoint = [&](CGAL_Surface::Vertex_index v) -> vtkIdType {
    const std::size_t k = static_cast<std::size_t>(v.idx());
    auto it             = vidToPid.find(k);
    if (it != vidToPid.end())
    {
      return it->second;
    }
    const auto& p     = sm.point(v);
    const vtkIdType id = pts->InsertNextPoint(
      CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    vidToPid.emplace(k, id);
    return id;
  };

  vtkNew<vtkCellArray> lines;
  for (CGAL_Surface::Edge_index e : sm.edges())
  {
    if (!boost::get(featureEdges, e))
    {
      continue;
    }
    const CGAL_Surface::Halfedge_index h = sm.halfedge(e);
    const vtkIdType p0                    = ensurePoint(sm.source(h));
    const vtkIdType p1                    = ensurePoint(sm.target(h));
    vtkIdType c[2]                        = { p0, p1 };
    lines->InsertNextCell(2, c);
  }

  vtkNew<vtkCellArray> verts;
  for (CGAL_Surface::Vertex_index v : sm.vertices())
  {
    if (!boost::get(vertConstrained, v))
    {
      continue;
    }
    const vtkIdType pid = ensurePoint(v);
    verts->InsertNextCell(1, &pid);
  }

  out->SetPoints(pts);
  out->SetLines(lines);
  out->SetVerts(verts);
}

double InputBBoxLongestEdge(vtkPolyData* pd)
{
  if (!pd || pd->GetNumberOfPoints() == 0)
  {
    return 0.0;
  }
  double bounds[6];
  pd->GetBounds(bounds);
  vtkBoundingBox bbox(bounds);
  return std::max({ bbox.GetLength(0), bbox.GetLength(1), bbox.GetLength(2) });
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXShapeSmoothing::vtkSHYXShapeSmoothing()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
vtkSHYXShapeSmoothing::~vtkSHYXShapeSmoothing()
{
  this->SetFeatureMaskArrayName(nullptr);
}

//------------------------------------------------------------------------------
int vtkSHYXShapeSmoothing::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXShapeSmoothing::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SmoothingMethod: " << this->SmoothingMethod << std::endl;
  os << indent << "NumberOfIterations: " << this->NumberOfIterations << std::endl;
  os << indent << "ProtectAngle: " << this->ProtectAngle << std::endl;
  os << indent << "SharpFeatureSideFilter: " << this->SharpFeatureSideFilter << std::endl;
  os << indent << "AnchorTolerance: " << this->AnchorTolerance << std::endl;
  os << indent << "DetectFeatureEdges: " << this->DetectFeatureEdges << std::endl;
  os << indent << "FeatureMaskEnabled: " << this->FeatureMaskEnabled << std::endl;
  os << indent << "FeatureMaskArrayName: "
     << (this->FeatureMaskArrayName ? this->FeatureMaskArrayName : "(null)") << std::endl;
  os << indent << "FeatureMaskThreshold: " << this->FeatureMaskThreshold << std::endl;
  os << indent << "FeatureMaskAllScalars: " << this->FeatureMaskAllScalars << std::endl;
  os << indent << "ShapeTimeStep: " << this->ShapeTimeStep << std::endl;
  os << indent << "ShapeDoScale: " << this->ShapeDoScale << std::endl;
  os << indent << "UseAngleSmoothing: " << this->UseAngleSmoothing << std::endl;
  os << indent << "UseAreaSmoothing: " << this->UseAreaSmoothing << std::endl;
  os << indent << "UseSafetyConstraints: " << this->UseSafetyConstraints << std::endl;
  os << indent << "UseDelaunayFlips: " << this->UseDelaunayFlips << std::endl;
  os << indent << "DoProject: " << this->DoProject << std::endl;
  os << indent << "FairingContinuity: " << this->FairingContinuity << std::endl;
  os << indent << "FairScope: " << this->FairScope << std::endl;
}

//------------------------------------------------------------------------------
int vtkSHYXShapeSmoothing::RequestData(
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

  if (this->NumberOfIterations < 1 && this->SmoothingMethod != FAIR)
  {
    vtkErrorMacro("NumberOfIterations must be >= 1, got " << this->NumberOfIterations);
    return 0;
  }
  if (this->SmoothingMethod == SHAPE_MCF && this->ShapeTimeStep <= 0.0)
  {
    vtkErrorMacro("ShapeTimeStep must be strictly positive for smooth_shape.");
    return 0;
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  if (!vtkCGALHelper::toCGAL(input, cgalMesh.get(), nullptr))
  {
    vtkErrorMacro("Failed to convert input vtkPolyData to a CGAL surface mesh (triangulated "
                  "polydata is required).");
    return 0;
  }

  CGAL_Surface& sm = cgalMesh->surface;
  if (sm.number_of_faces() == 0)
  {
    output->ShallowCopy(input);
    outputFeatures->Initialize();
    return 1;
  }

  // Master switch: when off, sharp-edge / feature-mask sources do not contribute any
  // constraint to the smoother (A+B suppressed). vtkSHYXShapeSmoothing has no vtkSelection
  // input (no C source), so featureEdges and vertexConstrained stay empty in that case.
  const bool detectFeatures = this->DetectFeatureEdges;

  // Threshold-based feature mask, evaluated on the input (same convention as AIR).
  std::vector<char> inputFeatureMask;
  bool inputFeatureMaskOk = false;
  if (detectFeatures && this->FeatureMaskEnabled)
  {
    inputFeatureMaskOk = ComputeFeatureFaceMask(input, this->FeatureMaskArrayName,
      this->FeatureMaskThreshold, this->FeatureMaskAllScalars, inputFeatureMask);
    if (!inputFeatureMaskOk)
    {
      vtkWarningMacro("Feature Mask is ON but array '"
        << (this->FeatureMaskArrayName ? this->FeatureMaskArrayName : "")
        << "' was not found on the input or its size mismatched. Feature Mask is ignored.");
    }
  }
  const bool useMask = detectFeatures && this->FeatureMaskEnabled && inputFeatureMaskOk;

  // Anchor positions from sharp endpoints inside the mask threshold patch (input topology).
  std::vector<std::array<double, 3>> anchorPoints;
  if (useMask)
  {
    vtkNew<vtkPolyData> inputMaskPatch;
    ExtractMaskedFacesPolyData(input, inputFeatureMask, inputMaskPatch);
    CollectPatchSharpFeatureWorldPoints(
      inputMaskPatch, this->ProtectAngle, this->SharpFeatureSideFilter, anchorPoints);
  }

  // Build the constrained vertex / edge property maps.
  auto featureEdges = get(CGAL::edge_is_feature, sm);
  auto vertexConstrained =
    sm.template add_property_map<Graph_Verts, bool>("v:vespa_smooth_constrained", false).first;

  try
  {
    std::vector<char> faceInMaskRegion;
    if (detectFeatures)
    {
      pmp::detect_sharp_edges(sm, this->ProtectAngle, featureEdges);
      ApplySharpFeatureSideFilter(sm, featureEdges, this->SharpFeatureSideFilter);

      if (useMask)
      {
        std::vector<vtkIdType> faceIdxToSm;
        BuildCgalFaceIndexToVtkCell(sm, faceIdxToSm);
        ApplyFeatureRegionMaskToSharpEdges(sm, featureEdges, inputFeatureMask, faceIdxToSm);

        faceInMaskRegion.assign(static_cast<size_t>(sm.number_of_faces()), 0);
        for (CGAL_Surface::Face_index f : sm.faces())
        {
          const std::size_t fi = static_cast<std::size_t>(f.idx());
          if (fi >= faceIdxToSm.size())
          {
            continue;
          }
          const vtkIdType vtkC = faceIdxToSm[fi];
          if (vtkC >= 0 && static_cast<size_t>(vtkC) < inputFeatureMask.size() &&
            inputFeatureMask[static_cast<size_t>(vtkC)])
          {
            faceInMaskRegion[fi] = 1;
          }
        }
        AddPatchBoundaryFeatureEdges(sm, featureEdges, faceInMaskRegion);
      }

      // Constrained vertices = endpoints of feature edges (sharp + mask boundary).
      for (CGAL_Surface::Edge_index e : sm.edges())
      {
        if (!boost::get(featureEdges, e))
        {
          continue;
        }
        const CGAL_Surface::Halfedge_index h = sm.halfedge(e);
        boost::put(vertexConstrained, sm.source(h), true);
        boost::put(vertexConstrained, sm.target(h), true);
      }
    }

    // Anchor snapping (mask patch sharp endpoints) — same convention as AIR.
    // anchorPoints is empty when detectFeatures is false, so this is a no-op.
    const double L           = InputBBoxLongestEdge(input);
    const double anchorTolL  = std::max(0.0, this->AnchorTolerance) * (L > 0.0 ? L : 1.0);
    const double anchorTolSq = std::max(1e-36, anchorTolL * anchorTolL);
    MarkVerticesNearAnchorPoints(sm, vertexConstrained, anchorPoints, anchorTolSq);

    // Dispatch to the selected algorithm.
    if (this->SmoothingMethod == SHAPE_MCF)
    {
      // CGAL `do_scale` (volume-preserving rescale after MCF) is silently disabled in
      // curvature_flow_impl::init_smoothing() unless the mesh is closed AND there is at most one
      // constrained vertex. Detect both cases so the user is not left wondering why a "pipe"
      // (open boundary) keeps shrinking with ShapeDoScale = ON.
      if (this->ShapeDoScale)
      {
        const bool meshClosed = CGAL::is_closed(sm);
        std::size_t nbConstrained = 0;
        for (CGAL_Surface::Vertex_index v : sm.vertices())
        {
          if (boost::get(vertexConstrained, v))
          {
            if (++nbConstrained > 1)
            {
              break;
            }
          }
        }
        if (!meshClosed)
        {
          vtkWarningMacro(
            "smooth_shape: ShapeDoScale (do_scale) is ignored by CGAL because the input mesh is "
            "not closed (e.g. a pipe with inlet/outlet boundary loops). Mean curvature flow will "
            "shrink the surface. To preserve overall shape/volume on open meshes, switch "
            "Algorithm to 'Angle & Area' and keep 'Reproject on input (do_project)' = ON.");
        }
        else if (nbConstrained > 1)
        {
          vtkWarningMacro(
            "smooth_shape: ShapeDoScale (do_scale) is ignored by CGAL because more than one "
            "vertex is constrained ("
            << nbConstrained
            << " constrained vertices, anchor must be unique). Increase Protection Angle to "
               "drop sharp constraints, disable Feature Mask, or switch Algorithm to "
               "'Angle & Area' with 'Reproject on input' = ON.");
        }
      }

      pmp::smooth_shape(sm, this->ShapeTimeStep,
        pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->NumberOfIterations))
          .vertex_is_constrained_map(vertexConstrained)
          .do_scale(this->ShapeDoScale));
    }
    else if (this->SmoothingMethod == ANGLE_AND_AREA)
    {
      bool effectiveUseArea = this->UseAreaSmoothing;
#ifndef CGAL_PMP_USE_CERES_SOLVER
      // Without Ceres, CGAL silently disables area smoothing internally; angle path still runs.
      effectiveUseArea = false;
#endif
      if (!this->UseAngleSmoothing && !effectiveUseArea)
      {
        vtkWarningMacro("angle_and_area_smoothing: both angle and area smoothing are disabled "
                        "(or area is unavailable). Mesh is unchanged.");
      }
      pmp::angle_and_area_smoothing(sm,
        pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->NumberOfIterations))
          .use_angle_smoothing(this->UseAngleSmoothing)
          .use_area_smoothing(effectiveUseArea)
          .use_safety_constraints(this->UseSafetyConstraints)
          .use_Delaunay_flips(this->UseDelaunayFlips)
          .do_project(this->DoProject)
          .vertex_is_constrained_map(vertexConstrained)
          .edge_is_constrained_map(featureEdges));
    }
    else // FAIR
    {
      std::vector<Graph_Verts> fairVerts;
      fairVerts.reserve(static_cast<size_t>(sm.number_of_vertices()));

      const bool maskScope = (this->FairScope == FAIR_MASK_REGION_ONLY);
      if (maskScope && !useMask)
      {
        vtkWarningMacro("FairScope = MaskRegionOnly but Feature Mask is OFF / unresolved "
                        "(or Detect feature edges is OFF); falling back to all "
                        "non-constrained vertices.");
      }

      for (CGAL_Surface::Vertex_index v : sm.vertices())
      {
        if (boost::get(vertexConstrained, v))
        {
          continue;
        }
        if (maskScope && useMask)
        {
          const CGAL_Surface::Halfedge_index hv = sm.halfedge(v);
          if (hv == CGAL_Surface::null_halfedge())
          {
            continue;
          }
          bool allIn = true;
          bool any   = false;
          for (CGAL_Surface::Halfedge_index h : halfedges_around_target(hv, sm))
          {
            if (sm.is_border(h))
            {
              allIn = false;
              continue;
            }
            const CGAL_Surface::Face_index f = sm.face(h);
            const std::size_t fi             = static_cast<std::size_t>(f.idx());
            if (fi >= faceInMaskRegion.size() || !faceInMaskRegion[fi])
            {
              allIn = false;
            }
            any = true;
          }
          if (!any || !allIn)
          {
            continue;
          }
        }
        fairVerts.push_back(v);
      }

      if (fairVerts.empty())
      {
        vtkWarningMacro("Fair: vertex range is empty (every vertex is constrained or outside the "
                        "mask region). Mesh is unchanged.");
      }
      else if (fairVerts.size() == static_cast<size_t>(sm.number_of_vertices()))
      {
        vtkErrorMacro("Fair: every vertex would be moved (no boundary conditions). CGAL would "
                      "shrink the mesh to the origin. Enable Detect feature edges, lower "
                      "ProtectAngle, or enable Feature Mask before fairing.");
        sm.remove_property_map(vertexConstrained);
        return 0;
      }
      else
      {
        const bool ok = pmp::fair(sm, fairVerts,
          pmp::parameters::fairing_continuity(static_cast<unsigned int>(this->FairingContinuity)));
        if (!ok)
        {
          vtkWarningMacro("CGAL fair() failed (linear system was not solved); mesh unchanged.");
        }
      }
    }

    // Diagnostic feature output (port 1).
    FillFeatureDiagnosticPolyData(sm, featureEdges, vertexConstrained, outputFeatures);
  }
  catch (std::exception& e)
  {
    sm.remove_property_map(vertexConstrained);
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  sm.remove_property_map(vertexConstrained);

  vtkCGALHelper::toVTK(cgalMesh.get(), output);
  this->copyAttributes(input, output);

  return 1;
}
