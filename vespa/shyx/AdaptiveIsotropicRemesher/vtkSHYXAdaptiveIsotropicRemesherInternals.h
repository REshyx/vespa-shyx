#ifndef vtkSHYXAdaptiveIsotropicRemesherInternals_h
#define vtkSHYXAdaptiveIsotropicRemesherInternals_h

#include "vtkCGALHelper.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkObject.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSetGet.h>

#include <CGAL/Kernel/global_functions.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vespa_shyx_air_remesh_internals
{
namespace pmp_int = CGAL::Polygon_mesh_processing;

inline double TupleMagnitude(vtkDataArray* arr, vtkIdType tupleIdx)
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

inline bool ComputeFeatureFaceMask(vtkPolyData* pd, const char* arrayName, double threshold, bool allScalars,
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

/**
 * Fills @a selected with mesh cells whose associated scalar sample lies in the closed interval
 * [@a rangeMin, @a rangeMax] (tuple magnitude, same as feature mask). Array resolution matches
 * ComputeFeatureFaceMask: cell data preferred when both point and cell arrays share the same name;
 * for point-centered data, @a allScalars chooses whether every corner must fall in-range vs any corner.
 * @return false if the array cannot be resolved, the name is empty, or rangeMin > rangeMax (selected is cleared).
 */
inline bool CollectCellsByScalarValueRange(vtkPolyData* pd, const char* arrayName, double rangeMin,
  double rangeMax, bool allScalars, std::set<vtkIdType>& selected)
{
  selected.clear();
  if (!pd || !arrayName || arrayName[0] == '\0' || rangeMin > rangeMax)
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

  auto inRange = [&](vtkIdType tupleIdx) -> bool {
    const double m = TupleMagnitude(arrOnCell, tupleIdx);
    return (m >= rangeMin && m <= rangeMax);
  };

  if (!usePointCorners)
  {
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (inRange(cid))
      {
        selected.insert(cid);
      }
    }
    return true;
  }

  vtkIdType nptsCorner;
  const vtkIdType* pids;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    pd->GetCellPoints(cid, nptsCorner, pids);
    bool anyPass = false;
    bool allPass = (nptsCorner > 0);
    for (vtkIdType k = 0; k < nptsCorner; ++k)
    {
      const bool pass = inRange(pids[k]);
      anyPass = anyPass || pass;
      allPass = allPass && pass;
    }
    const bool cellPass = allScalars ? allPass : anyPass;
    if (cellPass)
    {
      selected.insert(cid);
    }
  }
  return true;
}

inline void ExtractMaskedFacesPolyData(vtkPolyData* inMesh, const std::vector<char>& keepMask, vtkPolyData* out)
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

inline void BuildCgalFaceIndexToVtkCell(const CGAL_Surface& sm, std::vector<vtkIdType>& faceIdxToVtkCell)
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
inline void MarkFeatureEdgeByVertices(CGAL_Surface& sm, EdgeBoolMap featureEdges,
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

inline void BuildEdgeCells(vtkPolyData* pd, EdgeCellMap& edgeCells)
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
inline void ForEachSelectionBoundaryEdge(
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
inline void AddSelectionBoundaryUsingVtkTopology(vtkObject* logger, vtkPolyData* pd, CGAL_Surface& sm,
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

inline void CollectSelectionBoundaryWorldPoints(vtkPolyData* pd, const std::set<vtkIdType>& selected,
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
inline void LiftPatchSharpFeaturesToFullMesh(vtkObject* logger, CGAL_Surface& fullSm, EdgeBoolMapFull fullFeatures,
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
inline void AddPatchBoundaryFeatureEdges(const CGAL_Surface& sm, EdgeBoolMap featureEdges,
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
inline void ApplyFeatureRegionMaskToSharpEdges(const CGAL_Surface& sm, EdgeBoolMap featureEdges,
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

inline void CollectCellsFromExtracted(vtkPolyData* mesh, vtkDataSet* extracted, std::set<vtkIdType>& selected)
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
inline void ApplySharpFeatureSideFilter(CGAL_Surface& sm, EdgeBoolMap featureEdges, int mode)
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
inline void DetectSharpEdgesWithFilter(
  CGAL_Surface& sm, double protectAngleDeg, int sharpSideMode, EdgeBoolMap featureEdges)
{
  pmp_int::detect_sharp_edges(sm, protectAngleDeg, featureEdges);
  ApplySharpFeatureSideFilter(sm, featureEdges, sharpSideMode);
}

inline void CollectPatchSharpFeatureWorldPoints(vtkPolyData* maskPatch, double protectAngle, int sharpSideFilter,
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
inline void MarkVerticesNearAnchorPoints(CGAL_Surface& sm, VertBoolMap vertConstrained,
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
inline void FillFeaturePolyDataSharpLinesOnly(const CGAL_Surface& sm, const EdgeBoolMap& featureEdges, vtkPolyData* out)
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
inline void AppendSharpFeatureVertsToPolyData(vtkPolyData* out, const CGAL_Surface& sm, const EdgeBoolMap& featureEdges)
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

} // namespace vespa_shyx_air_remesh_internals

#endif
