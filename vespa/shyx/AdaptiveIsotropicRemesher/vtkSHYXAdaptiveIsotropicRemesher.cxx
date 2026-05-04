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
#include <vtkCellType.h>
#include <vtkIdList.h>

#include <CGAL/Polygon_mesh_processing/Adaptive_sizing_field.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Polygon_mesh_processing/smooth_shape.h>
#include <CGAL/Kernel/global_functions.h>
#include <CGAL/boost/graph/iterator.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

vtkStandardNewMacro(vtkSHYXAdaptiveIsotropicRemesher);

namespace pmp = CGAL::Polygon_mesh_processing;

namespace
{
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

bool ComputeFeatureFaceMask(vtkPolyData* pd, const char* arrayName, double threshold, bool allScalars,
  std::vector<char>& outMask)
{
  outMask.assign(static_cast<size_t>(pd->GetNumberOfCells()), 0);
  if (!pd || !arrayName || arrayName[0] == '\0')
  {
    return false;
  }

  vtkDataArray* const cellArr = pd->GetCellData()->GetArray(arrayName);
  vtkDataArray* const ptArr = pd->GetPointData()->GetArray(arrayName);
  const vtkIdType nCells = pd->GetNumberOfCells();
  const vtkIdType nPts = pd->GetNumberOfPoints();

  const bool cellOk = (cellArr != nullptr && cellArr->GetNumberOfTuples() == nCells);
  const bool pointOk = (ptArr != nullptr && ptArr->GetNumberOfTuples() == nPts);

  vtkDataArray* arrOnCell = nullptr;
  bool usePointCorners = false;

  if (cellOk && pointOk)
  {
    // Same name on point and cell: prefer cell (per-cell threshold; All Scalars ignored).
    arrOnCell = cellArr;
    usePointCorners = false;
  }
  else if (cellOk)
  {
    arrOnCell = cellArr;
    usePointCorners = false;
  }
  else if (pointOk)
  {
    arrOnCell = ptArr;
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
      const double m = TupleMagnitude(arrOnCell, pids[k]);
      const bool pass = (m > threshold);
      anyPass = anyPass || pass;
      allPass = allPass && pass;
    }
    const bool cellPass = allScalars ? allPass : anyPass;
    outMask[static_cast<size_t>(cid)] = cellPass ? 1 : 0;
  }
  return true;
}

void ExtractMaskedFacesPolyData(vtkPolyData* inMesh, const std::vector<char>& keepMask, vtkPolyData* out)
{
  out->Initialize();
  if (!inMesh || keepMask.size() != static_cast<size_t>(inMesh->GetNumberOfCells()))
  {
    return;
  }

  const vtkIdType nPts = inMesh->GetNumberOfPoints();
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

  vtkNew<vtkCellArray> newVerts;
  vtkNew<vtkCellArray> newLines;
  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkCellArray> newStrips;
  vtkNew<vtkIdList> remapped;
  std::vector<vtkIdType> keptOrig;

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (!keepMask[static_cast<size_t>(cid)])
    {
      continue;
    }
    const int ctype = inMesh->GetCellType(cid);
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
    if (!ok)
    {
      continue;
    }

    vtkCellArray* target = newPolys;
    switch (ctype)
    {
      case VTK_VERTEX:
      case VTK_POLY_VERTEX:
        target = newVerts;
        break;
      case VTK_LINE:
      case VTK_POLY_LINE:
        target = newLines;
        break;
      case VTK_TRIANGLE_STRIP:
        target = newStrips;
        break;
      default:
        break;
    }
    target->InsertNextCell(remapped);
    keptOrig.push_back(cid);
  }

  out->SetPoints(newPts);
  out->SetVerts(newVerts);
  out->SetLines(newLines);
  out->SetPolys(newPolys);
  out->SetStrips(newStrips);

  const vtkIdType nOutCells = static_cast<vtkIdType>(keptOrig.size());
  out->GetCellData()->CopyAllocate(inMesh->GetCellData(), nOutCells);
  for (vtkIdType j = 0; j < nOutCells; ++j)
  {
    out->GetCellData()->CopyData(inMesh->GetCellData(), keptOrig[static_cast<size_t>(j)], j);
  }

  const vtkIdType nOutPts = newPts->GetNumberOfPoints();
  out->GetPointData()->CopyAllocate(inMesh->GetPointData(), nOutPts);
  for (vtkIdType oldIdx = 0; oldIdx < nPts; ++oldIdx)
  {
    const vtkIdType newIdx = old2new[static_cast<size_t>(oldIdx)];
    if (newIdx >= 0)
    {
      out->GetPointData()->CopyData(inMesh->GetPointData(), oldIdx, newIdx);
    }
  }

  out->GetFieldData()->PassData(inMesh->GetFieldData());
  out->Squeeze();
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
void MarkFeatureEdgeByVertices(CGAL_Surface& sm, EdgeBoolMap featureEdges,
  CGAL_Surface::Vertex_index va, CGAL_Surface::Vertex_index vb)
{
  for (CGAL_Surface::Halfedge_index h : halfedges_around_source(va, sm))
  {
    if (sm.target(h) == vb)
    {
      boost::put(featureEdges, sm.edge(h), true);
      return;
    }
  }
}

struct EdgeKeyHash
{
  size_t operator()(std::pair<vtkIdType, vtkIdType> k) const noexcept
  {
    return (static_cast<size_t>(k.first) << 32) ^ static_cast<size_t>(k.second);
  }
};
using EdgeCellMap =
  std::unordered_map<std::pair<vtkIdType, vtkIdType>, std::vector<vtkIdType>, EdgeKeyHash>;

void BuildEdgeCells(vtkPolyData* pd, EdgeCellMap& edgeCells)
{
  vtkIdType npts;
  const vtkIdType* pids;
  const vtkIdType nCells = pd->GetNumberOfCells();
  edgeCells.reserve(static_cast<size_t>(nCells) * 2);
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    pd->GetCellPoints(cid, npts, pids);
    for (vtkIdType a = 0; a < npts; ++a)
    {
      vtkIdType u = pids[a];
      vtkIdType v = pids[(a + 1) % npts];
      if (u > v)
      {
        std::swap(u, v);
      }
      edgeCells[{ u, v }].push_back(cid);
    }
  }
}

template <typename Callback>
void ForEachSelectionBoundaryEdge(
  vtkPolyData* pd, const std::set<vtkIdType>& selected, Callback cb)
{
  if (!pd || selected.empty())
  {
    return;
  }
  EdgeCellMap edgeCells;
  BuildEdgeCells(pd, edgeCells);
  const vtkIdType nPts = pd->GetNumberOfPoints();
  for (auto& kv : edgeCells)
  {
    auto& ids = kv.second;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    const vtkIdType pu = kv.first.first;
    const vtkIdType pv = kv.first.second;
    if (pu < 0 || pv < 0 || pu >= nPts || pv >= nPts)
    {
      continue;
    }

    bool boundary = false;
    if (ids.size() == 2)
    {
      const bool s0 = selected.count(ids[0]) != 0u;
      const bool s1 = selected.count(ids[1]) != 0u;
      boundary = (s0 != s1);
    }
    else if (ids.size() == 1)
    {
      boundary = (selected.count(ids[0]) != 0u);
    }
    if (boundary)
    {
      cb(pu, pv);
    }
  }
}

template <typename EdgeBoolMap>
void AddSelectionBoundaryUsingVtkTopology(vtkObject* logger, vtkPolyData* pd, CGAL_Surface& sm,
  EdgeBoolMap featureEdges, const std::set<vtkIdType>& selected)
{
  if (!pd || selected.empty())
  {
    return;
  }
  if (static_cast<size_t>(sm.number_of_vertices()) !=
      static_cast<size_t>(pd->GetNumberOfPoints()))
  {
    if (logger)
    {
      vtkWarningWithObjectMacro(logger,
        << "Selection boundary constraints skipped: VTK point count does not match CGAL vertex "
           "count (expected identical topology to port 0 input used for remesh).");
    }
    return;
  }

  ForEachSelectionBoundaryEdge(pd, selected, [&](vtkIdType pu, vtkIdType pv) {
    const CGAL_Surface::Vertex_index va(static_cast<std::size_t>(pu));
    const CGAL_Surface::Vertex_index vb(static_cast<std::size_t>(pv));
    MarkFeatureEdgeByVertices(sm, featureEdges, va, vb);
  });
}

void CollectSelectionBoundaryWorldPoints(vtkPolyData* pd, const std::set<vtkIdType>& selected,
  std::vector<std::array<double, 3>>& out)
{
  ForEachSelectionBoundaryEdge(pd, selected, [&](vtkIdType pu, vtkIdType pv) {
    double x[3];
    pd->GetPoint(pu, x);
    out.push_back({ x[0], x[1], x[2] });
    pd->GetPoint(pv, x);
    out.push_back({ x[0], x[1], x[2] });
  });
}

template <typename EdgeBoolMapFull, typename EdgeBoolMapPatch>
void LiftPatchSharpFeaturesToFullMesh(vtkObject* logger, CGAL_Surface& fullSm, EdgeBoolMapFull fullFeatures,
  const CGAL_Surface& patchSm, EdgeBoolMapPatch patchFeatures, double bboxLength)
{
  const double scale = (bboxLength > 0.0) ? (1.0e9 / bboxLength) : 1.0e9;

  using QuantKey = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
  const auto quant = [scale](const auto& p) -> QuantKey {
    return {
      static_cast<std::int64_t>(std::llround(CGAL::to_double(p.x()) * scale)),
      static_cast<std::int64_t>(std::llround(CGAL::to_double(p.y()) * scale)),
      static_cast<std::int64_t>(std::llround(CGAL::to_double(p.z()) * scale)),
    };
  };

  std::map<QuantKey, CGAL_Surface::Vertex_index> fullVertByQuant;
  for (CGAL_Surface::Vertex_index v : fullSm.vertices())
  {
    fullVertByQuant[quant(fullSm.point(v))] = v;
  }

  std::size_t missing = 0;
  for (CGAL_Surface::Edge_index pe : patchSm.edges())
  {
    if (!boost::get(patchFeatures, pe))
    {
      continue;
    }
    const CGAL_Surface::Halfedge_index ph = patchSm.halfedge(pe);
    const CGAL_Surface::Vertex_index pv0 = patchSm.source(ph);
    const CGAL_Surface::Vertex_index pv1 = patchSm.target(ph);
    const auto it0 = fullVertByQuant.find(quant(patchSm.point(pv0)));
    const auto it1 = fullVertByQuant.find(quant(patchSm.point(pv1)));
    if (it0 == fullVertByQuant.end() || it1 == fullVertByQuant.end())
    {
      ++missing;
      continue;
    }
    MarkFeatureEdgeByVertices(fullSm, fullFeatures, it0->second, it1->second);
  }

  if (missing != 0u && logger)
  {
    vtkWarningWithObjectMacro(logger,
      << "LiftPatchSharpFeaturesToFullMesh: could not map " << missing
      << " patch feature edge(s) onto the full mesh (coordinate quantization).");
  }
}

template <typename EdgeBoolMap>
void AddPatchBoundaryFeatureEdges(const CGAL_Surface& sm, EdgeBoolMap featureEdges,
  const std::vector<char>& faceInPatch)
{
  if (faceInPatch.empty())
  {
    return;
  }
  for (CGAL_Surface::Edge_index e : sm.edges())
  {
    const CGAL_Surface::Halfedge_index h0 = sm.halfedge(e);
    const CGAL_Surface::Halfedge_index ho = sm.opposite(h0);
    const bool b0 = sm.is_border(h0);
    const bool b1 = sm.is_border(ho);
    if (!b0 && !b1)
    {
      const CGAL_Surface::Face_index fA = sm.face(h0);
      const CGAL_Surface::Face_index fB = sm.face(ho);
      const std::size_t ia = static_cast<std::size_t>(fA.idx());
      const std::size_t ib = static_cast<std::size_t>(fB.idx());
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
    if (b0 || b1)
    {
      const CGAL_Surface::Face_index f = b0 ? sm.face(ho) : sm.face(h0);
      const std::size_t fi = static_cast<std::size_t>(f.idx());
      if (fi < faceInPatch.size() && faceInPatch[fi])
      {
        boost::put(featureEdges, e, true);
      }
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
    const bool b0 = sm.is_border(h0);
    const bool b1 = sm.is_border(ho);
    if (b0 && b1)
    {
      continue;
    }
    if (b0 || b1)
    {
      const CGAL_Surface::Face_index f = b0 ? sm.face(ho) : sm.face(h0);
      const std::size_t fi = static_cast<std::size_t>(f.idx());
      if (fi >= faceIdxToVtkCell.size())
      {
        boost::put(featureEdges, e, false);
        continue;
      }
      const vtkIdType vtkC = faceIdxToVtkCell[fi];
      if (vtkC < 0 || static_cast<std::size_t>(vtkC) >= cellPass.size() || !cellPass[static_cast<std::size_t>(vtkC)])
      {
        boost::put(featureEdges, e, false);
      }
      continue;
    }
    const CGAL_Surface::Face_index fA = sm.face(h0);
    const CGAL_Surface::Face_index fB = sm.face(ho);
    const std::size_t ia = static_cast<std::size_t>(fA.idx());
    const std::size_t ib = static_cast<std::size_t>(fB.idx());
    if (ia >= faceIdxToVtkCell.size() || ib >= faceIdxToVtkCell.size())
    {
      boost::put(featureEdges, e, false);
      continue;
    }
    const vtkIdType va = faceIdxToVtkCell[ia];
    const vtkIdType vb = faceIdxToVtkCell[ib];
    const bool passA = (va >= 0 && static_cast<std::size_t>(va) < cellPass.size() && cellPass[static_cast<std::size_t>(va)]);
    const bool passB = (vb >= 0 && static_cast<std::size_t>(vb) < cellPass.size() && cellPass[static_cast<std::size_t>(vb)]);
    if (!passA || !passB)
    {
      boost::put(featureEdges, e, false);
    }
  }
}

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
  std::unordered_set<vtkIdType> selPt;
  selPt.reserve(static_cast<size_t>(ptIds->GetNumberOfTuples()));
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
void DetectSharpEdgesWithFilter(
  CGAL_Surface& sm, double protectAngleDeg, int sharpSideMode, EdgeBoolMap featureEdges)
{
  pmp::detect_sharp_edges(sm, protectAngleDeg, featureEdges);
  ApplySharpFeatureSideFilter(sm, featureEdges, sharpSideMode);
}

void CollectPatchSharpFeatureWorldPoints(vtkPolyData* maskPatch, double protectAngle, int sharpSideFilter,
  std::vector<std::array<double, 3>>& outAppend)
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
  DetectSharpEdgesWithFilter(patch.surface, protectAngle, sharpSideFilter, feat);
  for (CGAL_Surface::Edge_index e : patch.surface.edges())
  {
    if (!boost::get(feat, e))
    {
      continue;
    }
    const CGAL_Surface::Halfedge_index h = patch.surface.halfedge(e);
    const CGAL_Surface::Vertex_index v0 = patch.surface.source(h);
    const CGAL_Surface::Vertex_index v1 = patch.surface.target(h);
    for (CGAL_Surface::Vertex_index vx : { v0, v1 })
    {
      const auto& p = patch.surface.point(vx);
      outAppend.push_back({ CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()) });
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
    const auto& p = sm.point(v);
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

template <typename EdgeBoolMap>
void FillFeaturePolyDataSharpLinesOnly(const CGAL_Surface& sm, const EdgeBoolMap& featureEdges, vtkPolyData* out)
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
    const vtkIdType id =
      pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    vidToPid.emplace(k, id);
    return id;
  };

  vtkNew<vtkCellArray> lines;
  vtkNew<vtkCellArray> verts;

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
  }

  out->SetPoints(pts);
  out->SetLines(lines);
  out->SetVerts(verts);
}

template <typename EdgeBoolMap>
void AppendSharpFeatureVertsToPolyData(vtkPolyData* out, const CGAL_Surface& sm, const EdgeBoolMap& featureEdges)
{
  if (!out)
  {
    return;
  }
  vtkPoints* pts = out->GetPoints();
  if (!pts)
  {
    vtkNew<vtkPoints> np;
    out->SetPoints(np);
    pts = out->GetPoints();
  }
  vtkNew<vtkCellArray> combinedVerts;
  vtkCellArray* oldVerts = out->GetVerts();
  if (oldVerts != nullptr && oldVerts->GetNumberOfCells() > 0)
  {
    combinedVerts->DeepCopy(oldVerts);
  }

  std::unordered_set<std::size_t> emitted;
  emitted.reserve(static_cast<size_t>(sm.number_of_vertices()));

  for (CGAL_Surface::Edge_index e : sm.edges())
  {
    if (!boost::get(featureEdges, e))
    {
      continue;
    }
    const CGAL_Surface::Halfedge_index h0 = sm.halfedge(e);
    for (CGAL_Surface::Vertex_index vx :
      { sm.source(h0), sm.target(h0) })
    {
      const std::size_t k = static_cast<std::size_t>(vx.idx());
      if (!emitted.insert(k).second)
      {
        continue;
      }
      const auto& p = sm.point(vx);
      const vtkIdType pid = pts->InsertNextPoint(
        CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
      combinedVerts->InsertNextCell(1, &pid);
    }
  }

  out->SetVerts(combinedVerts);
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(4);
}

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::~vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetFeatureMaskArrayName(nullptr);
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
  os << indent << "RemeshProtectConstraints: " << (this->RemeshProtectConstraints ? "on" : "off") << std::endl;
  os << indent << "RemeshCollapseConstraints: " << (this->RemeshCollapseConstraints ? "on" : "off") << std::endl;
  os << indent << "RemeshRelaxConstraints: " << (this->RemeshRelaxConstraints ? "on" : "off") << std::endl;
  os << indent << "RemeshDoSplit: " << (this->RemeshDoSplit ? "on" : "off") << std::endl;
  os << indent << "RemeshDoCollapse: " << (this->RemeshDoCollapse ? "on" : "off") << std::endl;
  os << indent << "RemeshDoFlip: " << (this->RemeshDoFlip ? "on" : "off") << std::endl;
  os << indent << "DetectFeatureEdges: " << (this->DetectFeatureEdges ? "on" : "off") << std::endl;
  os << indent << "FeatureMaskEnabled: " << (this->FeatureMaskEnabled ? "on" : "off") << std::endl;
  os << indent << "FeatureMaskArrayName: "
     << (this->FeatureMaskArrayName ? this->FeatureMaskArrayName : "(null)") << std::endl;
  os << indent << "FeatureMaskThreshold: " << this->FeatureMaskThreshold << std::endl;
  os << indent << "FeatureMaskAllScalars: " << (this->FeatureMaskAllScalars ? "on" : "off") << std::endl;
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
  if (port == 0 || port == 1 || port == 2 || port == 3)
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
  vtkPolyData* output          = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outputFeatures  = vtkPolyData::GetData(outputVector, 1);
  vtkPolyData* outputMaskPatch = vtkPolyData::GetData(outputVector, 2);
  vtkPolyData* outputMaskPatchRemeshed = vtkPolyData::GetData(outputVector, 3);

  if (!input || !output || !outputFeatures || !outputMaskPatch || !outputMaskPatchRemeshed)
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

  std::vector<char> inputFeatureMask;
  bool inputFeatureMaskOk = false;
  if (this->FeatureMaskEnabled)
  {
    inputFeatureMaskOk = ComputeFeatureFaceMask(input, this->FeatureMaskArrayName,
      this->FeatureMaskThreshold, this->FeatureMaskAllScalars, inputFeatureMask);
    if (!inputFeatureMaskOk)
    {
      vtkWarningMacro("Feature mask is enabled but the chosen array could not be evaluated on the "
                      "input. Output port 2 is empty; CGAL constraints and port 1 use unmasked "
                      "sharp-edge detection.");
      outputMaskPatch->Initialize();
    }
    else
    {
      ExtractMaskedFacesPolyData(input, inputFeatureMask, outputMaskPatch);
    }
  }
  else
  {
    outputMaskPatch->Initialize();
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
    if (this->DetectFeatureEdges)
    {
      DetectSharpEdgesWithFilter(
        cgalMesh->surface, this->ProtectAngle, this->SharpFeatureSideFilter, featureEdges);
      if (this->FeatureMaskEnabled && inputFeatureMaskOk)
      {
        std::vector<vtkIdType> faceIdxToVtkIn;
        BuildCgalFaceIndexToVtkCell(cgalMesh->surface, faceIdxToVtkIn);
        ApplyFeatureRegionMaskToSharpEdges(
          cgalMesh->surface, featureEdges, inputFeatureMask, faceIdxToVtkIn);
      }

      if (this->FeatureMaskEnabled && inputFeatureMaskOk &&
        outputMaskPatch->GetNumberOfCells() > 0)
      {
        vtkCGALHelper::Vespa_surface cgalForPort1Patch;
        if (vtkCGALHelper::toCGAL(outputMaskPatch, &cgalForPort1Patch, nullptr))
        {
          auto patchFeat = get(CGAL::edge_is_feature, cgalForPort1Patch.surface);
          DetectSharpEdgesWithFilter(cgalForPort1Patch.surface, this->ProtectAngle,
            this->SharpFeatureSideFilter, patchFeat);
          LiftPatchSharpFeaturesToFullMesh(this, cgalMesh->surface, featureEdges,
            cgalForPort1Patch.surface, patchFeat, L);
        }
      }
    }

    if (!selected.empty())
    {
      AddSelectionBoundaryUsingVtkTopology(this, input, cgalMesh->surface, featureEdges, selected);
    }

    const auto remeshNp = [&]() {
      return pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->NumberOfIterations))
        .number_of_relaxation_steps(static_cast<unsigned int>(this->NumberOfRelaxationSteps))
        .protect_constraints(this->RemeshProtectConstraints)
        .collapse_constraints(this->RemeshCollapseConstraints)
        .relax_constraints(this->RemeshRelaxConstraints)
        .do_split(this->RemeshDoSplit)
        .do_collapse(this->RemeshDoCollapse)
        .do_flip(this->RemeshDoFlip)
        .edge_is_constrained_map(featureEdges);
    };

    if (patchRemesh)
    {
      pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
        std::make_pair(minLen, maxLen), remeshFaces, cgalMesh->surface);
      pmp::isotropic_remeshing(remeshFaces, sizing, cgalMesh->surface, remeshNp());
    }
    else
    {
      pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
        std::make_pair(minLen, maxLen), cgalMesh->surface.faces(), cgalMesh->surface);
      pmp::isotropic_remeshing(cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp());
    }

    if (this->ShapeSmoothingIterations > 0)
    {
      std::vector<std::array<double, 3>> smoothAnchorPoints;
      CollectSelectionBoundaryWorldPoints(input, selected, smoothAnchorPoints);
      if (this->DetectFeatureEdges && this->FeatureMaskEnabled)
      {
        vtkNew<vtkPolyData> remeshedSurfForSmoothAnchors;
        vtkCGALHelper::toVTK(cgalMesh.get(), remeshedSurfForSmoothAnchors);
        this->interpolateAttributes(input, remeshedSurfForSmoothAnchors);
        std::vector<char> remeshMaskCells;
        if (ComputeFeatureFaceMask(remeshedSurfForSmoothAnchors, this->FeatureMaskArrayName,
              this->FeatureMaskThreshold, this->FeatureMaskAllScalars, remeshMaskCells))
        {
          vtkNew<vtkPolyData> remeshedThresholdPatch;
          ExtractMaskedFacesPolyData(remeshedSurfForSmoothAnchors, remeshMaskCells, remeshedThresholdPatch);
          if (remeshedThresholdPatch->GetNumberOfCells() > 0)
          {
            CollectPatchSharpFeatureWorldPoints(remeshedThresholdPatch, this->ProtectAngle,
              this->SharpFeatureSideFilter, smoothAnchorPoints);
          }
        }
      }
      const double smoothAnchorTolSq = std::max(1e-36, (1e-4 * L) * (1e-4 * L));

      if (this->DetectFeatureEdges)
      {
        DetectSharpEdgesWithFilter(
          cgalMesh->surface, this->ProtectAngle, this->SharpFeatureSideFilter, featureEdges);

        vtkNew<vtkPolyData> tmpSurf;
        vtkCGALHelper::toVTK(cgalMesh.get(), tmpSurf);
        this->interpolateAttributes(input, tmpSurf);

        if (this->FeatureMaskEnabled)
        {
          std::vector<char> smoothMask;
          if (ComputeFeatureFaceMask(tmpSurf, this->FeatureMaskArrayName,
                this->FeatureMaskThreshold, this->FeatureMaskAllScalars, smoothMask))
          {
            std::vector<vtkIdType> faceIdxToSm;
            BuildCgalFaceIndexToVtkCell(cgalMesh->surface, faceIdxToSm);
            ApplyFeatureRegionMaskToSharpEdges(
              cgalMesh->surface, featureEdges, smoothMask, faceIdxToSm);

            std::vector<char> faceInMaskRegion(
              static_cast<size_t>(cgalMesh->surface.number_of_faces()), 0);
            for (CGAL_Surface::Face_index f : cgalMesh->surface.faces())
            {
              const std::size_t fi = static_cast<std::size_t>(f.idx());
              if (fi >= faceIdxToSm.size())
              {
                continue;
              }
              const vtkIdType vtkC = faceIdxToSm[fi];
              if (vtkC >= 0 && static_cast<size_t>(vtkC) < smoothMask.size() &&
                  smoothMask[static_cast<size_t>(vtkC)])
              {
                faceInMaskRegion[fi] = 1;
              }
            }
            AddPatchBoundaryFeatureEdges(cgalMesh->surface, featureEdges, faceInMaskRegion);
          }
          else
          {
            vtkWarningMacro("Feature mask: could not evaluate threshold on the remeshed surface "
                            "before smooth_shape (same array/threshold as remesh); mask clipping "
                            "and mask-boundary feature constraints are skipped for smoothing.");
          }
        }
      }
      else
      {
        for (CGAL_Surface::Edge_index e : cgalMesh->surface.edges())
        {
          boost::put(featureEdges, e, false);
        }
      }

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

      MarkVerticesNearAnchorPoints(sm, vertexConstrained, smoothAnchorPoints, smoothAnchorTolSq);

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

  if (this->FeatureMaskEnabled)
  {
    std::vector<char> remeshedOutputMask;
    if (ComputeFeatureFaceMask(output, this->FeatureMaskArrayName, this->FeatureMaskThreshold,
          this->FeatureMaskAllScalars, remeshedOutputMask))
    {
      ExtractMaskedFacesPolyData(output, remeshedOutputMask, outputMaskPatchRemeshed);
    }
    else
    {
      vtkWarningMacro("Feature mask: could not evaluate threshold on port 0 output; "
                      "mask patch (remeshed) output port is empty.");
      outputMaskPatchRemeshed->Initialize();
    }
  }
  else
  {
    outputMaskPatchRemeshed->Initialize();
  }

  if (this->DetectFeatureEdges)
  {
    bool linesFromInputMaskPatch = false;
    if (this->FeatureMaskEnabled && inputFeatureMaskOk && outputMaskPatch->GetNumberOfCells() > 0)
    {
      try
      {
        vtkCGALHelper::Vespa_surface cgalPort2;
        if (vtkCGALHelper::toCGAL(outputMaskPatch, &cgalPort2, nullptr))
        {
          auto featPort2 = get(CGAL::edge_is_feature, cgalPort2.surface);
          DetectSharpEdgesWithFilter(
            cgalPort2.surface, this->ProtectAngle, this->SharpFeatureSideFilter, featPort2);
          FillFeaturePolyDataSharpLinesOnly(cgalPort2.surface, featPort2, outputFeatures);
          linesFromInputMaskPatch = true;
        }
        else
        {
          vtkWarningMacro("Could not build a CGAL surface from the input mask patch (port 2); "
                          "port 1 lines will use the full remeshed surface.");
        }
      }
      catch (const std::exception& e)
      {
        vtkWarningMacro("Sharp feature lines on input mask patch failed: "
          << e.what() << " Falling back to full remeshed surface for lines.");
      }
    }

    if (!linesFromInputMaskPatch)
    {
      CGAL_Surface& sm = cgalMesh->surface;
      auto featOutMap = get(CGAL::edge_is_feature, sm);
      DetectSharpEdgesWithFilter(sm, this->ProtectAngle, this->SharpFeatureSideFilter, featOutMap);
      if (this->FeatureMaskEnabled)
      {
        std::vector<char> outMask;
        const bool outOk = ComputeFeatureFaceMask(output, this->FeatureMaskArrayName,
          this->FeatureMaskThreshold, this->FeatureMaskAllScalars, outMask);
        if (!outOk)
        {
          vtkWarningMacro("Feature mask is enabled but the array could not be evaluated on the "
                          "remeshed surface (e.g. missing data after interpolation); port 1 lines "
                          "use unmasked sharp features on the full output.");
        }
        else
        {
          std::vector<vtkIdType> faceIdxToVtk;
          BuildCgalFaceIndexToVtkCell(sm, faceIdxToVtk);
          ApplyFeatureRegionMaskToSharpEdges(sm, featOutMap, outMask, faceIdxToVtk);
        }
      }
      FillFeaturePolyDataSharpLinesOnly(sm, featOutMap, outputFeatures);
    }

    if (this->ShapeSmoothingIterations > 0)
    {
      if (this->FeatureMaskEnabled && outputMaskPatchRemeshed->GetNumberOfCells() > 0)
      {
        try
        {
          vtkCGALHelper::Vespa_surface cgalPort3;
          if (vtkCGALHelper::toCGAL(outputMaskPatchRemeshed, &cgalPort3, nullptr))
          {
            auto featPort3 = get(CGAL::edge_is_feature, cgalPort3.surface);
            DetectSharpEdgesWithFilter(
              cgalPort3.surface, this->ProtectAngle, this->SharpFeatureSideFilter, featPort3);
            AppendSharpFeatureVertsToPolyData(outputFeatures, cgalPort3.surface, featPort3);
          }
        }
        catch (const std::exception& e)
        {
          vtkWarningMacro(
            "Sharp feature points on remeshed mask patch (port 3) failed: " << e.what());
        }
      }
      else if (!this->FeatureMaskEnabled)
      {
        CGAL_Surface& sm = cgalMesh->surface;
        auto featFull = get(CGAL::edge_is_feature, sm);
        DetectSharpEdgesWithFilter(sm, this->ProtectAngle, this->SharpFeatureSideFilter, featFull);
        AppendSharpFeatureVertsToPolyData(outputFeatures, sm, featFull);
      }
    }
    else
    {
      vtkNew<vtkCellArray> emptyVerts;
      outputFeatures->SetVerts(emptyVerts);
    }
  }
  else
  {
    outputFeatures->Initialize();
  }

  return 1;
}
