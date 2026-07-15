#include "vtkSHYXSelectionFillAlphaReunionFilter.h"

#include "vtkCGALAlphaWrapping.h"
#include "vtkCGALHelper.h"
#include "vtkSHYXHoleFillFilter.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractSelection.h>
#include <vtkFloatArray.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <vtkTriangleFilter.h>

#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Polygon_mesh_processing/smooth_shape.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/iterator.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace pmp = CGAL::Polygon_mesh_processing;

vtkStandardNewMacro(vtkSHYXSelectionFillAlphaReunionFilter);

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

void BuildPolyDataWithoutCells(vtkPolyData* inMesh, const std::set<vtkIdType>& selected, vtkPolyData* out)
{
  out->Initialize();
  if (!inMesh)
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
    if (selected.count(cid) != 0u)
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
    if (selected.count(cid) != 0u)
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

    vtkCellArray* target = nullptr;
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
      case VTK_TRIANGLE:
      case VTK_QUAD:
      case VTK_POLYGON:
      case VTK_PIXEL:
        target = newPolys;
        break;
      case VTK_TRIANGLE_STRIP:
        target = newStrips;
        break;
      default:
        target = newPolys;
        break;
    }
    if (target)
    {
      target->InsertNextCell(remapped);
      keptOrig.push_back(cid);
    }
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

void BuildPolyDataWithOnlySelectedCells(
  vtkPolyData* inMesh, const std::set<vtkIdType>& selected, vtkPolyData* out)
{
  out->Initialize();
  if (!inMesh)
  {
    return;
  }

  const vtkIdType nPts = inMesh->GetNumberOfPoints();
  const vtkIdType nCells = inMesh->GetNumberOfCells();
  if (nCells == 0)
  {
    return;
  }

  std::vector<char> usedPt(static_cast<size_t>(nPts), 0);
  vtkIdType npts;
  const vtkIdType* pids;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (selected.count(cid) == 0u)
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
    if (selected.count(cid) == 0u)
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

    vtkCellArray* target = nullptr;
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
      case VTK_TRIANGLE:
      case VTK_QUAD:
      case VTK_POLYGON:
      case VTK_PIXEL:
        target = newPolys;
        break;
      case VTK_TRIANGLE_STRIP:
        target = newStrips;
        break;
      default:
        target = newPolys;
        break;
    }
    if (target)
    {
      target->InsertNextCell(remapped);
      keptOrig.push_back(cid);
    }
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
 * Corefinement visitor: record which output faces were imported from the Alpha-Wrap mesh
 * (tm1). Coplanar overlap faces are marked as AW so they enter the cleanup patch.
 * Visitor is CopyConstructible (CGAL requirement) — shared_ptr keeps origin storage shared.
 */
struct FaceOriginVisitor : public pmp::Corefinement::Default_visitor<CGAL_Surface>
{
  const CGAL_Surface* AwMesh = nullptr;
  std::shared_ptr<std::vector<char>> OriginByFaceIdx;

  void after_face_copy(face_descriptor /*f_src*/, const CGAL_Surface& tm_src,
    face_descriptor f_tgt, const CGAL_Surface& /*tm_tgt*/)
  {
    if (!this->OriginByFaceIdx)
    {
      return;
    }
    const char origin = (this->AwMesh != nullptr && &tm_src == this->AwMesh) ? 1 : 0;
    const std::size_t i = static_cast<std::size_t>(f_tgt.idx());
    if (this->OriginByFaceIdx->size() <= i)
    {
      this->OriginByFaceIdx->resize(i + 1u, 0);
    }
    (*this->OriginByFaceIdx)[i] = origin;
  }

  void subface_of_coplanar_faces_intersection(face_descriptor f, const CGAL_Surface& /*tm*/)
  {
    if (!this->OriginByFaceIdx)
    {
      return;
    }
    const std::size_t i = static_cast<std::size_t>(f.idx());
    if (this->OriginByFaceIdx->size() <= i)
    {
      this->OriginByFaceIdx->resize(i + 1u, 0);
    }
    (*this->OriginByFaceIdx)[i] = 1;
  }
};

/**
 * CGAL union of @a awSide (tm1) and @a unselectedSide (tm2) with precise face-origin tracking.
 * @a awFaceMask is filled in VTK cell order (same as vtkCGALHelper::toVTK face iteration): 1 = from AW.
 */
bool CorefineUnionWithFaceOrigin(vtkPolyData* awSide, vtkPolyData* unselectedSide, bool orientWhenNeeded,
  bool throwOnSelfIntersection, vtkPolyData* outUnited, std::vector<char>& awFaceMask, std::string& error)
{
  error.clear();
  awFaceMask.clear();
  if (!awSide || !unselectedSide || !outUnited)
  {
    error = "null input for provenance union.";
    return false;
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalAW =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalUn =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalOut =
    std::make_unique<vtkCGALHelper::Vespa_surface>();

  if (!vtkCGALHelper::toCGAL(awSide, cgalAW.get()) || !vtkCGALHelper::toCGAL(unselectedSide, cgalUn.get()))
  {
    error = "VTK to CGAL conversion failed before union.";
    return false;
  }

  try
  {
    if (orientWhenNeeded)
    {
      if (!pmp::does_bound_a_volume(cgalAW->surface))
      {
        pmp::orient_to_bound_a_volume(cgalAW->surface);
      }
      if (!pmp::does_bound_a_volume(cgalUn->surface))
      {
        pmp::orient_to_bound_a_volume(cgalUn->surface);
      }
    }

    FaceOriginVisitor visitor;
    visitor.AwMesh = &cgalAW->surface;
    visitor.OriginByFaceIdx = std::make_shared<std::vector<char>>();

    const bool ok = pmp::corefine_and_compute_union(cgalAW->surface, cgalUn->surface, cgalOut->surface,
      pmp::parameters::throw_on_self_intersection(throwOnSelfIntersection).visitor(visitor),
      pmp::parameters::all_default());

    if (!ok)
    {
      error = "corefine_and_compute_union returned false.";
      return false;
    }

    if (!vtkCGALHelper::toVTK(cgalOut.get(), outUnited))
    {
      error = "CGAL to VTK conversion failed after union.";
      return false;
    }

    // Face iteration order matches vtkCGALHelper::toVTK.
    awFaceMask.reserve(static_cast<size_t>(outUnited->GetNumberOfCells()));
    const auto& origin = *visitor.OriginByFaceIdx;
    for (Graph_Faces f : faces(cgalOut->surface))
    {
      const std::size_t i = static_cast<std::size_t>(f.idx());
      const char fromAW = (i < origin.size() && origin[i]) ? 1 : 0;
      awFaceMask.push_back(fromAW);
    }

    if (static_cast<vtkIdType>(awFaceMask.size()) != outUnited->GetNumberOfCells())
    {
      error = "face-origin mask size mismatch after union.";
      return false;
    }
  }
  catch (const pmp::Corefinement::Self_intersection_exception&)
  {
    error = "self-intersection during boolean (ThrowOnSelfIntersection is on).";
    return false;
  }
  catch (const std::exception& e)
  {
    error = std::string("CGAL union exception: ") + e.what();
    return false;
  }

  return true;
}

void DilateCellMask(vtkPolyData* mesh, std::vector<char>& mask, int layers)
{
  if (!mesh || layers <= 0 || mask.empty())
  {
    return;
  }

  const vtkIdType nCells = mesh->GetNumberOfCells();
  if (static_cast<vtkIdType>(mask.size()) != nCells)
  {
    return;
  }

  mesh->BuildLinks();
  vtkNew<vtkIdList> cellPts;
  vtkNew<vtkIdList> neighborCells;
  std::vector<char> next = mask;

  for (int layer = 0; layer < layers; ++layer)
  {
    next = mask;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!mask[static_cast<size_t>(cid)])
      {
        continue;
      }
      mesh->GetCellPoints(cid, cellPts);
      for (vtkIdType k = 0; k < cellPts->GetNumberOfIds(); ++k)
      {
        mesh->GetPointCells(cellPts->GetId(k), neighborCells);
        for (vtkIdType j = 0; j < neighborCells->GetNumberOfIds(); ++j)
        {
          const vtkIdType nid = neighborCells->GetId(j);
          if (nid >= 0 && nid < nCells)
          {
            next[static_cast<size_t>(nid)] = 1;
          }
        }
      }
    }
    mask.swap(next);
  }
}

double MeanEdgeLengthOfMaskedFaces(vtkPolyData* mesh, const std::vector<char>& mask)
{
  if (!mesh || mask.empty())
  {
    return 0.0;
  }

  const vtkIdType nCells = mesh->GetNumberOfCells();
  double sum = 0.0;
  vtkIdType count = 0;
  vtkIdType npts;
  const vtkIdType* pids;

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (cid >= static_cast<vtkIdType>(mask.size()) || !mask[static_cast<size_t>(cid)])
    {
      continue;
    }
    mesh->GetCellPoints(cid, npts, pids);
    if (npts < 2)
    {
      continue;
    }
    for (vtkIdType k = 0; k < npts; ++k)
    {
      double a[3], b[3];
      mesh->GetPoint(pids[k], a);
      mesh->GetPoint(pids[(k + 1) % npts], b);
      const double dx = a[0] - b[0];
      const double dy = a[1] - b[1];
      const double dz = a[2] - b[2];
      sum += std::sqrt(dx * dx + dy * dy + dz * dz);
      ++count;
    }
  }
  return (count > 0) ? (sum / static_cast<double>(count)) : 0.0;
}

void AttachBridgeMaskArray(vtkPolyData* mesh, const std::vector<char>& mask, const char* name)
{
  if (!mesh || !name || mask.empty())
  {
    return;
  }
  const vtkIdType nCells = mesh->GetNumberOfCells();
  if (static_cast<vtkIdType>(mask.size()) != nCells)
  {
    return;
  }

  vtkNew<vtkFloatArray> arr;
  arr->SetName(name);
  arr->SetNumberOfComponents(1);
  arr->SetNumberOfTuples(nCells);
  for (vtkIdType i = 0; i < nCells; ++i)
  {
    arr->SetValue(i, mask[static_cast<size_t>(i)] ? 1.f : 0.f);
  }
  mesh->GetCellData()->AddArray(arr);
  mesh->GetCellData()->SetActiveScalars(name);
}

/**
 * Local isotropic remesh (+ relaxation) and constrained angle/area smooth on a dilated AW patch.
 * Returns false on hard failure (caller may keep the pre-cleanup mesh).
 * If @a outMaskAfter is non-null, it is filled in VTK cell order with 1 = cleanup patch after remesh
 * (faces that were outside the input mask keep 0; remeshed/new faces in the patch are 1).
 */
bool LocalRemeshAndSmoothBridge(vtkPolyData* mesh, const std::vector<char>& mask,
  double targetEdgeLength, int remeshIterations, int remeshRelaxationSteps, int smoothIterations,
  double smoothTimeStep, vtkPolyData* out, std::string& error, std::vector<char>* outMaskAfter = nullptr)
{
  error.clear();
  if (outMaskAfter)
  {
    outMaskAfter->clear();
  }
  if (!mesh || !out || mask.empty())
  {
    error = "null mesh/mask for bridge cleanup.";
    return false;
  }

  std::size_t nMarked = 0;
  for (char c : mask)
  {
    nMarked += (c != 0) ? 1u : 0u;
  }
  if (nMarked == 0)
  {
    error = "bridge cleanup mask is empty.";
    return false;
  }

  if (targetEdgeLength <= 0.0)
  {
    error = "bridge target edge length must be positive.";
    return false;
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  std::vector<Graph_Faces> vtkCellToCgalFace;
  if (!vtkCGALHelper::toCGAL(mesh, cgalMesh.get(), &vtkCellToCgalFace))
  {
    error = "VTK to CGAL conversion failed for bridge cleanup.";
    return false;
  }

  CGAL_Surface& sm = cgalMesh->surface;
  std::vector<Graph_Faces> remeshFaces;
  remeshFaces.reserve(nMarked);

  std::size_t maxFaceIdx = 0;
  for (Graph_Faces f : sm.faces())
  {
    maxFaceIdx = std::max(maxFaceIdx, static_cast<std::size_t>(f.idx()));
  }
  std::vector<char> faceInPatch(maxFaceIdx + 1u, 0);

  for (vtkIdType cid = 0; cid < static_cast<vtkIdType>(mask.size()); ++cid)
  {
    if (!mask[static_cast<size_t>(cid)])
    {
      continue;
    }
    if (cid >= static_cast<vtkIdType>(vtkCellToCgalFace.size()))
    {
      continue;
    }
    const Graph_Faces fd = vtkCellToCgalFace[static_cast<size_t>(cid)];
    if (!sm.is_valid(fd))
    {
      continue;
    }
    remeshFaces.push_back(fd);
    const std::size_t fi = static_cast<std::size_t>(fd.idx());
    if (fi >= faceInPatch.size())
    {
      faceInPatch.resize(fi + 1u, 0);
    }
    faceInPatch[fi] = 1;
  }

  if (remeshFaces.empty())
  {
    error = "no remeshable CGAL faces in bridge cleanup mask.";
    return false;
  }

  // Surviving faces outside the cleanup patch stay tagged; remeshed/new faces get default false
  // → after remesh, mask = !outside.
  auto faceOutside =
    sm.add_property_map<Graph_Faces, bool>("f:vespa_bridge_outside", false).first;
  for (Graph_Faces f : sm.faces())
  {
    const std::size_t fi = static_cast<std::size_t>(f.idx());
    const bool inPatch = (fi < faceInPatch.size() && faceInPatch[fi] != 0);
    boost::put(faceOutside, f, !inPatch);
  }

  try
  {
    auto keepFixed =
      sm.add_property_map<Graph_Verts, bool>("v:vespa_bridge_keep_fixed", false).first;
    for (Graph_Verts v : sm.vertices())
    {
      bool hasRemesh = false;
      bool hasNonRemesh = false;
      const auto h0 = sm.halfedge(v);
      if (h0 == CGAL_Surface::null_halfedge())
      {
        boost::put(keepFixed, v, true);
        continue;
      }
      for (auto h : halfedges_around_target(h0, sm))
      {
        if (sm.is_border(h))
        {
          continue;
        }
        const Graph_Faces f = sm.face(h);
        if (!sm.is_valid(f))
        {
          continue;
        }
        const std::size_t fi = static_cast<std::size_t>(f.idx());
        if (fi < faceInPatch.size() && faceInPatch[fi])
        {
          hasRemesh = true;
        }
        else
        {
          hasNonRemesh = true;
        }
      }
      // Fix outside + patch-boundary vertices; free only strict patch interior.
      boost::put(keepFixed, v, !hasRemesh || hasNonRemesh);
    }

    // Only constrain the cleanup-patch boundary (and patch border edges). Do NOT run
    // detect_sharp_edges here — boolean seams often create artificial sharp dihedrals that
    // would otherwise be protected and survive remesh/smooth inside the patch.
    auto featureEdges = get(CGAL::edge_is_feature, sm);
    for (CGAL_Surface::Edge_index e : sm.edges())
    {
      boost::put(featureEdges, e, false);
    }
    for (CGAL_Surface::Edge_index e : sm.edges())
    {
      const auto h0 = sm.halfedge(e);
      const auto ho = sm.opposite(h0);
      const bool b0 = sm.is_border(h0);
      const bool b1 = sm.is_border(ho);
      if (!b0 && !b1)
      {
        const Graph_Faces fA = sm.face(h0);
        const Graph_Faces fB = sm.face(ho);
        const std::size_t ia = static_cast<std::size_t>(fA.idx());
        const std::size_t ib = static_cast<std::size_t>(fB.idx());
        const bool inA = (ia < faceInPatch.size() && faceInPatch[ia]);
        const bool inB = (ib < faceInPatch.size() && faceInPatch[ib]);
        if (inA != inB)
        {
          boost::put(featureEdges, e, true);
        }
      }
      else if (b0 != b1)
      {
        const Graph_Faces f = b0 ? sm.face(ho) : sm.face(h0);
        const std::size_t fi = static_cast<std::size_t>(f.idx());
        if (fi < faceInPatch.size() && faceInPatch[fi])
        {
          boost::put(featureEdges, e, true);
        }
      }
    }

    pmp::isotropic_remeshing(remeshFaces, targetEdgeLength, sm,
      pmp::parameters::number_of_iterations(static_cast<unsigned int>(remeshIterations))
        .number_of_relaxation_steps(static_cast<unsigned int>(remeshRelaxationSteps))
        .protect_constraints(true)
        .edge_is_constrained_map(featureEdges));

    if (smoothIterations > 0)
    {
      // Post-remesh shape smooth (MCF). angle_and_area mainly equalizes triangles and barely
      // changes the surface; smooth_shape moves along mean curvature so iterations are visible.
      // keepFixed already freezes outside + patch-boundary vertices.
      const double dt = (smoothTimeStep > 0.0) ? smoothTimeStep : 1e-3;
      pmp::smooth_shape(sm, dt,
        pmp::parameters::number_of_iterations(static_cast<unsigned int>(smoothIterations))
          .vertex_is_constrained_map(keepFixed)
          .do_scale(false));
    }
  }
  catch (const std::exception& e)
  {
    error = std::string("CGAL bridge cleanup failed: ") + e.what();
    return false;
  }

  if (!vtkCGALHelper::toVTK(cgalMesh.get(), out))
  {
    error = "CGAL to VTK conversion failed after bridge cleanup.";
    return false;
  }

  if (outMaskAfter)
  {
    outMaskAfter->clear();
    outMaskAfter->reserve(static_cast<size_t>(out->GetNumberOfCells()));
    for (Graph_Faces f : faces(sm))
    {
      // Outside faces kept the tag; remeshed/new patch faces default to false → mask 1.
      outMaskAfter->push_back(boost::get(faceOutside, f) ? 0 : 1);
    }
    if (static_cast<vtkIdType>(outMaskAfter->size()) != out->GetNumberOfCells())
    {
      outMaskAfter->clear();
      error = "post-remesh bridge mask size mismatch.";
      return false;
    }
  }
  return true;
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXSelectionFillAlphaReunionFilter::vtkSHYXSelectionFillAlphaReunionFilter()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
vtkSHYXSelectionFillAlphaReunionFilter::~vtkSHYXSelectionFillAlphaReunionFilter()
{
  this->SetSelectionCellArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionFillAlphaReunionFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionFillAlphaReunionFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
  os << indent << "FairingContinuity: " << this->FairingContinuity << "\n";
  os << indent << "AbsoluteThresholds: " << (this->AbsoluteThresholds ? "on" : "off") << "\n";
  os << indent << "Alpha: " << this->Alpha << "\n";
  os << indent << "Offset: " << this->Offset << "\n";
  os << indent << "SkipAlphaWrapping: " << (this->SkipAlphaWrapping ? "on" : "off") << "\n";
  os << indent << "ThrowOnSelfIntersection: " << (this->ThrowOnSelfIntersection ? "on" : "off")
     << "\n";
  os << indent << "OrientToBoundVolumeWhenNeeded: "
     << (this->OrientToBoundVolumeWhenNeeded ? "on" : "off") << "\n";
  os << indent << "EnableBridgeCleanup: " << (this->EnableBridgeCleanup ? "on" : "off") << "\n";
  os << indent << "BridgeDilateLayers: " << this->BridgeDilateLayers << "\n";
  os << indent << "BridgeTargetEdgeLength: " << this->BridgeTargetEdgeLength << "\n";
  os << indent << "BridgeRemeshIterations: " << this->BridgeRemeshIterations << "\n";
  os << indent << "BridgeRemeshRelaxationSteps: " << this->BridgeRemeshRelaxationSteps << "\n";
  os << indent << "BridgeSmoothIterations: " << this->BridgeSmoothIterations << "\n";
  os << indent << "BridgeSmoothTimeStep: " << this->BridgeSmoothTimeStep << "\n";
  os << indent << "ExportBridgeMask: " << (this->ExportBridgeMask ? "on" : "off") << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionFillAlphaReunionFilter::FillInputPortInformation(int port, vtkInformation* info)
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
int vtkSHYXSelectionFillAlphaReunionFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* mesh = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!mesh || !output)
  {
    return 0;
  }

  if (mesh->GetNumberOfCells() == 0)
  {
    vtkErrorMacro(<< "Empty input mesh.");
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
        extractSelection->SetInputData(0, mesh);
        extractSelection->SetInputData(1, inputSel);
        extractSelection->Update();
        vtkDataSet* extracted = vtkDataSet::SafeDownCast(extractSelection->GetOutputDataObject(0));
        if (extracted && (extracted->GetNumberOfCells() > 0 || extracted->GetNumberOfPoints() > 0))
        {
          CollectCellsFromExtracted(mesh, extracted, selected);
        }
      }
    }
  }

  if (selected.empty() && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0')
  {
    vtkDataArray* arr = mesh->GetCellData()->GetArray(this->SelectionCellArrayName);
    if (arr)
    {
      const vtkIdType nc = mesh->GetNumberOfCells();
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

  if (selected.empty())
  {
    vtkErrorMacro(<< "No valid cell selection. Provide a Selection (port 1) or a cell array via "
                     "SelectionCellArrayName.");
    return 0;
  }

  vtkNew<vtkPolyData> partSel;
  vtkNew<vtkPolyData> partUnsel;
  BuildPolyDataWithOnlySelectedCells(mesh, selected, partSel);
  BuildPolyDataWithoutCells(mesh, selected, partUnsel);

  if (partSel->GetNumberOfCells() == 0)
  {
    vtkErrorMacro(<< "Selected part has no cells after split.");
    return 0;
  }

  vtkNew<vtkPolyData> triSel;
  vtkNew<vtkPolyData> triUnsel;
  {
    vtkNew<vtkTriangleFilter> tSel;
    tSel->SetInputData(partSel);
    tSel->Update();
    triSel->ShallowCopy(tSel->GetOutput());
  }
  {
    vtkNew<vtkTriangleFilter> tUn;
    tUn->SetInputData(partUnsel);
    tUn->Update();
    triUnsel->ShallowCopy(tUn->GetOutput());
  }

  vtkNew<vtkPolyData> filledSel;
  {
    vtkNew<vtkSHYXHoleFillFilter> h;
    h->SetFairingContinuity(this->FairingContinuity);
    h->SetInputData(triSel);
    h->SetUpdateAttributes(false);
    h->Update();
    filledSel->ShallowCopy(h->GetOutput());
  }

  vtkNew<vtkPolyData> selProcessed;
  if (this->SkipAlphaWrapping)
  {
    selProcessed->ShallowCopy(filledSel);
  }
  else
  {
    vtkNew<vtkCGALAlphaWrapping> aw;
    aw->SetAbsoluteThresholds(this->AbsoluteThresholds);
    aw->SetAlpha(this->Alpha);
    aw->SetOffset(this->Offset);
    aw->SetInputData(filledSel);
    aw->SetUpdateAttributes(false);
    aw->Update();
    selProcessed->ShallowCopy(aw->GetOutput());
  }

  vtkNew<vtkPolyData> filledUnsel;
  {
    vtkNew<vtkSHYXHoleFillFilter> h2;
    h2->SetFairingContinuity(this->FairingContinuity);
    h2->SetInputData(triUnsel);
    h2->SetUpdateAttributes(false);
    h2->Update();
    filledUnsel->ShallowCopy(h2->GetOutput());
  }

  if (partUnsel->GetNumberOfCells() == 0)
  {
    if (this->UpdateAttributes)
    {
      this->interpolateAttributes(mesh, selProcessed);
    }
    output->ShallowCopy(selProcessed);
    return 1;
  }

  if (filledUnsel->GetNumberOfCells() == 0)
  {
    if (this->UpdateAttributes)
    {
      this->interpolateAttributes(mesh, selProcessed);
    }
    output->ShallowCopy(selProcessed);
    return 1;
  }

  vtkNew<vtkPolyData> united;
  std::vector<char> awFaceMask;
  {
    std::string unionErr;
    if (!CorefineUnionWithFaceOrigin(selProcessed, filledUnsel, this->OrientToBoundVolumeWhenNeeded,
          this->ThrowOnSelfIntersection, united, awFaceMask, unionErr))
    {
      vtkErrorMacro(<< "Boolean union with face provenance failed: " << unionErr);
      return 0;
    }
  }

  bool bridgeCleanupRan = false;
  std::vector<char> bridgeMask = awFaceMask;
  std::vector<char> maskAfterRemesh;

  if (this->EnableBridgeCleanup && united->GetNumberOfCells() > 0)
  {
    const double meshLen = united->GetLength();
    DilateCellMask(united, bridgeMask, this->BridgeDilateLayers);

    std::size_t nMarked = 0;
    for (char c : bridgeMask)
    {
      nMarked += (c != 0) ? 1u : 0u;
    }

    if (nMarked == 0)
    {
      vtkWarningMacro(<< "Bridge cleanup: no Alpha-Wrap faces found in the union result. "
                         "Skipping local remesh/smooth.");
      output->ShallowCopy(united);
    }
    else
    {
      double targetLen = this->BridgeTargetEdgeLength;
      if (targetLen <= 0.0)
      {
        targetLen = MeanEdgeLengthOfMaskedFaces(united, bridgeMask);
      }
      if (targetLen <= 0.0)
      {
        targetLen = 0.01 * (meshLen > 0.0 ? meshLen : 1.0);
      }

      vtkNew<vtkPolyData> cleaned;
      std::string err;
      std::vector<char>* maskOutPtr = this->ExportBridgeMask ? &maskAfterRemesh : nullptr;
      if (LocalRemeshAndSmoothBridge(united, bridgeMask, targetLen, this->BridgeRemeshIterations,
            this->BridgeRemeshRelaxationSteps, this->BridgeSmoothIterations, this->BridgeSmoothTimeStep,
            cleaned, err, maskOutPtr))
      {
        output->ShallowCopy(cleaned);
        bridgeCleanupRan = true;
      }
      else
      {
        vtkWarningMacro(<< "Bridge cleanup failed (" << err
                        << "). Returning boolean union without local remesh/smooth.");
        output->ShallowCopy(united);
      }
    }
  }
  else
  {
    output->ShallowCopy(united);
  }

  if (this->UpdateAttributes)
  {
    this->interpolateAttributes(mesh, output);
  }

  if (this->ExportBridgeMask)
  {
    if (bridgeCleanupRan &&
      static_cast<vtkIdType>(maskAfterRemesh.size()) == output->GetNumberOfCells())
    {
      AttachBridgeMaskArray(output, maskAfterRemesh, "SHYXBridgeCleanupMask");
    }
    else if (!bridgeCleanupRan &&
      static_cast<vtkIdType>(bridgeMask.size()) == output->GetNumberOfCells())
    {
      AttachBridgeMaskArray(output, bridgeMask, "SHYXBridgeCleanupMask");
    }
  }

  return 1;
}
