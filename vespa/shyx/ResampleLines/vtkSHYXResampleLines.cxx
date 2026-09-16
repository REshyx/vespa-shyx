#include "vtkSHYXResampleLines.h"

#include "vtkAbstractArray.h"
#include "vtkBoundingBox.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCellType.h"
#include "vtkCleanPolyData.h"
#include "vtkDataObject.h"
#include "vtkFieldData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkSmartPointer.h"
#include "vtkStaticCellLocator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXResampleLines);

namespace
{
using EdgeKey = std::pair<vtkIdType, vtkIdType>;

EdgeKey MakeEdge(vtkIdType a, vtkIdType b)
{
  return a < b ? EdgeKey{ a, b } : EdgeKey{ b, a };
}

double LongestBBoxSide(vtkPolyData* pd)
{
  double b[6];
  pd->GetBounds(b);
  vtkBoundingBox box;
  box.SetBounds(b);
  return box.GetMaxLength();
}

double PointDistance(vtkPoints* pts, vtkIdType a, vtkIdType b)
{
  double pa[3], pb[3];
  pts->GetPoint(a, pa);
  pts->GetPoint(b, pb);
  return std::sqrt(vtkMath::Distance2BetweenPoints(pa, pb));
}

struct Branch
{
  std::vector<vtkIdType> ids;
  bool closed = false;
};

void AddUndirectedEdge(std::vector<std::vector<vtkIdType>>& adj, std::set<EdgeKey>& edges, vtkIdType a,
  vtkIdType b)
{
  if (a == b)
  {
    return;
  }
  if (!edges.insert(MakeEdge(a, b)).second)
  {
    return;
  }
  adj[static_cast<size_t>(a)].push_back(b);
  adj[static_cast<size_t>(b)].push_back(a);
}

void BuildLineGraph(vtkPolyData* pd, std::vector<std::vector<vtkIdType>>& adj, std::set<EdgeKey>& edges)
{
  const vtkIdType nPts = pd->GetNumberOfPoints();
  adj.assign(static_cast<size_t>(nPts), {});
  edges.clear();
  pd->BuildCells();
  const vtkIdType nCells = pd->GetNumberOfCells();
  for (vtkIdType c = 0; c < nCells; ++c)
  {
    const int type = pd->GetCellType(c);
    if (type != VTK_LINE && type != VTK_POLY_LINE)
    {
      continue;
    }
    vtkIdType npts = 0;
    const vtkIdType* pts = nullptr;
    pd->GetCellPoints(c, npts, pts);
    for (vtkIdType i = 0; i + 1 < npts; ++i)
    {
      AddUndirectedEdge(adj, edges, pts[i], pts[i + 1]);
    }
  }
}

vtkIdType OtherNeighbor(const std::vector<vtkIdType>& nbrs, vtkIdType prev)
{
  for (vtkIdType nb : nbrs)
  {
    if (nb != prev)
    {
      return nb;
    }
  }
  return -1;
}

void WalkOpenBranch(vtkIdType start, vtkIdType first, const std::vector<std::vector<vtkIdType>>& adj,
  const std::vector<int>& degree, std::set<EdgeKey>& used, Branch* branch)
{
  branch->closed = false;
  branch->ids.clear();
  branch->ids.push_back(start);
  used.insert(MakeEdge(start, first));
  vtkIdType prev = start;
  vtkIdType cur = first;
  const vtkIdType nPts = static_cast<vtkIdType>(adj.size());
  vtkIdType guard = 0;
  while (guard++ <= nPts)
  {
    branch->ids.push_back(cur);
    if (degree[static_cast<size_t>(cur)] != 2)
    {
      break;
    }
    const vtkIdType next = OtherNeighbor(adj[static_cast<size_t>(cur)], prev);
    if (next < 0)
    {
      break;
    }
    const EdgeKey key = MakeEdge(cur, next);
    if (used.count(key))
    {
      break;
    }
    used.insert(key);
    prev = cur;
    cur = next;
  }
}

void WalkClosedLoop(vtkIdType start, vtkIdType first, const std::vector<std::vector<vtkIdType>>& adj,
  std::set<EdgeKey>& used, Branch* branch)
{
  branch->closed = true;
  branch->ids.clear();
  branch->ids.push_back(start);
  used.insert(MakeEdge(start, first));
  vtkIdType prev = start;
  vtkIdType cur = first;
  const vtkIdType nPts = static_cast<vtkIdType>(adj.size());
  vtkIdType guard = 0;
  while (cur != start && guard++ <= nPts)
  {
    branch->ids.push_back(cur);
    const vtkIdType next = OtherNeighbor(adj[static_cast<size_t>(cur)], prev);
    if (next < 0)
    {
      branch->closed = false;
      break;
    }
    const EdgeKey key = MakeEdge(cur, next);
    if (used.count(key) && next != start)
    {
      branch->closed = false;
      break;
    }
    used.insert(key);
    prev = cur;
    cur = next;
  }
}

void ExtractBranches(const std::vector<std::vector<vtkIdType>>& adj, const std::set<EdgeKey>& edges,
  std::vector<Branch>* branches)
{
  const vtkIdType nPts = static_cast<vtkIdType>(adj.size());
  std::vector<int> degree(static_cast<size_t>(nPts), 0);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    degree[static_cast<size_t>(i)] = static_cast<int>(adj[static_cast<size_t>(i)].size());
  }

  std::set<EdgeKey> used;
  branches->clear();

  for (vtkIdType v = 0; v < nPts; ++v)
  {
    if (degree[static_cast<size_t>(v)] == 0 || degree[static_cast<size_t>(v)] == 2)
    {
      continue;
    }
    for (vtkIdType nb : adj[static_cast<size_t>(v)])
    {
      if (used.count(MakeEdge(v, nb)))
      {
        continue;
      }
      Branch br;
      WalkOpenBranch(v, nb, adj, degree, used, &br);
      if (br.ids.size() >= 2)
      {
        branches->push_back(std::move(br));
      }
    }
  }

  for (const EdgeKey& e : edges)
  {
    if (used.count(e))
    {
      continue;
    }
    Branch br;
    WalkClosedLoop(e.first, e.second, adj, used, &br);
    if (br.ids.size() >= 2)
    {
      branches->push_back(std::move(br));
    }
  }
}

void AccumulateArc(vtkPoints* pts, const Branch& br, std::vector<double>* s)
{
  const size_t n = br.ids.size();
  s->assign(br.closed ? n + 1 : n, 0.0);
  for (size_t i = 1; i < n; ++i)
  {
    (*s)[i] = (*s)[i - 1] + PointDistance(pts, br.ids[i - 1], br.ids[i]);
  }
  if (br.closed)
  {
    (*s)[n] = (*s)[n - 1] + PointDistance(pts, br.ids[n - 1], br.ids[0]);
  }
}

void InterpolateLocation(vtkPoints* pts, const Branch& br, const std::vector<double>& s, double t,
  double outP[3], vtkIdType* id0, vtkIdType* id1, double* alpha)
{
  const size_t nSeg = s.size() - 1;
  size_t i = 0;
  while (i + 1 < nSeg && s[i + 1] < t)
  {
    ++i;
  }
  const double seg = s[i + 1] - s[i];
  *id0 = br.ids[i];
  *id1 = (i + 1 == br.ids.size()) ? br.ids[0] : br.ids[i + 1];
  *alpha = (seg > 0.0) ? (t - s[i]) / seg : 0.0;
  *alpha = std::max(0.0, std::min(1.0, *alpha));
  double p0[3], p1[3];
  pts->GetPoint(*id0, p0);
  pts->GetPoint(*id1, p1);
  outP[0] = p0[0] + (*alpha) * (p1[0] - p0[0]);
  outP[1] = p0[1] + (*alpha) * (p1[1] - p0[1]);
  outP[2] = p0[2] + (*alpha) * (p1[2] - p0[2]);
}

vtkIdType MapFeature(vtkPoints* inPts, vtkPointData* inPD, vtkPoints* outPts, vtkPointData* outPD,
  std::vector<vtkIdType>& featureOut, vtkIdType inId)
{
  const size_t idx = static_cast<size_t>(inId);
  if (featureOut[idx] >= 0)
  {
    return featureOut[idx];
  }
  double p[3];
  inPts->GetPoint(inId, p);
  const vtkIdType outId = outPts->InsertNextPoint(p);
  outPD->CopyData(inPD, inId, outId);
  featureOut[idx] = outId;
  return outId;
}

void AppendPolyline(vtkCellArray* lines, const std::vector<vtkIdType>& ids)
{
  if (ids.size() < 2)
  {
    return;
  }
  lines->InsertNextCell(static_cast<vtkIdType>(ids.size()), ids.data());
}

void ResampleBranch(vtkPoints* inPts, vtkPointData* inPD, vtkPoints* outPts, vtkPointData* outPD,
  vtkCellArray* outLines, std::vector<vtkIdType>& featureOut, const Branch& br, double sampleDistance)
{
  std::vector<double> s;
  AccumulateArc(inPts, br, &s);
  const double length = s.empty() ? 0.0 : s.back();
  if (length <= 0.0)
  {
    return;
  }

  std::vector<vtkIdType> cellIds;
  cellIds.reserve(16);

  if (br.closed)
  {
    if (length <= sampleDistance)
    {
      for (vtkIdType id : br.ids)
      {
        cellIds.push_back(MapFeature(inPts, inPD, outPts, outPD, featureOut, id));
      }
      if (!cellIds.empty())
      {
        cellIds.push_back(cellIds.front());
      }
      AppendPolyline(outLines, cellIds);
      return;
    }

    cellIds.push_back(MapFeature(inPts, inPD, outPts, outPD, featureOut, br.ids.front()));
    const double endSkip = std::max(1e-12 * length, 1e-15);
    const int nStep = static_cast<int>(std::floor((length - endSkip) / sampleDistance));
    for (int k = 1; k <= nStep; ++k)
    {
      const double t = static_cast<double>(k) * sampleDistance;
      if (t >= length - endSkip)
      {
        break;
      }
      double p[3];
      vtkIdType id0 = 0, id1 = 0;
      double alpha = 0.0;
      InterpolateLocation(inPts, br, s, t, p, &id0, &id1, &alpha);
      const vtkIdType outId = outPts->InsertNextPoint(p);
      outPD->InterpolateEdge(inPD, outId, id0, id1, alpha);
      cellIds.push_back(outId);
    }
    cellIds.push_back(cellIds.front());
    AppendPolyline(outLines, cellIds);
    return;
  }

  cellIds.push_back(MapFeature(inPts, inPD, outPts, outPD, featureOut, br.ids.front()));
  if (length > sampleDistance)
  {
    const double endSkip = std::max(1e-12 * length, 1e-15);
    const int nStep = static_cast<int>(std::floor((length - endSkip) / sampleDistance));
    for (int k = 1; k <= nStep; ++k)
    {
      const double t = static_cast<double>(k) * sampleDistance;
      if (t >= length - endSkip)
      {
        break;
      }
      double p[3];
      vtkIdType id0 = 0, id1 = 0;
      double alpha = 0.0;
      InterpolateLocation(inPts, br, s, t, p, &id0, &id1, &alpha);
      const vtkIdType outId = outPts->InsertNextPoint(p);
      outPD->InterpolateEdge(inPD, outId, id0, id1, alpha);
      cellIds.push_back(outId);
    }
  }
  cellIds.push_back(MapFeature(inPts, inPD, outPts, outPD, featureOut, br.ids.back()));
  AppendPolyline(outLines, cellIds);
}

double Dist2ToSegment(
  const double x[3], const double a[3], const double b[3], double closest[3], double* t)
{
  const double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
  const double ax[3] = { x[0] - a[0], x[1] - a[1], x[2] - a[2] };
  const double ab2 = vtkMath::Dot(ab, ab);
  if (ab2 <= 0.0)
  {
    *t = 0.0;
    closest[0] = a[0];
    closest[1] = a[1];
    closest[2] = a[2];
    return vtkMath::Distance2BetweenPoints(x, a);
  }
  *t = vtkMath::Dot(ax, ab) / ab2;
  *t = std::max(0.0, std::min(1.0, *t));
  closest[0] = a[0] + (*t) * ab[0];
  closest[1] = a[1] + (*t) * ab[1];
  closest[2] = a[2] + (*t) * ab[2];
  return vtkMath::Distance2BetweenPoints(x, closest);
}

struct SegHit
{
  size_t poly = 0;
  size_t seg = 0;
  vtkIdType id0 = -1;
  vtkIdType id1 = -1;
  double t = 0.0;
  double closest[3] = { 0.0, 0.0, 0.0 };
  double dist2 = VTK_DOUBLE_MAX;
};

struct LineMergeState
{
  vtkPoints* pts = nullptr;
  vtkPointData* pd = nullptr;
  std::vector<std::vector<vtkIdType>> polys;
  std::vector<vtkIdType> srcCells;
  size_t nSnap = 0;
};

void GrowPointData(vtkPointData* pd, vtkIdType newId)
{
  if (!pd)
  {
    return;
  }
  const int n = pd->GetNumberOfArrays();
  for (int i = 0; i < n; ++i)
  {
    vtkAbstractArray* arr = pd->GetAbstractArray(i);
    if (arr && arr->GetNumberOfTuples() <= newId)
    {
      arr->SetNumberOfTuples(newId + 1);
    }
  }
}

bool SegmentBoxMayBeCloserThan(
  const double p[3], const double a[3], const double b[3], double dist2Limit)
{
  double d2 = 0.0;
  for (int i = 0; i < 3; ++i)
  {
    const double lo = std::min(a[i], b[i]);
    const double hi = std::max(a[i], b[i]);
    if (p[i] < lo)
    {
      const double d = lo - p[i];
      d2 += d * d;
    }
    else if (p[i] > hi)
    {
      const double d = p[i] - hi;
      d2 += d * d;
    }
    if (d2 > dist2Limit)
    {
      return false;
    }
  }
  return true;
}

size_t CountAcceptedSegments(const LineMergeState& st)
{
  size_t n = 0;
  const size_t nPolys = std::min(st.nSnap, st.polys.size());
  for (size_t pi = 0; pi < nPolys; ++pi)
  {
    const size_t np = st.polys[pi].size();
    if (np >= 2)
    {
      n += np - 1;
    }
  }
  return n;
}

SegHit FindClosestOnAccepted(const double p[3], const LineMergeState& st)
{
  SegHit best;
  const size_t nPolys = std::min(st.nSnap, st.polys.size());
  for (size_t pi = 0; pi < nPolys; ++pi)
  {
    const std::vector<vtkIdType>& ids = st.polys[pi];
    if (ids.size() < 2)
    {
      continue;
    }
    for (size_t s = 0; s + 1 < ids.size(); ++s)
    {
      double a[3], b[3], c[3];
      st.pts->GetPoint(ids[s], a);
      st.pts->GetPoint(ids[s + 1], b);
      if (!SegmentBoxMayBeCloserThan(p, a, b, best.dist2))
      {
        continue;
      }
      double t = 0.0;
      const double d2 = Dist2ToSegment(p, a, b, c, &t);
      if (d2 < best.dist2)
      {
        best.dist2 = d2;
        best.poly = pi;
        best.seg = s;
        best.id0 = ids[s];
        best.id1 = ids[s + 1];
        best.t = t;
        best.closest[0] = c[0];
        best.closest[1] = c[1];
        best.closest[2] = c[2];
      }
    }
  }
  return best;
}

bool PointOnAccepted(const double p[3], const LineMergeState& st, double tol2)
{
  const size_t nPolys = std::min(st.nSnap, st.polys.size());
  for (size_t pi = 0; pi < nPolys; ++pi)
  {
    const std::vector<vtkIdType>& ids = st.polys[pi];
    if (ids.size() < 2)
    {
      continue;
    }
    for (size_t s = 0; s + 1 < ids.size(); ++s)
    {
      double a[3], b[3], c[3];
      st.pts->GetPoint(ids[s], a);
      st.pts->GetPoint(ids[s + 1], b);
      if (!SegmentBoxMayBeCloserThan(p, a, b, tol2))
      {
        continue;
      }
      double t = 0.0;
      if (Dist2ToSegment(p, a, b, c, &t) <= tol2)
      {
        return true;
      }
    }
  }
  return false;
}

vtkSmartPointer<vtkStaticCellLocator> BuildAcceptedLocator(const LineMergeState& st, vtkPolyData* locPd)
{
  vtkNew<vtkCellArray> lines;
  const size_t nPolys = std::min(st.nSnap, st.polys.size());
  for (size_t pi = 0; pi < nPolys; ++pi)
  {
    const std::vector<vtkIdType>& ids = st.polys[pi];
    if (ids.size() < 2)
    {
      continue;
    }
    lines->InsertNextCell(static_cast<vtkIdType>(ids.size()), ids.data());
  }
  locPd->Initialize();
  locPd->SetPoints(st.pts);
  locPd->SetLines(lines);
  locPd->BuildCells();
  vtkSmartPointer<vtkStaticCellLocator> loc = vtkSmartPointer<vtkStaticCellLocator>::New();
  loc->SetDataSet(locPd);
  loc->BuildLocator();
  return loc;
}

bool LocatorPointOnAccepted(vtkStaticCellLocator* loc, const double p[3], double radius)
{
  if (!loc || radius < 0.0)
  {
    return false;
  }
  double q[3] = { p[0], p[1], p[2] };
  double closest[3];
  vtkIdType cellId = -1;
  int subId = 0;
  double dist2 = VTK_DOUBLE_MAX;
  const vtkIdType hit = loc->FindClosestPointWithinRadius(q, radius, closest, cellId, subId, dist2);
  return hit >= 0 && cellId >= 0 && dist2 <= radius * radius;
}

vtkIdType SnapOntoAccepted(const double p[3], LineMergeState& st)
{
  const SegHit hit = FindClosestOnAccepted(p, st);
  if (hit.id0 < 0 || hit.id1 < 0)
  {
    const vtkIdType id = st.pts->InsertNextPoint(p);
    GrowPointData(st.pd, id);
    return id;
  }
  constexpr double tEps = 1e-8;
  if (hit.t <= tEps)
  {
    return hit.id0;
  }
  if (hit.t >= 1.0 - tEps)
  {
    return hit.id1;
  }
  const vtkIdType newId = st.pts->InsertNextPoint(hit.closest);
  GrowPointData(st.pd, newId);
  if (st.pd)
  {
    st.pd->InterpolateEdge(st.pd, newId, hit.id0, hit.id1, hit.t);
  }
  st.polys[hit.poly].insert(st.polys[hit.poly].begin() + (hit.seg + 1), newId);
  return newId;
}

void AppendUniqueId(std::vector<vtkIdType>& ids, vtkIdType id)
{
  if (ids.empty() || ids.back() != id)
  {
    ids.push_back(id);
  }
}

void AcceptBranch(LineMergeState& st, std::vector<vtkIdType>&& branch, vtkIdType srcCell)
{
  if (branch.size() < 2)
  {
    return;
  }
  st.polys.push_back(std::move(branch));
  st.srcCells.push_back(srcCell);
}

void FillIsolatedOffPoints(std::vector<char>& mask, bool closed)
{
  const int n = static_cast<int>(mask.size());
  if (n < 3)
  {
    return;
  }
  std::vector<char> next = mask;
  for (int i = 0; i < n; ++i)
  {
    if (mask[static_cast<size_t>(i)])
    {
      continue;
    }
    int prev = i - 1;
    int nxt = i + 1;
    if (closed)
    {
      prev = (i + n - 1) % n;
      nxt = (i + 1) % n;
    }
    else if (i == 0 || i == n - 1)
    {
      continue;
    }
    if (mask[static_cast<size_t>(prev)] && mask[static_cast<size_t>(nxt)])
    {
      next[static_cast<size_t>(i)] = 1;
    }
  }
  mask.swap(next);
}

bool MaskAllOn(const std::vector<char>& mask)
{
  return std::all_of(mask.begin(), mask.end(), [](char c) { return c != 0; });
}

bool MaskAllOff(const std::vector<char>& mask)
{
  return std::none_of(mask.begin(), mask.end(), [](char c) { return c != 0; });
}

void EmitSnappedBranch(LineMergeState& st, const std::vector<vtkIdType>& uid, int start, int count,
  int nUnique, bool wrap, bool snapLeft, bool snapRight, vtkIdType srcCell)
{
  std::vector<vtkIdType> branch;
  auto pointOf = [&](int idx) {
    double p[3];
    st.pts->GetPoint(uid[static_cast<size_t>(idx)], p);
    return SnapOntoAccepted(p, st);
  };
  if (snapLeft)
  {
    const int left = wrap ? (start + nUnique - 1) % nUnique : start - 1;
    AppendUniqueId(branch, pointOf(left));
  }
  for (int c = 0; c < count; ++c)
  {
    const int idx = wrap ? (start + c) % nUnique : start + c;
    AppendUniqueId(branch, uid[static_cast<size_t>(idx)]);
  }
  if (snapRight)
  {
    const int right = wrap ? (start + count) % nUnique : start + count;
    AppendUniqueId(branch, pointOf(right));
  }
  AcceptBranch(st, std::move(branch), srcCell);
}

void CollectFreeRuns(LineMergeState& st, const std::vector<vtkIdType>& uid, const std::vector<char>& mask,
  bool closed, vtkIdType srcCell)
{
  const int n = static_cast<int>(uid.size());
  if (n < 1)
  {
    return;
  }
  if (MaskAllOn(mask))
  {
    return;
  }
  if (MaskAllOff(mask))
  {
    std::vector<vtkIdType> branch = uid;
    if (closed && branch.size() >= 2)
    {
      AppendUniqueId(branch, branch.front());
    }
    AcceptBranch(st, std::move(branch), srcCell);
    return;
  }

  if (closed)
  {
    int origin = 0;
    while (origin < n && !mask[static_cast<size_t>(origin)])
    {
      ++origin;
    }
    int k = 0;
    while (k < n)
    {
      const int idx = (origin + k) % n;
      if (mask[static_cast<size_t>(idx)])
      {
        ++k;
        continue;
      }
      const int start = idx;
      int count = 0;
      while (k < n && !mask[static_cast<size_t>((origin + k) % n)])
      {
        ++count;
        ++k;
      }
      EmitSnappedBranch(st, uid, start, count, n, true, true, true, srcCell);
    }
    return;
  }

  int i = 0;
  while (i < n)
  {
    if (mask[static_cast<size_t>(i)])
    {
      ++i;
      continue;
    }
    const int start = i;
    while (i < n && !mask[static_cast<size_t>(i)])
    {
      ++i;
    }
    EmitSnappedBranch(st, uid, start, i - start, n, false, start > 0, i < n, srcCell);
  }
}

vtkSmartPointer<vtkPolyData> EmitMergedPolylines(const LineMergeState& st, vtkCellData* inCD)
{
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  vtkNew<vtkPoints> outPts;
  outPts->SetDataType(st.pts ? st.pts->GetDataType() : VTK_DOUBLE);
  vtkNew<vtkCellArray> outLines;
  const vtkIdType nWork = st.pts ? st.pts->GetNumberOfPoints() : 0;
  std::vector<vtkIdType> remap(static_cast<size_t>(std::max<vtkIdType>(nWork, 0)), -1);
  out->SetPoints(outPts);
  out->SetLines(outLines);
  vtkPointData* outPD = out->GetPointData();
  if (st.pd)
  {
    outPD->InterpolateAllocate(st.pd, nWork);
  }
  const vtkIdType nOutGuess = static_cast<vtkIdType>(st.polys.size());
  if (inCD)
  {
    out->GetCellData()->CopyAllocate(inCD, nOutGuess);
  }

  vtkIdType oc = 0;
  for (size_t i = 0; i < st.polys.size(); ++i)
  {
    const std::vector<vtkIdType>& ids = st.polys[i];
    if (ids.size() < 2)
    {
      continue;
    }
    std::vector<vtkIdType> nid;
    nid.reserve(ids.size());
    for (vtkIdType id : ids)
    {
      if (id < 0 || id >= nWork)
      {
        continue;
      }
      const size_t idx = static_cast<size_t>(id);
      if (remap[idx] < 0)
      {
        double q[3];
        st.pts->GetPoint(id, q);
        remap[idx] = outPts->InsertNextPoint(q);
        if (st.pd)
        {
          outPD->CopyData(st.pd, id, remap[idx]);
        }
      }
      AppendUniqueId(nid, remap[idx]);
    }
    if (nid.size() < 2)
    {
      continue;
    }
    outLines->InsertNextCell(static_cast<vtkIdType>(nid.size()), nid.data());
    if (inCD && i < st.srcCells.size())
    {
      out->GetCellData()->CopyData(inCD, st.srcCells[i], oc);
    }
    ++oc;
  }
  outPts->Squeeze();
  outLines->Squeeze();
  outPD->Squeeze();
  out->GetCellData()->Squeeze();
  return out;
}

void MergeOnePolyline(
  LineMergeState& st, const std::vector<vtkIdType>& ids, vtkIdType srcCell, double tolerance)
{
  if (ids.size() < 2)
  {
    return;
  }
  if (st.polys.empty())
  {
    st.polys.push_back(ids);
    st.srcCells.push_back(srcCell);
    st.nSnap = st.polys.size();
    return;
  }

  st.nSnap = st.polys.size();
  const bool closed = ids.size() >= 3 && ids.front() == ids.back();
  const int nUnique = closed ? static_cast<int>(ids.size()) - 1 : static_cast<int>(ids.size());
  if (nUnique < 1)
  {
    return;
  }

  const double tol2 = tolerance * tolerance;
  vtkNew<vtkPolyData> locPd;
  vtkSmartPointer<vtkStaticCellLocator> loc;
  constexpr size_t kLocatorMinSegments = 32;
  if (tolerance > 0.0 && CountAcceptedSegments(st) >= kLocatorMinSegments)
  {
    loc = BuildAcceptedLocator(st, locPd);
  }

  std::vector<vtkIdType> uid(ids.begin(), ids.begin() + nUnique);
  std::vector<char> mask(static_cast<size_t>(nUnique), 0);
  for (int i = 0; i < nUnique; ++i)
  {
    double p[3];
    st.pts->GetPoint(uid[static_cast<size_t>(i)], p);
    const bool on = loc ? LocatorPointOnAccepted(loc, p, tolerance) : PointOnAccepted(p, st, tol2);
    mask[static_cast<size_t>(i)] = on ? 1 : 0;
  }
  FillIsolatedOffPoints(mask, closed);
  CollectFreeRuns(st, uid, mask, closed, srcCell);
}

struct LineNetwork
{
  std::vector<std::vector<vtkIdType>> polys;
  std::vector<vtkIdType> srcCells;
};

LineNetwork MergeNetworks(LineNetwork&& left, LineNetwork&& right, vtkPoints* pts, vtkPointData* pd,
  double tolerance)
{
  if (left.polys.empty())
  {
    return std::move(right);
  }
  if (right.polys.empty())
  {
    return std::move(left);
  }

  LineMergeState st;
  st.pts = pts;
  st.pd = pd;
  st.polys = std::move(left.polys);
  st.srcCells = std::move(left.srcCells);
  for (size_t i = 0; i < right.polys.size(); ++i)
  {
    const vtkIdType src = i < right.srcCells.size() ? right.srcCells[i] : -1;
    MergeOnePolyline(st, right.polys[i], src, tolerance);
  }

  LineNetwork out;
  out.polys = std::move(st.polys);
  out.srcCells = std::move(st.srcCells);
  return out;
}

vtkSmartPointer<vtkPolyData> MergeLaterPolylinesOntoEarlier(vtkPolyData* input, double tolerance)
{
  vtkSmartPointer<vtkPolyData> empty = vtkSmartPointer<vtkPolyData>::New();
  if (!input || !input->GetPoints())
  {
    return empty;
  }

  vtkNew<vtkPoints> workPts;
  workPts->DeepCopy(input->GetPoints());
  vtkNew<vtkPointData> workPD;
  workPD->DeepCopy(input->GetPointData());

  std::vector<LineNetwork> level;
  input->BuildCells();
  const vtkIdType nCells = input->GetNumberOfCells();
  for (vtkIdType c = 0; c < nCells; ++c)
  {
    const int type = input->GetCellType(c);
    if (type != VTK_LINE && type != VTK_POLY_LINE)
    {
      continue;
    }
    vtkIdType npts = 0;
    const vtkIdType* pts = nullptr;
    input->GetCellPoints(c, npts, pts);
    if (npts < 2 || !pts)
    {
      continue;
    }
    LineNetwork leaf;
    leaf.polys.emplace_back(pts, pts + npts);
    leaf.srcCells.push_back(c);
    level.push_back(std::move(leaf));
  }

  while (level.size() > 1)
  {
    std::vector<LineNetwork> next;
    next.reserve((level.size() + 1) / 2);
    for (size_t i = 0; i < level.size(); i += 2)
    {
      if (i + 1 >= level.size())
      {
        next.push_back(std::move(level[i]));
      }
      else
      {
        next.push_back(MergeNetworks(
          std::move(level[i]), std::move(level[i + 1]), workPts, workPD, tolerance));
      }
    }
    level.swap(next);
  }

  LineMergeState st;
  st.pts = workPts;
  st.pd = workPD;
  if (!level.empty())
  {
    st.polys = std::move(level[0].polys);
    st.srcCells = std::move(level[0].srcCells);
  }
  st.nSnap = st.polys.size();

  vtkSmartPointer<vtkPolyData> out = EmitMergedPolylines(st, input->GetCellData());
  out->GetFieldData()->PassData(input->GetFieldData());
  return out;
}
} // namespace

vtkSHYXResampleLines::vtkSHYXResampleLines()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

void vtkSHYXResampleLines::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "LineMerge: " << this->LineMerge << "\n";
  os << indent << "LineMergeTolerance: " << this->LineMergeTolerance << "\n";
  os << indent << "Fuse: " << this->Fuse << "\n";
  os << indent << "FuseTolerance: " << this->FuseTolerance << "\n";
  os << indent << "Sample: " << this->Sample << "\n";
  os << indent << "SampleDistance: " << this->SampleDistance << "\n";
}

int vtkSHYXResampleLines::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXResampleLines::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXResampleLines::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Null input or output.");
    return 0;
  }

  output->Initialize();
  if (input->GetNumberOfPoints() == 0)
  {
    return 1;
  }

  if (!this->LineMerge && !this->Fuse && !this->Sample)
  {
    vtkWarningMacro(<< "LineMerge, Fuse and Sample are all off; passing input through.");
    output->ShallowCopy(input);
    return 1;
  }

  const double bboxMax = LongestBBoxSide(input);
  if (bboxMax <= 0.0)
  {
    vtkWarningMacro(<< "Input has zero bounding-box extent.");
  }

  vtkSmartPointer<vtkPolyData> mergedHolder;
  vtkPolyData* lines = input;
  if (this->LineMerge)
  {
    double mergeTol = this->LineMergeTolerance;
    if (mergeTol <= 0.0)
    {
      mergeTol = 1e-4 * std::max(bboxMax, 0.0);
    }
    mergedHolder = MergeLaterPolylinesOntoEarlier(input, mergeTol);
    if (!mergedHolder)
    {
      vtkErrorMacro(<< "Line merge produced a null dataset.");
      return 0;
    }
    lines = mergedHolder;
  }

  vtkNew<vtkCleanPolyData> cleaner;
  if (this->Fuse)
  {
    double fuseTol = this->FuseTolerance;
    if (fuseTol <= 0.0)
    {
      fuseTol = 1e-6 * std::max(bboxMax, 0.0);
    }
    cleaner->SetInputData(lines);
    cleaner->PointMergingOn();
    cleaner->ConvertLinesToPointsOff();
    cleaner->ConvertPolysToLinesOff();
    cleaner->ConvertStripsToPolysOff();
    cleaner->SetToleranceIsAbsolute(1);
    cleaner->SetAbsoluteTolerance(fuseTol);
    cleaner->Update();
    lines = cleaner->GetOutput();
    if (!lines)
    {
      vtkErrorMacro(<< "Fuse produced a null dataset.");
      return 0;
    }
  }

  if (lines->GetNumberOfPoints() == 0)
  {
    output->GetFieldData()->PassData(input->GetFieldData());
    return 1;
  }

  if (!this->Sample)
  {
    output->ShallowCopy(lines);
    output->GetFieldData()->PassData(input->GetFieldData());
    return 1;
  }

  double sampleDistance = this->SampleDistance;
  if (sampleDistance <= 0.0)
  {
    sampleDistance = 0.01 * std::max(bboxMax, 0.0);
    vtkWarningMacro(<< "SampleDistance <= 0; using 0.01 * bounding-box longest side (" << sampleDistance
                    << ").");
  }
  if (sampleDistance <= 0.0)
  {
    vtkErrorMacro(<< "SampleDistance is not positive.");
    return 0;
  }

  std::vector<std::vector<vtkIdType>> adj;
  std::set<EdgeKey> graphEdges;
  BuildLineGraph(lines, adj, graphEdges);
  if (graphEdges.empty())
  {
    vtkWarningMacro(<< "Input has no VTK_LINE / VTK_POLY_LINE cells.");
    output->GetFieldData()->PassData(input->GetFieldData());
    return 1;
  }

  std::vector<Branch> branches;
  ExtractBranches(adj, graphEdges, &branches);
  if (branches.empty())
  {
    output->GetFieldData()->PassData(input->GetFieldData());
    return 1;
  }

  vtkNew<vtkPoints> outPts;
  vtkNew<vtkCellArray> outLines;
  outPts->SetDataType(lines->GetPoints() ? lines->GetPoints()->GetDataType() : VTK_DOUBLE);
  const vtkIdType estPts = lines->GetNumberOfPoints();
  outPts->Allocate(estPts);
  output->SetPoints(outPts);
  output->SetLines(outLines);

  vtkPointData* inPD = lines->GetPointData();
  vtkPointData* outPD = output->GetPointData();
  outPD->InterpolateAllocate(inPD, estPts);

  std::vector<vtkIdType> featureOut(static_cast<size_t>(lines->GetNumberOfPoints()), -1);
  vtkPoints* inPts = lines->GetPoints();
  for (const Branch& br : branches)
  {
    ResampleBranch(inPts, inPD, outPts, outPD, outLines, featureOut, br, sampleDistance);
  }

  outPts->Squeeze();
  outLines->Squeeze();
  outPD->Squeeze();
  output->GetFieldData()->PassData(input->GetFieldData());
  return 1;
}

VTK_ABI_NAMESPACE_END
