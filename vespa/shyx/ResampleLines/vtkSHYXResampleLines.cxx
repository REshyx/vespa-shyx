#include "vtkSHYXResampleLines.h"

#include "vtkBoundingBox.h"
#include "vtkCellArray.h"
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

#include <algorithm>
#include <cmath>
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
} // namespace

vtkSHYXResampleLines::vtkSHYXResampleLines()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

void vtkSHYXResampleLines::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Fuse: " << this->Fuse << "\n";
  os << indent << "FuseTolerance: " << this->FuseTolerance << "\n";
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

  const double bboxMax = LongestBBoxSide(input);
  if (bboxMax <= 0.0)
  {
    vtkWarningMacro(<< "Input has zero bounding-box extent.");
  }

  double fuseTol = this->FuseTolerance;
  if (fuseTol <= 0.0)
  {
    fuseTol = 1e-6 * std::max(bboxMax, 0.0);
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

  vtkPolyData* lines = input;
  vtkNew<vtkCleanPolyData> cleaner;
  if (this->Fuse)
  {
    cleaner->SetInputData(input);
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
