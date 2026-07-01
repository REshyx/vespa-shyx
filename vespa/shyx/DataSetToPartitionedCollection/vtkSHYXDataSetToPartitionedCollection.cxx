#include "vtkSHYXDataSetToPartitionedCollection.h"

#include <vtkAbstractArray.h>
#include <vtkAppendPolyData.h>
#include <vtkCellArray.h>
#include <vtkCellType.h>
#include <vtkCellData.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataAssembly.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkDoubleArray.h>
#include <vtkExtractCells.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkIOSSReader.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolygon.h>
#include <vtkSmartPointer.h>
#include <vtkTetra.h>
#include <vtkThreshold.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXDataSetToPartitionedCollection);

namespace
{
/** vtkThreshold BETWEEN upper bound for merged non-positive patch (values <= this, inclusive). */
constexpr double kPartitionLeZeroInclusive = 0.0;
/** vtkThreshold BETWEEN lower bound for high-side patches split by connectivity (values >= this, inclusive). */
constexpr double kPartitionGeOneInclusive = 1.0;
constexpr const char* kBoundaryRadialValueArrayName = "BoundaryRadialValue";
constexpr const char* kBoundaryRadialNormalArrayName = "BoundaryRadialValueNormal";
constexpr const char* kBoundaryVariableArrayPrefix = "BoundaryVariable";

/** vtkIOSSWriter expects contiguous global ids (1..N) per volume block. */
void SetContiguousGlobalIds(vtkUnstructuredGrid* ug)
{
  if (!ug)
  {
    return;
  }
  vtkNew<vtkIdTypeArray> pg;
  pg->SetName("GlobalIds");
  const vtkIdType np = ug->GetNumberOfPoints();
  pg->SetNumberOfTuples(np);
  for (vtkIdType i = 0; i < np; ++i)
  {
    pg->SetValue(i, i + 1);
  }
  ug->GetPointData()->SetGlobalIds(pg);

  vtkNew<vtkIdTypeArray> cg;
  cg->SetName("GlobalIds");
  const vtkIdType nc = ug->GetNumberOfCells();
  cg->SetNumberOfTuples(nc);
  for (vtkIdType i = 0; i < nc; ++i)
  {
    cg->SetValue(i, i + 1);
  }
  ug->GetCellData()->SetGlobalIds(cg);
}

/** Side sets still need contiguous cell GlobalIds; point GlobalIds must stay volume-unique for IOSS. */
void SetContiguousCellGlobalIdsPolyData(vtkPolyData* pd)
{
  if (!pd)
  {
    return;
  }
  vtkNew<vtkIdTypeArray> cg;
  cg->SetName("GlobalIds");
  const vtkIdType nc = pd->GetNumberOfCells();
  cg->SetNumberOfTuples(nc);
  for (vtkIdType i = 0; i < nc; ++i)
  {
    cg->SetValue(i, i + 1);
  }
  pd->GetCellData()->SetGlobalIds(cg);
}

void RestoreSurfacePointGlobalIdsFromVolume(vtkPolyData* surf, vtkUnstructuredGrid* tetVol)
{
  if (!surf || !tetVol)
  {
    return;
  }
  auto* origPt = vtkIdTypeArray::SafeDownCast(surf->GetPointData()->GetArray("vtkOriginalPointIds"));
  auto* volPtG = vtkIdTypeArray::SafeDownCast(tetVol->GetPointData()->GetGlobalIds());
  if (!origPt || !volPtG)
  {
    return;
  }
  const vtkIdType np = surf->GetNumberOfPoints();
  vtkNew<vtkIdTypeArray> pg;
  pg->SetName("GlobalIds");
  pg->SetNumberOfTuples(np);
  for (vtkIdType p = 0; p < np; ++p)
  {
    pg->SetValue(p, volPtG->GetValue(origPt->GetValue(p)));
  }
  surf->GetPointData()->SetGlobalIds(pg);
}

void AssignStandalonePatchPointGlobalIds(vtkPolyData* pd, vtkIdType& nextGid)
{
  const vtkIdType npt = pd->GetNumberOfPoints();
  vtkNew<vtkIdTypeArray> pg;
  pg->SetName("GlobalIds");
  pg->SetNumberOfTuples(npt);
  for (vtkIdType p = 0; p < npt; ++p)
  {
    pg->SetValue(p, nextGid + p);
  }
  nextGid += npt;
  pd->GetPointData()->SetGlobalIds(pg);
}

bool UnorderedTriMatch(vtkIdType a0, vtkIdType a1, vtkIdType a2, vtkIdType b0, vtkIdType b1, vtkIdType b2)
{
  std::array<vtkIdType, 3> A = { a0, a1, a2 };
  std::array<vtkIdType, 3> B = { b0, b1, b2 };
  std::sort(A.begin(), A.end());
  std::sort(B.begin(), B.end());
  return A[0] == B[0] && A[1] == B[1] && A[2] == B[2];
}

/** Which tet face (0..3) matches the surface triangle (volume point ids). */
int FindTetFaceIndexForTriangle(vtkUnstructuredGrid* tet, vtkIdType tetCellId, vtkIdType t0, vtkIdType t1,
  vtkIdType t2)
{
  if (tet->GetCellType(tetCellId) != VTK_TETRA)
  {
    return -1;
  }
  vtkNew<vtkIdList> cpts;
  tet->GetCellPoints(tetCellId, cpts);
  if (cpts->GetNumberOfIds() != 4)
  {
    return -1;
  }
  const vtkIdType* p = cpts->GetPointer(0);
  for (int face = 0; face < 4; ++face)
  {
    const vtkIdType* fl = vtkTetra::GetFaceArray(face);
    const vtkIdType f0 = p[fl[0]];
    const vtkIdType f1 = p[fl[1]];
    const vtkIdType f2 = p[fl[2]];
    if (UnorderedTriMatch(t0, t1, t2, f0, f1, f2))
    {
      return face;
    }
  }
  return -1;
}

/** Side-set cell array element_side: (volume cell GlobalId, Exodus tet face 1..4). PassThrough ids. */
void PrepareTetBoundarySurfaceForIoss(vtkUnstructuredGrid* tetVol, vtkPolyData* surf)
{
  if (!tetVol || !surf || surf->GetNumberOfCells() == 0)
  {
    return;
  }

  auto* origCell = vtkIdTypeArray::SafeDownCast(surf->GetCellData()->GetArray("vtkOriginalCellIds"));
  auto* origPt = vtkIdTypeArray::SafeDownCast(surf->GetPointData()->GetArray("vtkOriginalPointIds"));
  auto* volCellG = vtkIdTypeArray::SafeDownCast(tetVol->GetCellData()->GetGlobalIds());
  auto* volPtG = vtkIdTypeArray::SafeDownCast(tetVol->GetPointData()->GetGlobalIds());
  if (!origCell || !origPt || !volCellG || !volPtG)
  {
    return;
  }

  const vtkIdType nf = surf->GetNumberOfCells();
  vtkNew<vtkIntArray> es;
  es->SetName("element_side");
  es->SetNumberOfComponents(2);
  es->SetNumberOfTuples(nf);

  vtkNew<vtkIdList> cpts;
  for (vtkIdType fi = 0; fi < nf; ++fi)
  {
    const vtkIdType ocid = origCell->GetValue(fi);
    const vtkIdType globalElem = volCellG->GetValue(ocid);
    surf->GetCellPoints(fi, cpts);
    int exodusFace = 1;
    if (cpts->GetNumberOfIds() == 3)
    {
      const vtkIdType s0 = origPt->GetValue(cpts->GetId(0));
      const vtkIdType s1 = origPt->GetValue(cpts->GetId(1));
      const vtkIdType s2 = origPt->GetValue(cpts->GetId(2));
      const int found = FindTetFaceIndexForTriangle(tetVol, ocid, s0, s1, s2);
      if (found >= 0)
      {
        exodusFace = found + 1;
      }
    }
    es->SetTuple2(fi, static_cast<int>(globalElem), exodusFace);
  }
  surf->GetCellData()->AddArray(es);
}

vtkSmartPointer<vtkPolyData> BuildNodeSetPolyData(vtkPolyData* sideSurface)
{
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  vtkNew<vtkPoints> pts;
  pts->DeepCopy(sideSurface->GetPoints());
  out->SetPoints(pts);
  vtkNew<vtkCellArray> verts;
  const vtkIdType n = pts->GetNumberOfPoints();
  for (vtkIdType p = 0; p < n; ++p)
  {
    verts->InsertNextCell(1, &p);
  }
  out->SetVerts(verts);
  out->GetPointData()->DeepCopy(sideSurface->GetPointData());
  out->GetCellData()->Initialize();
  SetContiguousCellGlobalIdsPolyData(out);
  return out;
}

/**
 * vtkPolyDataConnectivityFilter often keeps the full input point list; only some points are
 * referenced by extracted cells. Drop unreferenced points and remap connectivity so each side{i} /
 * node{i} contains only vertices used by that patch's cells.
 */
void StripUnreferencedPoints(vtkPolyData* pd)
{
  if (!pd || pd->GetNumberOfPoints() == 0 || pd->GetNumberOfCells() == 0)
  {
    return;
  }

  pd->BuildCells();
  const vtkIdType np = pd->GetNumberOfPoints();
  std::vector<unsigned char> used(static_cast<size_t>(np), 0);
  vtkNew<vtkIdList> cpts;

  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    for (vtkIdType i = 0; i < cpts->GetNumberOfIds(); ++i)
    {
      const vtkIdType pid = cpts->GetId(i);
      if (pid >= 0 && pid < np)
      {
        used[static_cast<size_t>(pid)] = 1;
      }
    }
  }

  vtkIdType nUsed = 0;
  for (vtkIdType p = 0; p < np; ++p)
  {
    if (used[static_cast<size_t>(p)])
    {
      ++nUsed;
    }
  }

  if (nUsed == np)
  {
    return;
  }

  std::vector<vtkIdType> oldToNew(static_cast<size_t>(np), -1);
  vtkIdType newIx = 0;
  for (vtkIdType p = 0; p < np; ++p)
  {
    if (used[static_cast<size_t>(p)])
    {
      oldToNew[static_cast<size_t>(p)] = newIx++;
    }
  }

  vtkNew<vtkPoints> newPts;
  newPts->SetNumberOfPoints(nUsed);
  for (vtkIdType p = 0; p < np; ++p)
  {
    if (!used[static_cast<size_t>(p)])
    {
      continue;
    }
    double x[3];
    pd->GetPoint(p, x);
    newPts->SetPoint(oldToNew[static_cast<size_t>(p)], x);
  }

  vtkPointData* ipd = pd->GetPointData();
  vtkNew<vtkPointData> opd;
  for (int ai = 0; ai < ipd->GetNumberOfArrays(); ++ai)
  {
    vtkAbstractArray* arr = ipd->GetAbstractArray(ai);
    if (!arr || static_cast<vtkIdType>(arr->GetNumberOfTuples()) != np)
    {
      continue;
    }
    vtkAbstractArray* na = arr->NewInstance();
    na->SetName(arr->GetName());
    na->SetNumberOfComponents(arr->GetNumberOfComponents());
    na->SetNumberOfTuples(nUsed);
    for (vtkIdType p = 0; p < np; ++p)
    {
      if (used[static_cast<size_t>(p)])
      {
        na->SetTuple(oldToNew[static_cast<size_t>(p)], p, arr);
      }
    }
    opd->AddArray(na);
    na->Delete();
  }

  vtkNew<vtkCellArray> newVerts;
  vtkNew<vtkCellArray> newLines;
  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkCellArray> newStrips;

  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    const int ct = pd->GetCellType(cid);
    pd->GetCellPoints(cid, cpts);
    const int ncp = cpts->GetNumberOfIds();
    std::vector<vtkIdType> mapped(static_cast<size_t>(ncp));
    for (vtkIdType i = 0; i < ncp; ++i)
    {
      mapped[static_cast<size_t>(i)] = oldToNew[static_cast<size_t>(cpts->GetId(i))];
    }

    switch (ct)
    {
      case VTK_VERTEX:
      case VTK_POLY_VERTEX:
        newVerts->InsertNextCell(ncp, mapped.data());
        break;
      case VTK_LINE:
      case VTK_POLY_LINE:
        newLines->InsertNextCell(ncp, mapped.data());
        break;
      case VTK_TRIANGLE:
      case VTK_QUAD:
      case VTK_POLYGON:
      case VTK_PIXEL:
        newPolys->InsertNextCell(ncp, mapped.data());
        break;
      case VTK_TRIANGLE_STRIP:
        newStrips->InsertNextCell(ncp, mapped.data());
        break;
      default:
        if (ncp == 1)
        {
          newVerts->InsertNextCell(1, mapped.data());
        }
        else if (ncp == 2)
        {
          newLines->InsertNextCell(2, mapped.data());
        }
        else if (ncp >= 3)
        {
          newPolys->InsertNextCell(ncp, mapped.data());
        }
        break;
    }
  }

  pd->SetPoints(newPts);
  pd->SetVerts(newVerts);
  pd->SetLines(newLines);
  pd->SetPolys(newPolys);
  pd->SetStrips(newStrips);
  vtkPointData* outp = pd->GetPointData();
  outp->ShallowCopy(opd);
  if (vtkIdTypeArray* g = vtkIdTypeArray::SafeDownCast(outp->GetAbstractArray("GlobalIds")))
  {
    outp->SetGlobalIds(g);
  }
  if (vtkDataArray* nrm = outp->GetArray("Normals"))
  {
    outp->SetNormals(nrm);
  }
  pd->Modified();
}

double TriangleArea(const double a[3], const double b[3], const double c[3])
{
  double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
  double ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
  double cp[3];
  vtkMath::Cross(ab, ac, cp);
  return 0.5 * vtkMath::Norm(cp);
}

/** Sum of per-cell areas (triangles via cross product; larger polygons via vtkPolygon::ComputeArea). */
double ComputePolyDataSurfaceArea(vtkPolyData* pd)
{
  if (!pd || pd->GetNumberOfCells() == 0)
  {
    return 0.0;
  }
  double sum = 0.0;
  vtkPoints* pts = pd->GetPoints();
  vtkNew<vtkIdList> ids;
  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    pd->GetCellPoints(cid, ids);
    const int n = ids->GetNumberOfIds();
    if (n < 3 || !pts)
    {
      continue;
    }
    if (n == 3)
    {
      double p0[3], p1[3], p2[3];
      pts->GetPoint(ids->GetId(0), p0);
      pts->GetPoint(ids->GetId(1), p1);
      pts->GetPoint(ids->GetId(2), p2);
      sum += TriangleArea(p0, p1, p2);
    }
    else
    {
      double polyN[3];
      sum += vtkPolygon::ComputeArea(pts, static_cast<vtkIdType>(n), ids->GetPointer(0), polyN);
    }
  }
  return sum;
}

void SortSidePiecesByAreaDescending(std::vector<vtkSmartPointer<vtkPolyData>>* pieces)
{
  if (!pieces || pieces->size() <= 1)
  {
    return;
  }
  const size_t n = pieces->size();
  std::vector<double> areas;
  areas.reserve(n);
  for (const auto& p : *pieces)
  {
    areas.push_back(ComputePolyDataSurfaceArea(p.GetPointer()));
  }
  std::vector<size_t> order(n);
  std::iota(order.begin(), order.end(), size_t{ 0 });
  std::stable_sort(order.begin(), order.end(),
    [&areas](size_t a, size_t b) { return areas[a] > areas[b]; });
  std::vector<vtkSmartPointer<vtkPolyData>> sorted;
  sorted.reserve(n);
  for (size_t idx : order)
  {
    sorted.push_back((*pieces)[idx]);
  }
  *pieces = std::move(sorted);
}

/** Move 3rd patch to front, 1st patch to end; order between is 2nd then old index 3..n-1. */
void ApplyCustomPostReorder(std::vector<vtkSmartPointer<vtkPolyData>>* pieces)
{
  if (!pieces || pieces->size() < 3)
  {
    return;
  }
  std::vector<vtkSmartPointer<vtkPolyData>>& v = *pieces;
  const size_t n = v.size();
  std::vector<vtkSmartPointer<vtkPolyData>> out;
  out.reserve(n);
  out.push_back(v[2]);
  out.push_back(v[1]);
  for (size_t i = 3; i < n; ++i)
  {
    out.push_back(v[i]);
  }
  out.push_back(v[0]);
  v = std::move(out);
}

double Cross2D(const double a[2], const double b[2])
{
  return a[0] * b[1] - a[1] * b[0];
}

void AddCountedEdge(std::map<std::pair<vtkIdType, vtkIdType>, int>& edgeCounts, vtkIdType a, vtkIdType b)
{
  if (a == b)
  {
    return;
  }
  if (b < a)
  {
    std::swap(a, b);
  }
  ++edgeCounts[std::make_pair(a, b)];
}

bool ComputeAverageCellNormal(vtkPolyData* pd, double normal[3])
{
  normal[0] = normal[1] = normal[2] = 0.0;
  if (!pd || !pd->GetPoints())
  {
    return false;
  }

  vtkNew<vtkIdList> cpts;
  double p0[3], p1[3], p2[3], e1[3], e2[3], n[3];
  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    if (cpts->GetNumberOfIds() < 3)
    {
      continue;
    }
    pd->GetPoint(cpts->GetId(0), p0);
    for (vtkIdType k = 1; k + 1 < cpts->GetNumberOfIds(); ++k)
    {
      pd->GetPoint(cpts->GetId(k), p1);
      pd->GetPoint(cpts->GetId(k + 1), p2);
      vtkMath::Subtract(p1, p0, e1);
      vtkMath::Subtract(p2, p0, e2);
      vtkMath::Cross(e1, e2, n);
      normal[0] += n[0];
      normal[1] += n[1];
      normal[2] += n[2];
    }
  }

  const double len = vtkMath::Norm(normal);
  if (len <= 1e-30 || !vtkMath::IsFinite(len))
  {
    return false;
  }
  normal[0] /= len;
  normal[1] /= len;
  normal[2] /= len;
  return true;
}

/**
 * For one side-set block, add point and cell scalars that are 1 on the topological boundary and
 * radially interpolated toward the boundary in that block's fitted local plane. Cell values are the
 * average of their point values so vtkIOSSWriter can expose the array under Side Set Arrays. No
 * locator fallback is used: points whose ray does not intersect a boundary segment receive NaN.
 */
void AddBoundaryRadialValueArray(vtkPolyData* pd, const char* arrayName, double exponent)
{
  if (!pd || !arrayName || arrayName[0] == '\0')
  {
    return;
  }

  const vtkIdType nPts = pd->GetNumberOfPoints();
  const vtkIdType nCells = pd->GetNumberOfCells();
  vtkNew<vtkDoubleArray> values;
  values->SetName(arrayName);
  values->SetNumberOfComponents(1);
  values->SetNumberOfTuples(nPts);
  const double nanv = std::numeric_limits<double>::quiet_NaN();
  values->Fill(nanv);

  vtkNew<vtkDoubleArray> cellValues;
  cellValues->SetName(arrayName);
  cellValues->SetNumberOfComponents(1);
  cellValues->SetNumberOfTuples(nCells);
  cellValues->Fill(nanv);

  if (nPts == 0 || nCells == 0 || !pd->GetPoints())
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }

  std::map<std::pair<vtkIdType, vtkIdType>, int> edgeCounts;
  vtkNew<vtkIdList> cpts;
  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    const vtkIdType ncp = cpts->GetNumberOfIds();
    if (ncp == 2)
    {
      AddCountedEdge(edgeCounts, cpts->GetId(0), cpts->GetId(1));
      continue;
    }
    if (ncp < 3)
    {
      continue;
    }
    for (vtkIdType k = 0; k < ncp; ++k)
    {
      AddCountedEdge(edgeCounts, cpts->GetId(k), cpts->GetId((k + 1) % ncp));
    }
  }

  std::vector<std::pair<vtkIdType, vtkIdType>> boundaryEdges;
  std::vector<unsigned char> isBoundaryPoint(static_cast<size_t>(nPts), 0);
  for (const auto& item : edgeCounts)
  {
    if (item.second != 1)
    {
      continue;
    }
    const vtkIdType a = item.first.first;
    const vtkIdType b = item.first.second;
    if (a < 0 || a >= nPts || b < 0 || b >= nPts)
    {
      continue;
    }
    boundaryEdges.push_back(item.first);
    isBoundaryPoint[static_cast<size_t>(a)] = 1;
    isBoundaryPoint[static_cast<size_t>(b)] = 1;
  }

  if (boundaryEdges.empty())
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }

  double center[3] = { 0.0, 0.0, 0.0 };
  vtkIdType nBoundaryPts = 0;
  double x[3];
  for (vtkIdType p = 0; p < nPts; ++p)
  {
    if (!isBoundaryPoint[static_cast<size_t>(p)])
    {
      continue;
    }
    pd->GetPoint(p, x);
    center[0] += x[0];
    center[1] += x[1];
    center[2] += x[2];
    ++nBoundaryPts;
  }
  if (nBoundaryPts == 0)
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }
  center[0] /= static_cast<double>(nBoundaryPts);
  center[1] /= static_cast<double>(nBoundaryPts);
  center[2] /= static_cast<double>(nBoundaryPts);

  double normal[3];
  if (!ComputeAverageCellNormal(pd, normal))
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }

  double axisU[3], axisV[3];
  vtkMath::Perpendiculars(normal, axisU, axisV, 0.0);

  std::vector<std::array<double, 2>> uv(static_cast<size_t>(nPts));
  double scale = 0.0;
  for (vtkIdType p = 0; p < nPts; ++p)
  {
    pd->GetPoint(p, x);
    const double rel[3] = { x[0] - center[0], x[1] - center[1], x[2] - center[2] };
    uv[static_cast<size_t>(p)] = { vtkMath::Dot(rel, axisU), vtkMath::Dot(rel, axisV) };
    scale = std::max(scale, std::abs(uv[static_cast<size_t>(p)][0]));
    scale = std::max(scale, std::abs(uv[static_cast<size_t>(p)][1]));
  }

  const double geomTol = 1e-12 * std::max(1.0, scale);
  const double rayTol = 1e-9;
  for (vtkIdType p = 0; p < nPts; ++p)
  {
    if (isBoundaryPoint[static_cast<size_t>(p)])
    {
      values->SetValue(p, 0.0);
      continue;
    }

    const double q[2] = { uv[static_cast<size_t>(p)][0], uv[static_cast<size_t>(p)][1] };
    const double qNorm = std::sqrt(q[0] * q[0] + q[1] * q[1]);
    if (qNorm <= geomTol)
    {
      values->SetValue(p, 1.0);
      continue;
    }

    double bestS = std::numeric_limits<double>::infinity();
    for (const auto& edge : boundaryEdges)
    {
      const double a[2] = { uv[static_cast<size_t>(edge.first)][0],
        uv[static_cast<size_t>(edge.first)][1] };
      const double b[2] = { uv[static_cast<size_t>(edge.second)][0],
        uv[static_cast<size_t>(edge.second)][1] };
      const double e[2] = { b[0] - a[0], b[1] - a[1] };
      const double denom = Cross2D(q, e);
      if (std::abs(denom) <= geomTol * qNorm)
      {
        continue;
      }
      const double s = Cross2D(a, e) / denom;
      const double u = Cross2D(a, q) / denom;
      if (s >= 1.0 - rayTol && u >= -rayTol && u <= 1.0 + rayTol && s < bestS)
      {
        bestS = s;
      }
    }

    if (vtkMath::IsFinite(bestS) && bestS > 0.0)
    {
      double raw = 1.0 / bestS;
      raw = std::max(0.0, std::min(1.0, raw));
      values->SetValue(p, 1.0 - std::pow(raw, exponent));
    }
  }

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    double sum = 0.0;
    vtkIdType count = 0;
    for (vtkIdType k = 0; k < cpts->GetNumberOfIds(); ++k)
    {
      const vtkIdType pid = cpts->GetId(k);
      if (pid < 0 || pid >= nPts)
      {
        continue;
      }
      const double v = values->GetValue(pid);
      if (!vtkMath::IsFinite(v))
      {
        continue;
      }
      sum += v;
      ++count;
    }
    if (count > 0)
    {
      cellValues->SetValue(cid, sum / static_cast<double>(count));
    }
  }

  pd->GetPointData()->RemoveArray(arrayName);
  pd->GetPointData()->AddArray(values);
  pd->GetCellData()->RemoveArray(arrayName);
  pd->GetCellData()->AddArray(cellValues);
}

void InitializeVolumeBoundaryRadialNormalArray(vtkUnstructuredGrid* volume, const char* arrayName)
{
  if (!volume || !arrayName || arrayName[0] == '\0')
  {
    return;
  }

  vtkNew<vtkDoubleArray> values;
  values->SetName(arrayName);
  values->SetNumberOfComponents(3);
  values->SetNumberOfTuples(volume->GetNumberOfPoints());
  values->Fill(0.0);

  volume->GetPointData()->RemoveArray(arrayName);
  volume->GetPointData()->AddArray(values);
}

std::vector<std::string> BoundaryVariableArrayNames(size_t nVariables)
{
  std::vector<std::string> names;
  names.reserve(nVariables);
  for (size_t i = 0; i < nVariables; ++i)
  {
    names.push_back(std::string(kBoundaryVariableArrayPrefix) + std::to_string(i + 1));
  }
  return names;
}

void InitializeVolumeBoundaryVariableArrays(
  vtkUnstructuredGrid* volume, const std::vector<std::string>& arrayNames)
{
  if (!volume)
  {
    return;
  }

  for (const std::string& arrayName : arrayNames)
  {
    if (arrayName.empty())
    {
      continue;
    }

    vtkNew<vtkDoubleArray> values;
    values->SetName(arrayName.c_str());
    values->SetNumberOfComponents(1);
    values->SetNumberOfTuples(volume->GetNumberOfPoints());
    values->Fill(0.0);

    volume->GetPointData()->RemoveArray(arrayName.c_str());
    volume->GetPointData()->AddArray(values);
  }
}

vtkIdType AccumulateBoundaryRadialNormalToVolume(
  vtkPolyData* side, vtkUnstructuredGrid* volume, const char* radialArrayName,
  const char* vectorArrayName, const std::vector<std::string>& variableArrayNames,
  const std::vector<double>& variables, bool useRadialValueForNormal,
  std::vector<unsigned int>& volumePointWriteCounts)
{
  if (!side || !volume || !radialArrayName || radialArrayName[0] == '\0' || !vectorArrayName ||
    vectorArrayName[0] == '\0' || variableArrayNames.empty() || variables.empty())
  {
    return 0;
  }

  double normal[3];
  if (!ComputeAverageCellNormal(side, normal))
  {
    return 0;
  }

  vtkDataArray* sideValues =
    vtkDataArray::SafeDownCast(side->GetPointData()->GetArray(radialArrayName));
  vtkDataArray* sideGids = vtkDataArray::SafeDownCast(side->GetPointData()->GetGlobalIds());
  vtkDoubleArray* volumeVectors =
    vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(vectorArrayName));
  if (!sideValues || !sideGids || !volumeVectors || sideValues->GetNumberOfComponents() < 1 ||
    sideValues->GetNumberOfTuples() != side->GetNumberOfPoints() ||
    sideGids->GetNumberOfTuples() != side->GetNumberOfPoints() ||
    volumeVectors->GetNumberOfComponents() != 3 ||
    volumeVectors->GetNumberOfTuples() != volume->GetNumberOfPoints())
  {
    return 0;
  }

  std::vector<vtkDoubleArray*> volumeVariables;
  volumeVariables.reserve(variableArrayNames.size());
  for (const std::string& arrayName : variableArrayNames)
  {
    vtkDoubleArray* arr = vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(arrayName.c_str()));
    if (!arr || arr->GetNumberOfComponents() != 1 ||
      arr->GetNumberOfTuples() != volume->GetNumberOfPoints())
    {
      return 0;
    }
    volumeVariables.push_back(arr);
  }

  const vtkIdType nVolumePts = volume->GetNumberOfPoints();
  if (static_cast<vtkIdType>(volumePointWriteCounts.size()) != nVolumePts)
  {
    volumePointWriteCounts.assign(static_cast<size_t>(nVolumePts), 0);
  }

  vtkIdType duplicateWrites = 0;
  for (vtkIdType p = 0; p < side->GetNumberOfPoints(); ++p)
  {
    const double v = sideValues->GetComponent(p, 0);
    if (!vtkMath::IsFinite(v))
    {
      continue;
    }

    const vtkIdType volumePointId = static_cast<vtkIdType>(sideGids->GetComponent(p, 0)) - 1;
    if (volumePointId < 0 || volumePointId >= nVolumePts)
    {
      continue;
    }

    if (volumePointWriteCounts[static_cast<size_t>(volumePointId)] > 0)
    {
      ++duplicateWrites;
    }

    double out[3];
    volumeVectors->GetTuple(volumePointId, out);
    const double normalWeight = useRadialValueForNormal ? v : 1.0;
    out[0] += normalWeight * normal[0];
    out[1] += normalWeight * normal[1];
    out[2] += normalWeight * normal[2];
    volumeVectors->SetTuple(volumePointId, out);
    for (size_t i = 0; i < volumeVariables.size(); ++i)
    {
      const double value = i < variables.size() && vtkMath::IsFinite(variables[i]) ? variables[i] : 0.0;
      volumeVariables[i]->SetValue(volumePointId, volumeVariables[i]->GetValue(volumePointId) + value);
    }
    ++volumePointWriteCounts[static_cast<size_t>(volumePointId)];
  }
  return duplicateWrites;
}

void AverageRepeatedBoundaryRadialNormalWrites(
  vtkUnstructuredGrid* volume, const char* vectorArrayName,
  const std::vector<std::string>& variableArrayNames,
  const std::vector<unsigned int>& writeCounts)
{
  if (!volume || !vectorArrayName || vectorArrayName[0] == '\0')
  {
    return;
  }

  vtkDoubleArray* volumeVectors =
    vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(vectorArrayName));
  if (!volumeVectors || volumeVectors->GetNumberOfComponents() != 3 ||
    volumeVectors->GetNumberOfTuples() != volume->GetNumberOfPoints() ||
    static_cast<vtkIdType>(writeCounts.size()) != volume->GetNumberOfPoints())
  {
    return;
  }

  std::vector<vtkDoubleArray*> volumeVariables;
  volumeVariables.reserve(variableArrayNames.size());
  for (const std::string& arrayName : variableArrayNames)
  {
    vtkDoubleArray* arr = vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(arrayName.c_str()));
    if (arr && arr->GetNumberOfComponents() == 1 &&
      arr->GetNumberOfTuples() == volume->GetNumberOfPoints())
    {
      volumeVariables.push_back(arr);
    }
  }

  for (vtkIdType p = 0; p < volume->GetNumberOfPoints(); ++p)
  {
    const unsigned int count = writeCounts[static_cast<size_t>(p)];
    if (count <= 1)
    {
      continue;
    }
    double out[3];
    volumeVectors->GetTuple(p, out);
    out[0] /= static_cast<double>(count);
    out[1] /= static_cast<double>(count);
    out[2] /= static_cast<double>(count);
    volumeVectors->SetTuple(p, out);
    for (vtkDoubleArray* arr : volumeVariables)
    {
      arr->SetValue(p, arr->GetValue(p) / static_cast<double>(count));
    }
  }
}

/** Sub-mesh containing only the listed cell ids (e.g. one EndpointIndex bucket). */
void BuildPolyDataWithCellIds(vtkPolyData* inMesh, const std::vector<vtkIdType>& keepCells, vtkPolyData* out)
{
  out->Initialize();
  if (!inMesh || keepCells.empty())
  {
    return;
  }

  const vtkIdType nPts = inMesh->GetNumberOfPoints();
  const vtkIdType nCells = inMesh->GetNumberOfCells();
  if (nCells == 0)
  {
    return;
  }

  std::set<vtkIdType> keepSet(keepCells.begin(), keepCells.end());
  std::vector<char> usedPt(static_cast<size_t>(nPts), 0);
  vtkIdType npts;
  const vtkIdType* pids;
  for (vtkIdType cid : keepSet)
  {
    if (cid < 0 || cid >= nCells)
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

  for (vtkIdType cid : keepSet)
  {
    if (cid < 0 || cid >= nCells)
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
 * vtkThreshold on point \p arrayName first component in [\p lowerThr, \p upperThr] (vtkThreshold
 * THRESHOLD_BETWEEN; interval endpoints are inclusive in VTK), then vtkGeometryFilter +
 * vtkPolyDataConnectivityFilter. All Scalars is always off (any corner may qualify a cell). When
 * \p mergeAllRegions is false, each connected region is its own piece. When true, all regions are
 * vtkAppendPolyData-merged into one surface.
 */
void CollectSurfacePiecesByThresholdRangeConnectivity(vtkPolyData* surface, const char* arrayName,
  double lowerThr, double upperThr, bool mergeAllRegions,
  std::vector<vtkSmartPointer<vtkPolyData>>* outPieces)
{
  outPieces->clear();
  if (!surface || surface->GetNumberOfCells() == 0 || !arrayName || !arrayName[0])
  {
    return;
  }

  vtkDataArray* arr = vtkDataArray::SafeDownCast(surface->GetPointData()->GetAbstractArray(arrayName));
  if (!arr || arr->GetNumberOfTuples() != surface->GetNumberOfPoints() || arr->GetNumberOfComponents() < 1)
  {
    return;
  }

  vtkNew<vtkThreshold> th;
  th->SetInputData(surface);
  th->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, arrayName);
  th->SetThresholdFunction(vtkThreshold::THRESHOLD_BETWEEN);
  th->SetLowerThreshold(lowerThr);
  th->SetUpperThreshold(upperThr);
  th->SetSelectedComponent(0);
  th->SetComponentModeToUseSelected();
  th->SetAllScalars(0);
  th->Update();

  vtkDataSet* thOut = vtkDataSet::SafeDownCast(th->GetOutputDataObject(0));
  if (!thOut || thOut->GetNumberOfCells() == 0)
  {
    return;
  }

  vtkNew<vtkGeometryFilter> geom;
  geom->SetInputData(thOut);
  geom->Update();
  vtkPolyData* poly = vtkPolyData::SafeDownCast(geom->GetOutput());
  if (!poly || poly->GetNumberOfCells() == 0)
  {
    return;
  }

  vtkNew<vtkPolyDataConnectivityFilter> probe;
  probe->SetInputData(poly);
  probe->SetExtractionModeToAllRegions();
  probe->ColorRegionsOn();
  probe->Update();

  const int nRegions = static_cast<int>(probe->GetNumberOfExtractedRegions());
  if (nRegions <= 0)
  {
    return;
  }

  vtkNew<vtkPolyDataConnectivityFilter> conn;
  conn->SetInputData(poly);
  conn->SetExtractionModeToSpecifiedRegions();
  conn->ColorRegionsOff();

  if (!mergeAllRegions)
  {
    for (int r = 0; r < nRegions; ++r)
    {
      conn->InitializeSpecifiedRegionList();
      conn->AddSpecifiedRegion(r);
      conn->Update();
      vtkPolyData* out = conn->GetOutput();
      if (!out || out->GetNumberOfCells() == 0)
      {
        continue;
      }
      vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
      copy->DeepCopy(out);
      StripUnreferencedPoints(copy);
      outPieces->push_back(copy);
    }
  }
  else
  {
    vtkNew<vtkAppendPolyData> append;
    int nAppended = 0;
    for (int r = 0; r < nRegions; ++r)
    {
      conn->InitializeSpecifiedRegionList();
      conn->AddSpecifiedRegion(r);
      conn->Update();
      vtkPolyData* out = conn->GetOutput();
      if (!out || out->GetNumberOfCells() == 0)
      {
        continue;
      }
      append->AddInputData(out);
      ++nAppended;
    }
    if (nAppended == 0)
    {
      return;
    }
    append->Update();
    vtkPolyData* mergedOut = append->GetOutput();
    if (!mergedOut || mergedOut->GetNumberOfCells() == 0)
    {
      return;
    }
    vtkSmartPointer<vtkPolyData> merged = vtkSmartPointer<vtkPolyData>::New();
    merged->DeepCopy(mergedOut);
    StripUnreferencedPoints(merged);
    outPieces->push_back(merged);
  }
}

/** Build side patches: one merged region for first component <= 0 (any corner; vtkThreshold BETWEEN),
 * then one patch per connected region where the first component >= 1 (any corner). */
void BuildSidePiecesSignThreshold(vtkPolyData* surface, const char* arrayName,
  std::vector<vtkSmartPointer<vtkPolyData>>* sidePieces)
{
  sidePieces->clear();

  std::vector<vtkSmartPointer<vtkPolyData>> leZero;
  CollectSurfacePiecesByThresholdRangeConnectivity(surface, arrayName,
    std::numeric_limits<double>::lowest(), kPartitionLeZeroInclusive, true, &leZero);

  std::vector<vtkSmartPointer<vtkPolyData>> geOne;
  CollectSurfacePiecesByThresholdRangeConnectivity(surface, arrayName, kPartitionGeOneInclusive,
    std::numeric_limits<double>::max(), false, &geOne);

  if (!leZero.empty() && leZero[0] && leZero[0]->GetNumberOfCells() > 0)
  {
    sidePieces->push_back(leZero[0]);
  }
  for (auto& p : geOne)
  {
    if (p && p->GetNumberOfCells() > 0)
    {
      sidePieces->push_back(p);
    }
  }
}

void CollectCuspConnectedSurfacePieces(vtkPolyData* surface, double featureAngle,
  std::vector<vtkSmartPointer<vtkPolyData>>* outPieces)
{
  outPieces->clear();
  if (!surface || surface->GetNumberOfCells() == 0)
  {
    return;
  }

  vtkNew<vtkPolyDataNormals> nrm;
  nrm->SetInputData(surface);
  nrm->SplittingOn();
  nrm->SetFeatureAngle(featureAngle);
  nrm->ConsistencyOn();
  nrm->AutoOrientNormalsOn();
  nrm->Update();

  vtkPolyData* split = nrm->GetOutput();

  vtkNew<vtkPolyDataConnectivityFilter> probe;
  probe->SetInputData(split);
  probe->SetExtractionModeToAllRegions();
  probe->ColorRegionsOn();
  probe->Update();

  const int nRegions = static_cast<int>(probe->GetNumberOfExtractedRegions());
  if (nRegions <= 0)
  {
    return;
  }

  vtkNew<vtkPolyDataConnectivityFilter> conn;
  conn->SetInputData(split);
  conn->SetExtractionModeToSpecifiedRegions();
  conn->ColorRegionsOff();

  for (int r = 0; r < nRegions; ++r)
  {
    conn->InitializeSpecifiedRegionList();
    conn->AddSpecifiedRegion(r);
    conn->Update();
    vtkPolyData* out = conn->GetOutput();
    if (!out || out->GetNumberOfCells() == 0)
    {
      continue;
    }
    vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
    copy->DeepCopy(out);
    StripUnreferencedPoints(copy);
    outPieces->push_back(copy);
  }
}

/** IOSS-style names + Exodus-style entity id. vtkIOSSReader::ENTITY_TYPE() is protected in PV 6, so
 *  entity kind is conveyed by vtkDataAssembly (element_blocks / side_sets / node_sets). */
void SetIossBlockMeta(
  vtkPartitionedDataSetCollection* coll, unsigned int pdsIdx, const char* name, int entityId)
{
  vtkInformation* meta = coll->GetMetaData(pdsIdx);
  if (!meta)
  {
    return;
  }
  meta->Set(vtkCompositeDataSet::NAME(), name);
  meta->Set(vtkIOSSReader::ENTITY_ID(), entityId);
}

void AppendPdcBlock(vtkPartitionedDataSetCollection* coll, unsigned int& blockIndex, vtkDataSet* ds,
  const char* iossName, int entityId)
{
  vtkNew<vtkPartitionedDataSet> pds;
  pds->SetPartition(0, ds);
  coll->SetPartitionedDataSet(blockIndex, pds);
  SetIossBlockMeta(coll, blockIndex, iossName, entityId);
  ++blockIndex;
}

std::vector<std::string> ParseBlockNames(const char* names)
{
  std::vector<std::string> result;
  if (!names || names[0] == '\0')
  {
    return result;
  }

  std::stringstream stream(names);
  std::string line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    result.push_back(line);
  }
  return result;
}

std::vector<std::vector<double>> ParseLineDoubleMatrix(const char* values)
{
  std::vector<std::vector<double>> result;
  if (!values || values[0] == '\0')
  {
    return result;
  }

  std::stringstream stream(values);
  std::string line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    std::stringstream lineStream(line);
    std::vector<double> row;
    double value = 0.0;
    while (lineStream >> value)
    {
      row.push_back(value);
    }
    if (row.empty())
    {
      row.push_back(0.0);
    }
    result.push_back(row);
  }
  return result;
}

std::vector<double> ResolveLineDoubles(
  const std::vector<std::vector<double>>& values, unsigned int blockIndex, size_t nVariables)
{
  std::vector<double> result(nVariables, 0.0);
  if (blockIndex >= values.size())
  {
    return result;
  }
  for (size_t i = 0; i < nVariables && i < values[blockIndex].size(); ++i)
  {
    result[i] = values[blockIndex][i];
  }
  return result;
}

size_t CountBoundaryVariables(const std::vector<std::vector<double>>& values)
{
  size_t nVariables = 1;
  for (const auto& row : values)
  {
    nVariables = std::max(nVariables, row.size());
  }
  return nVariables;
}

bool HasNonZeroBoundaryVariable(
  const std::vector<std::vector<double>>& values, bool hasTet, unsigned int nSideNodePairs)
{
  const unsigned int sideOffset = (hasTet ? 1u : 0u) + nSideNodePairs;
  for (unsigned int i = 0; i < nSideNodePairs; ++i)
  {
    if (sideOffset + i >= values.size())
    {
      continue;
    }
    for (double value : values[sideOffset + i])
    {
      if (value != 0.0 && vtkMath::IsFinite(value))
      {
        return true;
      }
    }
  }
  return false;
}

bool HasNonZeroBoundaryVariableRow(const std::vector<double>& values)
{
  for (double value : values)
  {
    if (value != 0.0 && vtkMath::IsFinite(value))
    {
      return true;
    }
  }
  return false;
}

std::string ResolveBlockName(
  const std::vector<std::string>& customNames, unsigned int blockIndex, const std::string& fallback)
{
  if (blockIndex < customNames.size() && !customNames[blockIndex].empty())
  {
    return customNames[blockIndex];
  }
  return fallback;
}

void BuildIossAssembly(vtkPartitionedDataSetCollection* coll, bool hasTet,
  unsigned int nSideNodePairs, const std::vector<std::string>& blockNames)
{
  vtkNew<vtkDataAssembly> rootAsm;
  rootAsm->SetRootNodeName("IOSS");

  const int elemBlocksNode = rootAsm->AddNode("element_blocks");
  const int sideSetsNode = rootAsm->AddNode("side_sets");
  const int nodeSetsNode = rootAsm->AddNode("node_sets");

  unsigned int dsCursor = 0;

  if (hasTet)
  {
    const std::string elemName = ResolveBlockName(blockNames, dsCursor, "tetrahedra");
    const int leaf =
      rootAsm->AddNode(vtkDataAssembly::MakeValidNodeName(elemName.c_str()).c_str(), elemBlocksNode);
    rootAsm->SetAttribute(leaf, "label", elemName.c_str());
    rootAsm->AddDataSetIndex(leaf, dsCursor++);
  }

  for (unsigned int i = 0; i < nSideNodePairs; ++i)
  {
    const std::string nodeName =
      ResolveBlockName(blockNames, dsCursor, "node" + std::to_string(i));
    const int leafN = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(nodeName.c_str()).c_str(), nodeSetsNode);
    rootAsm->SetAttribute(leafN, "label", nodeName.c_str());
    rootAsm->AddDataSetIndex(leafN, dsCursor++);
  }
  for (unsigned int i = 0; i < nSideNodePairs; ++i)
  {
    const std::string sideName =
      ResolveBlockName(blockNames, dsCursor, "side" + std::to_string(i));
    const int leafS = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(sideName.c_str()).c_str(), sideSetsNode);
    rootAsm->SetAttribute(leafS, "label", sideName.c_str());
    rootAsm->AddDataSetIndex(leafS, dsCursor++);
  }

  coll->SetDataAssembly(rootAsm);
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXDataSetToPartitionedCollection::vtkSHYXDataSetToPartitionedCollection()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetPartitionPointArrayName("EndpointIndex");
  this->SetBoundaryVariables("");
}

//------------------------------------------------------------------------------
vtkSHYXDataSetToPartitionedCollection::~vtkSHYXDataSetToPartitionedCollection()
{
  this->SetPartitionPointArrayName(nullptr);
  this->SetBoundaryVariables(nullptr);
  this->SetBlockNames(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXDataSetToPartitionedCollection::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FeatureAngle: " << this->FeatureAngle << "\n";
  os << indent << "PartitionPointArrayName: "
     << (this->PartitionPointArrayName ? this->PartitionPointArrayName : "(null)") << "\n";
  os << indent << "SortByArea: " << this->SortByArea << "\n";
  os << indent << "CustomPostReorder: " << this->CustomPostReorder << "\n";
  os << indent << "ComputeBoundaryRadialValue: " << this->ComputeBoundaryRadialValue << "\n";
  os << indent << "BoundaryRadialNormalFalloffFactor: "
     << this->BoundaryRadialNormalFalloffFactor << "\n";
  os << indent << "BoundaryVariables: "
     << (this->BoundaryVariables ? this->BoundaryVariables : "(null)") << "\n";
  os << indent << "BlockNames: " << (this->BlockNames ? this->BlockNames : "(null)") << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXDataSetToPartitionedCollection::FillInputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXDataSetToPartitionedCollection::FillOutputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXDataSetToPartitionedCollection::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  vtkPartitionedDataSetCollection* output =
    vtkPartitionedDataSetCollection::GetData(outputVector, 0);

  if (!input)
  {
    vtkErrorMacro(<< "Input is null.");
    return 0;
  }
  if (!output)
  {
    vtkErrorMacro(<< "Output PartitionedDataSetCollection is null.");
    return 0;
  }

  vtkNew<vtkPartitionedDataSetCollection> result;
  unsigned int blockIndex = 0;
  int nextEntityId = 1;
  vtkSmartPointer<vtkUnstructuredGrid> volumeGridForIoss;
  const std::vector<std::string> customBlockNames = ParseBlockNames(this->BlockNames);
  const std::vector<std::vector<double>> boundaryVariables =
    ParseLineDoubleMatrix(this->BoundaryVariables);

  vtkUnstructuredGrid* ug = vtkUnstructuredGrid::SafeDownCast(input);
  vtkNew<vtkPolyData> surfaceWork;

  if (ug)
  {
    const vtkIdType nCells = ug->GetNumberOfCells();
    vtkNew<vtkIdList> tetIds;
    tetIds->Allocate(nCells);
    for (vtkIdType c = 0; c < nCells; ++c)
    {
      if (ug->GetCellType(c) == VTK_TETRA)
      {
        tetIds->InsertNextId(c);
      }
    }

    if (tetIds->GetNumberOfIds() > 0)
    {
      vtkNew<vtkExtractCells> exTet;
      exTet->SetInputData(ug);
      exTet->SetCellList(tetIds);
      exTet->Update();
      vtkUnstructuredGrid* tetOut = exTet->GetOutput();
      if (tetOut && tetOut->GetNumberOfCells() > 0)
      {
        volumeGridForIoss = vtkSmartPointer<vtkUnstructuredGrid>::New();
        volumeGridForIoss->DeepCopy(tetOut);
        SetContiguousGlobalIds(volumeGridForIoss);

        // Boundary of tet-only mesh; pass-through ids for vtkIOSSWriter (GlobalIds + element_side).
        vtkNew<vtkDataSetSurfaceFilter> surf;
        surf->SetInputData(volumeGridForIoss);
        surf->PassThroughCellIdsOn();
        surf->PassThroughPointIdsOn();
        surf->Update();
        surfaceWork->DeepCopy(surf->GetOutput());
        PrepareTetBoundarySurfaceForIoss(volumeGridForIoss, surfaceWork.GetPointer());

        const std::string volumeName = ResolveBlockName(customBlockNames, blockIndex, "tetrahedra");
        AppendPdcBlock(result, blockIndex, volumeGridForIoss, volumeName.c_str(), nextEntityId++);
      }
    }
  }
  else if (vtkPolyData* pd = vtkPolyData::SafeDownCast(input))
  {
    surfaceWork->DeepCopy(pd);
  }
  else
  {
    vtkNew<vtkDataSetSurfaceFilter> surf;
    surf->SetInputData(input);
    surf->Update();
    surfaceWork->DeepCopy(surf->GetOutput());
  }

  std::vector<vtkSmartPointer<vtkPolyData>> sidePieces;
  const char* partName = this->PartitionPointArrayName;
  vtkDataArray* partArr = nullptr;
  if (partName && partName[0] != '\0')
  {
    partArr = vtkDataArray::SafeDownCast(surfaceWork->GetPointData()->GetAbstractArray(partName));
    if (!partArr || partArr->GetNumberOfTuples() != surfaceWork->GetNumberOfPoints() ||
      partArr->GetNumberOfComponents() < 1)
    {
      vtkWarningMacro(<< "Point data array \"" << partName << "\" is missing, not numeric, or has length "
                      << (partArr ? partArr->GetNumberOfTuples() : static_cast<vtkIdType>(-1))
                      << " != number of points " << surfaceWork->GetNumberOfPoints()
                      << ". Using feature-angle / connectivity split instead.");
      partArr = nullptr;
    }
  }
  if (partArr)
  {
    BuildSidePiecesSignThreshold(surfaceWork.GetPointer(), partName, &sidePieces);
    if (sidePieces.empty())
    {
      vtkWarningMacro(<< "Split threshold (<=" << kPartitionLeZeroInclusive << " merged, >="
                      << kPartitionGeOneInclusive
                      << " connectivity) produced no patches; using "
                         "feature-angle / connectivity split instead.");
      CollectCuspConnectedSurfacePieces(surfaceWork.GetPointer(), this->FeatureAngle, &sidePieces);
    }
  }
  else
  {
    CollectCuspConnectedSurfacePieces(surfaceWork.GetPointer(), this->FeatureAngle, &sidePieces);
  }
  if (this->SortByArea)
  {
    SortSidePiecesByAreaDescending(&sidePieces);
  }
  if (this->CustomPostReorder)
  {
    ApplyCustomPostReorder(&sidePieces);
  }

  const unsigned int nPairs = static_cast<unsigned int>(sidePieces.size());
  vtkIdType nextStandaloneSurfacePointGid = 1;
  const size_t nBoundaryVariables = CountBoundaryVariables(boundaryVariables);
  const std::vector<std::string> boundaryVariableArrayNames =
    BoundaryVariableArrayNames(nBoundaryVariables);
  const bool writeVolumeNormals = volumeGridForIoss &&
    HasNonZeroBoundaryVariable(boundaryVariables, volumeGridForIoss != nullptr, nPairs);
  if (writeVolumeNormals)
  {
    InitializeVolumeBoundaryRadialNormalArray(volumeGridForIoss, kBoundaryRadialNormalArrayName);
    InitializeVolumeBoundaryVariableArrays(volumeGridForIoss, boundaryVariableArrayNames);
  }
  std::vector<unsigned int> volumeNormalWriteCounts;
  if (writeVolumeNormals)
  {
    volumeNormalWriteCounts.assign(static_cast<size_t>(volumeGridForIoss->GetNumberOfPoints()), 0);
  }

  std::vector<vtkSmartPointer<vtkPolyData>> preparedSides;
  std::vector<vtkSmartPointer<vtkPolyData>> preparedNodes;
  preparedSides.reserve(nPairs);
  preparedNodes.reserve(nPairs);

  for (unsigned int i = 0; i < nPairs; ++i)
  {
    vtkPolyData* sidePatch = sidePieces[i];

    vtkNew<vtkPolyData> sideCopy;
    sideCopy->DeepCopy(sidePatch);
    if (volumeGridForIoss)
    {
      RestoreSurfacePointGlobalIdsFromVolume(sideCopy, volumeGridForIoss);
    }
    else
    {
      AssignStandalonePatchPointGlobalIds(sideCopy, nextStandaloneSurfacePointGid);
    }
    AddBoundaryRadialValueArray(
      sideCopy, kBoundaryRadialValueArrayName, this->BoundaryRadialNormalFalloffFactor);
    if (writeVolumeNormals)
    {
      const unsigned int sideBlockIndex = 1u + nPairs + i;
      const std::vector<double> variables =
        ResolveLineDoubles(boundaryVariables, sideBlockIndex, nBoundaryVariables);
      if (!HasNonZeroBoundaryVariableRow(variables))
      {
        SetContiguousCellGlobalIdsPolyData(sideCopy);
        vtkSmartPointer<vtkPolyData> nodePd = BuildNodeSetPolyData(sideCopy);
        preparedSides.emplace_back(sideCopy.GetPointer());
        preparedNodes.emplace_back(nodePd);
        continue;
      }
      const vtkIdType duplicateWrites = AccumulateBoundaryRadialNormalToVolume(sideCopy,
        volumeGridForIoss, kBoundaryRadialValueArrayName, kBoundaryRadialNormalArrayName,
        boundaryVariableArrayNames, variables, this->ComputeBoundaryRadialValue != 0,
        volumeNormalWriteCounts);
      if (duplicateWrites > 0)
      {
        vtkWarningMacro(<< "Averaging " << duplicateWrites
                        << " repeated BoundaryRadialValueNormal point writes for side" << i << ".");
      }
    }
    SetContiguousCellGlobalIdsPolyData(sideCopy);

    vtkSmartPointer<vtkPolyData> nodePd = BuildNodeSetPolyData(sideCopy);
    preparedSides.emplace_back(sideCopy.GetPointer());
    preparedNodes.emplace_back(nodePd);
  }

  if (writeVolumeNormals)
  {
    AverageRepeatedBoundaryRadialNormalWrites(volumeGridForIoss, kBoundaryRadialNormalArrayName,
      boundaryVariableArrayNames, volumeNormalWriteCounts);
  }

  for (unsigned int i = 0; i < nPairs; ++i)
  {
    const std::string nodeName =
      ResolveBlockName(customBlockNames, blockIndex, "node" + std::to_string(i));
    AppendPdcBlock(result, blockIndex, preparedNodes[i], nodeName.c_str(), nextEntityId++);
  }
  for (unsigned int i = 0; i < nPairs; ++i)
  {
    const std::string sideName =
      ResolveBlockName(customBlockNames, blockIndex, "side" + std::to_string(i));
    AppendPdcBlock(result, blockIndex, preparedSides[i], sideName.c_str(), nextEntityId++);
  }

  BuildIossAssembly(result, volumeGridForIoss != nullptr, nPairs, customBlockNames);

  output->ShallowCopy(result);

  return 1;
}

VTK_ABI_NAMESPACE_END
