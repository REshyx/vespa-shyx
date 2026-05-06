#ifndef vtkSHYXAdaptiveIsotropicRemesherInternals_h
#define vtkSHYXAdaptiveIsotropicRemesherInternals_h

#include "vtkCGALHelper.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkObject.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSetGet.h>

#include <CGAL/Kernel/global_functions.h>
#include <CGAL/Kernel_traits.h>
#include <CGAL/Origin.h>
#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include "custom_interpolated_corrected_curvatures.h"
#include <CGAL/boost/graph/Face_filtered_graph.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/boost/graph/selection.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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

/**
 * Upper bound on CGAL face property slots keyed by \c Face_index::idx().
 * Prefer this over number_of_faces() when indexing std::vectors by idx(): Euler ops can leave
 * holes in idx space until garbage collection so max(idx)+1 may exceed the alive face count.
 */
inline std::size_t CgalFaceMaskSlotUpperBound(const CGAL_Surface& mesh)
{
  std::size_t ub = 0;
  for (CGAL_Surface::Face_index f : mesh.faces())
  {
    ub = std::max(ub, static_cast<std::size_t>(f.idx()) + 1u);
  }
  return ub;
}

/**
 * For meshes built via \c vtkCGALHelper::toCGAL(input, ..., vtkCellToCgalFace), map each used
 * face slot to the VTK input cell id (\c cid) stored in vtkCellToCgalFace[\c cid]. Unused slots -1.
 * Use with \c ApplyFeatureRegionMaskToSharpEdges / \c BuildCgalFaceMaskFromVtkCells when the VTK
 * mask is indexed by that input PolyData cell id — not CGAL faces() visitation order alone.
 */
inline void BuildCgalFaceSlotToInputVtkCellId(const CGAL_Surface& mesh,
  const std::vector<Graph_Faces>& vtkCellToCgalFace, std::vector<vtkIdType>& faceSlotToVtkCell)
{
  const std::size_t slotUb = CgalFaceMaskSlotUpperBound(mesh);
  faceSlotToVtkCell.assign(slotUb, static_cast<vtkIdType>(-1));
  for (vtkIdType cid = 0; cid < static_cast<vtkIdType>(vtkCellToCgalFace.size()); ++cid)
  {
    const Graph_Faces f = vtkCellToCgalFace[static_cast<size_t>(cid)];
    if (!mesh.is_valid(f))
    {
      continue;
    }
    const std::size_t fi = static_cast<std::size_t>(f.idx());
    if (fi < faceSlotToVtkCell.size())
    {
      faceSlotToVtkCell[fi] = cid;
    }
  }
}

/**
 * Map each face slot \c f.idx() to VTK cell id in \c CGAL::faces(\a sm) visitation order
 * (same order as \c vtkCGALHelper::toVTK on that mesh: VTK cell id 0,1,…).
 */
inline void BuildCgalFaceIndexToVtkCell(const CGAL_Surface& sm, std::vector<vtkIdType>& faceIdxToVtkCell)
{
  const std::size_t slotUb = CgalFaceMaskSlotUpperBound(sm);
  faceIdxToVtkCell.assign(slotUb, static_cast<vtkIdType>(-1));
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

/** Per-face mask indexed by CGAL \c Face_index::idx(): \c vtkCellMask[K] at VTK cell id K from \a faceIdxToVtkCell. */
inline void BuildCgalFaceMaskFromVtkCells(const CGAL_Surface& mesh,
  const std::vector<char>& vtkCellMask, const std::vector<vtkIdType>& faceIdxToVtkCell,
  std::vector<char>& outCgalFaceMask)
{
  const std::size_t slotUb = CgalFaceMaskSlotUpperBound(mesh);
  outCgalFaceMask.assign(slotUb, 0);
  for (CGAL_Surface::Face_index f : mesh.faces())
  {
    const std::size_t fi = static_cast<std::size_t>(f.idx());
    if (fi >= faceIdxToVtkCell.size() || fi >= outCgalFaceMask.size())
    {
      continue;
    }
    const vtkIdType vtkC = faceIdxToVtkCell[fi];
    if (vtkC >= 0 && static_cast<size_t>(vtkC) < vtkCellMask.size())
    {
      outCgalFaceMask[fi] = vtkCellMask[static_cast<size_t>(vtkC)];
    }
  }
}

/**
 * Vertex normals for CGAL interpolated_corrected_curvatures on @a mesh
 * (`v:vespa_icc_normal`).
 *
 * 1) `compute_vertex_normals` — area-weighted blend of **all** incident triangle normals (CGAL
 *    default; no angle-based crease splitting).
 * 2) When @a faceInMaskByFaceIdx is omitted or empty after (1): if leftover dual maps (`f:vespa_icc_in_mask`,
 *    `v:vespa_icc_n_*`) exist from a **previous** masked call on this mesh, they are reset — all faces
 *    marked non-mask and both dual vertex normals copied from (1) — so ICC does not keep using stale
 *    mask topology after remesh.
 * 3) If @a faceInMaskByFaceIdx is set: writes `f:vespa_icc_in_mask` and **dual** vertex normals for ICC:
 *    `v:vespa_icc_n_mask` / `v:vespa_icc_n_nonmask` — each is the normalized sum of incident face normals
 *    restricted to masked / non-mask faces respectively; if that partial sum is degenerate, falls back
 *    to (1). Per-triangle ICC assembly picks corners via mask membership so mask interiors are not
 *    polluted by normals from the opposite side at region boundaries.
 *
 * @param faceInMaskByFaceIdx optional, indexed by `Face_index::idx()`, length ≥ face count.
 *    Pass nullptr (or empty) only when no mask-driven ICC is intended; see (2).
 */
inline void PrepareIccVertexNormalsForAdaptiveSizing(CGAL_Surface& mesh,
  const std::vector<char>* faceInMaskByFaceIdx)
{
  using Vertex_index = CGAL_Surface::Vertex_index;
  using Face_index = CGAL_Surface::Face_index;
  using Vector_3 = CGAL_Kernel::Vector_3;

  auto vn_pair =
    mesh.template add_property_map<Vertex_index, Vector_3>("v:vespa_icc_normal", Vector_3(0, 0, 0));
  const auto vn_map = vn_pair.first;

  // CGAL 6.x: pass the property map as the 2nd argument (not parameters::vertex_normal_map alone).
  pmp_int::compute_vertex_normals(mesh, vn_map);

  if (faceInMaskByFaceIdx == nullptr || faceInMaskByFaceIdx->empty())
  {
    // A prior masked PrepareIcc leaves `v:vespa_icc_n_*` + `f:vespa_icc_in_mask`; after topology
    // changes (multi-iteration remesh) VTK cell indices no longer remap here, but CGAL still exposes
    // those maps. interpolated_corrected_curvatures loads them via VespaIccTryLoadDualNormalBundle
    // and then uses incompatible mask/normal data → bogus principals and NaN uncapped targets.
    // Neutralise to "no masking": all-face non-mask corners use vn_nonmask == vn_primary; vn_mask ==
    // vn_primary too (unused when every face is non-mask).
    const auto stale_fm = mesh.template property_map<Face_index, bool>("f:vespa_icc_in_mask");
    if (stale_fm.has_value())
    {
      for (Face_index f : mesh.faces())
      {
        put(*stale_fm, f, false);
      }
    }
    const auto stale_vm = mesh.template property_map<Vertex_index, Vector_3>("v:vespa_icc_n_mask");
    const auto stale_vnm = mesh.template property_map<Vertex_index, Vector_3>("v:vespa_icc_n_nonmask");
    if (stale_vm.has_value())
    {
      for (Vertex_index v : mesh.vertices())
      {
        put(*stale_vm, v, get(vn_map, v));
      }
    }
    if (stale_vnm.has_value())
    {
      for (Vertex_index v : mesh.vertices())
      {
        put(*stale_vnm, v, get(vn_map, v));
      }
    }
    return;
  }

  const std::size_t maskSz = faceInMaskByFaceIdx->size();

  auto fm_pair = mesh.template add_property_map<Face_index, bool>("f:vespa_icc_in_mask", false);
  const auto fm_map = fm_pair.first;
  for (Face_index f : mesh.faces())
  {
    const std::size_t fi = static_cast<std::size_t>(f.idx());
    put(fm_map, f, fi < maskSz && (*faceInMaskByFaceIdx)[fi] != 0);
  }

  const auto vn_mask_pair =
    mesh.template add_property_map<Vertex_index, Vector_3>("v:vespa_icc_n_mask", Vector_3(0, 0, 0));
  const auto vn_nonmask_pair =
    mesh.template add_property_map<Vertex_index, Vector_3>("v:vespa_icc_n_nonmask", Vector_3(0, 0, 0));
  const auto vn_mask_map = vn_mask_pair.first;
  const auto vn_nonmask_map = vn_nonmask_pair.first;

  for (Vertex_index v : mesh.vertices())
  {
    Vector_3 sumMask(0, 0, 0);
    Vector_3 sumNonMask(0, 0, 0);
    for (CGAL_Surface::Halfedge_index h : CGAL::halfedges_around_target(v, mesh))
    {
      const Face_index f = mesh.face(h);
      if (f == CGAL_Surface::null_face())
      {
        continue;
      }
      const Vector_3 fn = pmp_int::compute_face_normal(f, mesh);
      if (get(fm_map, f))
      {
        sumMask = sumMask + fn;
      }
      else
      {
        sumNonMask = sumNonMask + fn;
      }
    }

    const Vector_3 global_n = get(vn_map, v);
    auto normalizeOrFallback = [&](const Vector_3& partial) -> Vector_3 {
      const double sl = CGAL::to_double(partial.squared_length());
      if (sl > 1e-30)
      {
        return partial / std::sqrt(sl);
      }
      return global_n;
    };

    put(vn_mask_map, v, normalizeOrFallback(sumMask));
    put(vn_nonmask_map, v, normalizeOrFallback(sumNonMask));
  }
}

/**
 * Export per-vertex **non-mask-side** ICC normals (`v:vespa_icc_n_nonmask`: area blend of incident
 * face normals on faces outside the feature mask, unitized; falls back to `v:vespa_icc_normal` when
 * the dual bundle was never built — whole surface counts as non-mask). VTK point order must match
 * `vtkCGALHelper::toVTK` / `toCGAL`, i.e. the same order as `CGAL::vertices(mesh)`, **not** raw
 * `Vertex_index::idx()` (they can diverge after local remesh topology changes).
 */
inline void AddVespaIccNonMaskNormalsAsPointArray(
  const CGAL_Surface& mesh, vtkPolyData* pd, vtkObject* loggerOrNull = nullptr)
{
  if (!pd)
  {
    return;
  }
  if (static_cast<std::size_t>(pd->GetNumberOfPoints()) != mesh.number_of_vertices())
  {
    if (loggerOrNull != nullptr)
    {
      vtkWarningWithObjectMacro(loggerOrNull,
        << "Skipping VespaIccNonMaskVertexNormal export: VTK point count "
        << pd->GetNumberOfPoints() << " differs from CGAL vertex count "
        << mesh.number_of_vertices() << '.');
    }
    return;
  }
  using Vertex_index = CGAL_Surface::Vertex_index;
  const auto pmap_nm =
    mesh.template property_map<Vertex_index, CGAL_Kernel::Vector_3>("v:vespa_icc_n_nonmask");
  const auto pmap_gn =
    mesh.template property_map<Vertex_index, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");

  auto emitFrom = [&](const CGAL_Surface::Property_map<Vertex_index, CGAL_Kernel::Vector_3>& m) {
    vtkNew<vtkDoubleArray> ar;
    ar->SetName("VespaIccNonMaskVertexNormal");
    ar->SetNumberOfComponents(3);
    vtkIdType nPts = pd->GetNumberOfPoints();
    ar->SetNumberOfTuples(nPts);
    vtkIdType pid = 0;
    for (Vertex_index vtex : CGAL::vertices(mesh))
    {
      const CGAL_Kernel::Vector_3 nv = boost::get(m, vtex);
      ar->SetTuple3(
        pid, CGAL::to_double(nv.x()), CGAL::to_double(nv.y()), CGAL::to_double(nv.z()));
      ++pid;
    }
    if (pid != nPts && loggerOrNull != nullptr)
    {
      vtkWarningWithObjectMacro(loggerOrNull,
        << "VespaIccNonMaskVertexNormal: CGAL::vertices count " << pid << " != VTK tuples " << nPts
        << ".");
    }
    pd->GetPointData()->AddArray(ar);
  };

  if (pmap_nm.has_value())
  {
    emitFrom(pmap_nm.value());
    return;
  }
  if (pmap_gn.has_value())
  {
    emitFrom(pmap_gn.value());
  }
}

/**
 * Per-triangle ICC interpolated corrected measures mu^(0), mu^(1), mu^(2) (CGAL closed forms).
 * Uses halfedge order on @a f. When @a dual_bundle is active (mask maps from PrepareIccVertexNormalsForAdaptiveSizing),
 * each corner normal matches CGAL ICC per-face assembly; otherwise @a vn_map is used for all three corners.
 */
inline void ComputeIccClosedFormMeasuresForFace(const CGAL_Surface& mesh, CGAL_Surface::Face_index f,
  const vespa_shyx::VespaIccDualNormalBundle* dual_bundle,
  const CGAL_Surface::Property_map<CGAL_Surface::Vertex_index, CGAL_Kernel::Vector_3>& vn_map,
  double& mu0, double& mu1, double& mu2)
{
  using Vertex_index = CGAL_Surface::Vertex_index;
  using Vector_3 = CGAL_Kernel::Vector_3;
  using Point_3 = CGAL_Kernel::Point_3;

  const CGAL_Surface::Halfedge_index h = mesh.halfedge(f);
  const Vertex_index vi = mesh.source(h);
  const Vertex_index vj = mesh.target(h);
  const Vertex_index vk = mesh.target(mesh.next(h));

  const Point_3 pi = mesh.point(vi);
  const Point_3 pj = mesh.point(vj);
  const Point_3 pk = mesh.point(vk);

  const Vector_3 ui = vespa_shyx::VespaIccCornerNormalForFace(dual_bundle, vn_map, f, vi);
  const Vector_3 uj = vespa_shyx::VespaIccCornerNormalForFace(dual_bundle, vn_map, f, vj);
  const Vector_3 uk = vespa_shyx::VespaIccCornerNormalForFace(dual_bundle, vn_map, f, vk);

  const Vector_3 ubar = (ui + uj + uk) * (1.0 / 3.0);

  const Vector_3 Xi = pi - CGAL::ORIGIN;
  const Vector_3 Xj = pj - CGAL::ORIGIN;
  const Vector_3 Xk = pk - CGAL::ORIGIN;

  const Vector_3 cross_jk = CGAL::cross_product(pj - pi, pk - pi);
  mu0 = 0.5 * CGAL::to_double(CGAL::scalar_product(ubar, cross_jk));

  const Vector_3 tmu1 =
    CGAL::cross_product(uk - uj, Xi) + CGAL::cross_product(ui - uk, Xj) + CGAL::cross_product(uj - ui, Xk);
  mu1 = 0.5 * CGAL::to_double(CGAL::scalar_product(ubar, tmu1));

  mu2 = 0.5 * CGAL::to_double(CGAL::scalar_product(ui, CGAL::cross_product(uj, uk)));
}

namespace detail
{
template <typename FaceGraph>
inline void RunIccFillVertexCurvatureScalars(FaceGraph& fg, std::size_t num_vertices,
  const CGAL_Surface::Property_map<CGAL_Surface::Vertex_index, CGAL_Kernel::Vector_3>* vn_map,
  const CGAL_Surface* vespa_dual_normal_property_host,
  std::vector<double>& out_kmin, std::vector<double>& out_kmax,
  std::vector<double>& out_kmean, std::vector<double>& out_kgauss)
{
  namespace pmp_sf = CGAL::Polygon_mesh_processing;
  using Vertex_index = CGAL_Surface::Vertex_index;
  using Point_3      = CGAL_Surface::Point;
  using Kernel       = typename CGAL::Kernel_traits<Point_3>::Kernel;
  using Principal    = vespa_shyx::Custom_principal_curvatures_and_directions<Kernel>;
  using CTag         = CGAL::dynamic_vertex_property_t<Principal>;

  auto curv_map = get(CTag(), fg);
  if (vn_map != nullptr)
  {
    vespa_shyx::custom_interpolated_corrected_curvatures(fg,
      pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map)
        .vertex_normal_map(*vn_map),
      vespa_dual_normal_property_host);
  }
  else
  {
    vespa_shyx::custom_interpolated_corrected_curvatures(
      fg, pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map),
      vespa_dual_normal_property_host);
  }

  for (Vertex_index v : vertices(fg))
  {
    const std::size_t i = static_cast<std::size_t>(v.idx());
    if (i >= num_vertices)
    {
      continue;
    }
    const Principal vc = get(curv_map, v);
    const double km = CGAL::to_double(vc.min_curvature);
    const double kM = CGAL::to_double(vc.max_curvature);
    out_kmin[i]    = km;
    out_kmax[i]    = kM;
    out_kmean[i]   = 0.5 * (km + kM);
    out_kgauss[i]  = km * kM;
  }
}
} // namespace detail

/**
 * Vertex ICC principal curvatures (custom_interpolated_corrected_curvatures fork), using optional
 * `v:vespa_icc_normal` when @a vn_map is non-null; dual-region maps on @a mesh drive per-face corner
 * normals when present, and per-vertex aggregates ignore masked faces. Domain matches
 * FeatureAwareAdaptiveSizingField: whole mesh unless
 * @a patch_domain with non-empty @a patchFaces (expanded 1-ring patch). Vertices outside the
 * Face_filtered_graph patch remain NaN.
 */
inline void ComputeIccVertexCurvatureScalars(CGAL_Surface& mesh, bool patch_domain,
  const std::vector<CGAL_Surface::Face_index>& patchFaces,
  const CGAL_Surface::Property_map<CGAL_Surface::Vertex_index, CGAL_Kernel::Vector_3>* vn_map,
  std::vector<double>& out_kmin, std::vector<double>& out_kmax,
  std::vector<double>& out_kmean, std::vector<double>& out_kgauss)
{
  using Face_index = CGAL_Surface::Face_index;
  const std::size_t nv = mesh.number_of_vertices();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  out_kmin.assign(nv, nan);
  out_kmax.assign(nv, nan);
  out_kmean.assign(nv, nan);
  out_kgauss.assign(nv, nan);

  if (!patch_domain || patchFaces.empty() || patchFaces.size() == mesh.number_of_faces())
  {
    detail::RunIccFillVertexCurvatureScalars(mesh, nv, vn_map, &mesh, out_kmin, out_kmax, out_kmean,
      out_kgauss);
    return;
  }

  std::vector<Face_index> sel(patchFaces.begin(), patchFaces.end());
  auto is_sel = get(CGAL::dynamic_face_property_t<bool>(), mesh);
  for (Face_index f : faces(mesh))
  {
    put(is_sel, f, false);
  }
  for (Face_index f : patchFaces)
  {
    put(is_sel, f, true);
  }
  CGAL::expand_face_selection(sel, mesh, 1, is_sel, std::back_inserter(sel));
  CGAL::Face_filtered_graph<CGAL_Surface> ffg(mesh, sel);
  detail::RunIccFillVertexCurvatureScalars(ffg, nv, vn_map, &mesh, out_kmin, out_kmax, out_kmean,
    out_kgauss);
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

} // namespace vespa_shyx_air_remesh_internals

#endif
