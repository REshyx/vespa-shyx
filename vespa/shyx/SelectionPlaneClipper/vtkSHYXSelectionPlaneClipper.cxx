#include "vtkSHYXSelectionPlaneClipper.h"

#include <vtkAbstractArray.h>
#include <vtkAppendPolyData.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCleanPolyData.h>
#include <vtkClipPolyData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkFillHolesFilter.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointLocator.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTriangleFilter.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXSelectionPlaneClipper);

namespace
{
constexpr char kDefaultFillHoleStampArrayName[] = "EndpointIndex";

void SetClipPlaneHintPacked(vtkSHYXSelectionPlaneClipper* self, const double meshBounds[6],
  const double origin[3], const double planeNormal[3])
{
  if (!self)
  {
    return;
  }
  const double dx = meshBounds[1] - meshBounds[0];
  const double dy = meshBounds[3] - meshBounds[2];
  const double dz = meshBounds[5] - meshBounds[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double arrowLen = diag * 0.04;
  const double dh[3] = { origin[0] + arrowLen * planeNormal[0], origin[1] + arrowLen * planeNormal[1],
    origin[2] + arrowLen * planeNormal[2] };

  std::ostringstream oss;
  oss << origin[0] << ' ' << origin[1] << ' ' << origin[2] << ' ' << dh[0] << ' ' << dh[1] << ' ' << dh[2];
  const std::string packed = oss.str();
  self->SetClipPlaneHintPackedString(packed.c_str());
}

void BuildLinksIfNeeded(vtkDataSet* ds)
{
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    pd->BuildLinks();
  }
  else if (auto* ug = vtkUnstructuredGrid::SafeDownCast(ds))
  {
    ug->BuildLinks();
  }
}

vtkIdTypeArray* OriginalPointIds(vtkDataSet* extracted)
{
  if (!extracted)
  {
    return nullptr;
  }
  return vtkIdTypeArray::SafeDownCast(extracted->GetPointData()->GetArray("vtkOriginalPointIds"));
}

void OrientNormalWithHint(double n[3], const double hint[3])
{
  if (vtkMath::Norm(hint) < 1e-15)
  {
    return;
  }
  if (vtkMath::Dot(n, hint) < 0.0)
  {
    n[0] = -n[0];
    n[1] = -n[1];
    n[2] = -n[2];
  }
}

void AppendCellPointSamples(vtkDataSet* ds, vtkCell* cell, std::vector<std::array<double, 3>>& pts)
{
  if (!ds || !cell)
  {
    return;
  }
  const int npts = cell->GetNumberOfPoints();
  for (int i = 0; i < npts; ++i)
  {
    double x[3];
    ds->GetPoint(cell->GetPointId(i), x);
    pts.push_back({ x[0], x[1], x[2] });
  }
}

void AppendAllPointSamples(vtkDataSet* ds, std::vector<std::array<double, 3>>& pts)
{
  if (!ds)
  {
    return;
  }
  const vtkIdType np = ds->GetNumberOfPoints();
  pts.reserve(pts.size() + static_cast<size_t>(np));
  for (vtkIdType i = 0; i < np; ++i)
  {
    double x[3];
    ds->GetPoint(i, x);
    pts.push_back({ x[0], x[1], x[2] });
  }
}

bool LineDirectionFromSamples(
  const std::vector<std::array<double, 3>>& pts, const double origin[3], double dir[3])
{
  dir[0] = dir[1] = dir[2] = 0.0;
  double best = 0.0;
  for (const auto& p : pts)
  {
    const double d[3] = { p[0] - origin[0], p[1] - origin[1], p[2] - origin[2] };
    const double nn = vtkMath::Norm(d);
    if (nn > best)
    {
      best = nn;
      dir[0] = d[0];
      dir[1] = d[1];
      dir[2] = d[2];
    }
  }
  return vtkMath::Normalize(dir) > 1e-15;
}

void MakeNormalPerpendicularToDir(double n[3], const double dir[3])
{
  const double d = vtkMath::Dot(n, dir);
  n[0] -= d * dir[0];
  n[1] -= d * dir[1];
  n[2] -= d * dir[2];
  if (vtkMath::Normalize(n) < 1e-15)
  {
    vtkMath::Perpendiculars(dir, n, nullptr, 0.0);
    vtkMath::Normalize(n);
  }
}

/** Covariance PCA. origin is always the centroid when pts is not empty.
 *  Returns true when the samples span a plane (not a single point / line). */
bool FitPlaneFromPointsPCA(
  const std::vector<std::array<double, 3>>& pts, double origin[3], double normal[3])
{
  origin[0] = origin[1] = origin[2] = 0.0;
  normal[0] = 0.0;
  normal[1] = 0.0;
  normal[2] = 1.0;
  const size_t n = pts.size();
  if (n == 0)
  {
    return false;
  }
  for (const auto& p : pts)
  {
    origin[0] += p[0];
    origin[1] += p[1];
    origin[2] += p[2];
  }
  const double inv = 1.0 / static_cast<double>(n);
  origin[0] *= inv;
  origin[1] *= inv;
  origin[2] *= inv;
  if (n < 3)
  {
    return false;
  }

  double cov[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
  for (const auto& p : pts)
  {
    const double d[3] = { p[0] - origin[0], p[1] - origin[1], p[2] - origin[2] };
    for (int i = 0; i < 3; ++i)
    {
      for (int j = 0; j < 3; ++j)
      {
        cov[i][j] += d[i] * d[j];
      }
    }
  }
  double w[3] = { 0.0, 0.0, 0.0 };
  double V[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
  vtkMath::Diagonalize3x3(cov, w, V);

  int iMin = 0;
  int iMax = 0;
  for (int i = 1; i < 3; ++i)
  {
    if (w[i] < w[iMin])
    {
      iMin = i;
    }
    if (w[i] > w[iMax])
    {
      iMax = i;
    }
  }
  int iMid = 0;
  for (int i = 0; i < 3; ++i)
  {
    if (i != iMin && i != iMax)
    {
      iMid = i;
      break;
    }
  }

  const double lMax = std::max(w[iMax], 0.0);
  const double lMid = std::max((iMin == iMax) ? lMax : w[iMid], 0.0);
  if ((lMax < 1e-30) || (lMid < 1e-8 * lMax))
  {
    return false;
  }
  normal[0] = V[0][iMin];
  normal[1] = V[1][iMin];
  normal[2] = V[2][iMin];
  return vtkMath::Normalize(normal) > 1e-15;
}

bool AccumulateSurfaceCellNormal(vtkDataSet* ds, vtkIdType cid, double nAcc[3])
{
  vtkCell* cell = ds->GetCell(cid);
  if (!cell || cell->GetCellDimension() != 2)
  {
    return false;
  }
  const int npts = cell->GetNumberOfPoints();
  if (npts < 3)
  {
    return false;
  }
  double p0[3];
  ds->GetPoint(cell->GetPointId(0), p0);
  bool any = false;
  for (int i = 1; i + 1 < npts; ++i)
  {
    double p1[3];
    double p2[3];
    ds->GetPoint(cell->GetPointId(i), p1);
    ds->GetPoint(cell->GetPointId(i + 1), p2);
    const double e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    const double e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
    double c[3];
    vtkMath::Cross(e1, e2, c);
    if (vtkMath::Norm(c) < 1e-30)
    {
      continue;
    }
    nAcc[0] += c[0];
    nAcc[1] += c[1];
    nAcc[2] += c[2];
    any = true;
  }
  return any;
}

bool AverageIncidentSurfaceNormals(vtkDataSet* original, vtkDataSet* extracted, double n[3])
{
  n[0] = n[1] = n[2] = 0.0;
  if (!original || !extracted || extracted->GetNumberOfPoints() == 0)
  {
    return false;
  }
  BuildLinksIfNeeded(original);
  vtkIdTypeArray* origIds = OriginalPointIds(extracted);
  vtkNew<vtkIdList> cellIds;
  std::set<vtkIdType> seen;
  const vtkIdType np = extracted->GetNumberOfPoints();
  for (vtkIdType i = 0; i < np; ++i)
  {
    vtkIdType pid = -1;
    if (origIds)
    {
      pid = origIds->GetValue(i);
    }
    else
    {
      double x[3];
      extracted->GetPoint(i, x);
      pid = original->FindPoint(x);
    }
    if (pid < 0 || pid >= original->GetNumberOfPoints())
    {
      continue;
    }
    original->GetPointCells(pid, cellIds);
    for (vtkIdType k = 0; k < cellIds->GetNumberOfIds(); ++k)
    {
      const vtkIdType cid = cellIds->GetId(k);
      if (!seen.insert(cid).second)
      {
        continue;
      }
      AccumulateSurfaceCellNormal(original, cid, n);
    }
  }
  return vtkMath::Normalize(n) > 1e-15;
}

bool AverageDatasetPointNormals(vtkDataSet* original, vtkDataSet* extracted, double n[3])
{
  n[0] = n[1] = n[2] = 0.0;
  if (!extracted)
  {
    return false;
  }
  vtkDataArray* on = original ? original->GetPointData()->GetNormals() : nullptr;
  vtkDataArray* en = extracted->GetPointData()->GetNormals();
  vtkIdTypeArray* origIds = OriginalPointIds(extracted);
  const vtkIdType np = extracted->GetNumberOfPoints();
  int used = 0;
  for (vtkIdType i = 0; i < np; ++i)
  {
    double t[3] = { 0.0, 0.0, 0.0 };
    bool ok = false;
    if (on && origIds)
    {
      const vtkIdType pid = origIds->GetValue(i);
      if (pid >= 0 && pid < on->GetNumberOfTuples())
      {
        on->GetTuple(pid, t);
        ok = true;
      }
    }
    if (!ok && en && i < en->GetNumberOfTuples())
    {
      en->GetTuple(i, t);
      ok = true;
    }
    if (!ok || vtkMath::Normalize(t) < 1e-15)
    {
      continue;
    }
    n[0] += t[0];
    n[1] += t[1];
    n[2] += t[2];
    ++used;
  }
  return used > 0 && vtkMath::Normalize(n) > 1e-15;
}

bool GatherNormalHint(vtkDataSet* original, vtkDataSet* extracted, double hint[3])
{
  if (AverageIncidentSurfaceNormals(original, extracted, hint))
  {
    return true;
  }
  return AverageDatasetPointNormals(original, extracted, hint);
}

bool ParseInteractivePacked(const char* s, std::vector<double>& out)
{
  out.clear();
  if (!s || !*s)
  {
    return false;
  }
  std::istringstream iss(s);
  double v = 0.0;
  while (iss >> v)
  {
    out.push_back(v);
  }
  return out.size() == 6u;
}

vtkSmartPointer<vtkPolyData> clipOneSide(vtkPolyData* input, vtkPlane* plane, bool keepPositive)
{
  vtkNew<vtkClipPolyData> clipper;
  clipper->SetInputData(input);
  clipper->SetClipFunction(plane);
  clipper->SetValue(0.0);
  if (!keepPositive)
  {
    clipper->InsideOutOn();
  }
  clipper->Update();
  vtkSmartPointer<vtkPolyData> o = vtkSmartPointer<vtkPolyData>::New();
  o->DeepCopy(clipper->GetOutput());
  return o;
}

vtkSmartPointer<vtkPolyData> extractClosestComponent(vtkPolyData* input, const double pt[3])
{
  if (!input || input->GetNumberOfPoints() == 0)
  {
    return nullptr;
  }
  vtkNew<vtkPolyDataConnectivityFilter> conn;
  conn->SetInputData(input);
  conn->SetExtractionModeToClosestPointRegion();
  conn->SetClosestPoint(pt[0], pt[1], pt[2]);
  conn->Update();
  vtkSmartPointer<vtkPolyData> o = vtkSmartPointer<vtkPolyData>::New();
  o->DeepCopy(conn->GetOutput());
  return o;
}

vtkSmartPointer<vtkPolyData> removeClosestComponent(vtkPolyData* side, const double refPt[3])
{
  if (!side || side->GetNumberOfPoints() == 0)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }
  vtkNew<vtkPolyDataConnectivityFilter> allRegions;
  allRegions->SetInputData(side);
  allRegions->SetExtractionModeToAllRegions();
  allRegions->ColorRegionsOn();
  allRegions->Update();

  const int nRegions = allRegions->GetNumberOfExtractedRegions();
  if (nRegions <= 1)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }

  vtkSmartPointer<vtkPolyData> closestComp = extractClosestComponent(side, refPt);
  if (!closestComp || closestComp->GetNumberOfPoints() == 0)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }

  double probePt[3];
  closestComp->GetPoint(0, probePt);

  vtkNew<vtkPointLocator> locator;
  locator->SetDataSet(allRegions->GetOutput());
  locator->BuildLocator();
  const vtkIdType ptId = locator->FindClosestPoint(probePt);

  vtkDataArray* regionIds = allRegions->GetOutput()->GetPointData()->GetArray("RegionId");
  if (!regionIds)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }

  const int tipRegionId = static_cast<int>(regionIds->GetTuple1(ptId));

  vtkNew<vtkPolyDataConnectivityFilter> keep;
  keep->SetInputData(side);
  keep->SetExtractionModeToSpecifiedRegions();
  for (int r = 0; r < nRegions; ++r)
  {
    if (r != tipRegionId)
    {
      keep->AddSpecifiedRegion(r);
    }
  }
  keep->Update();
  vtkSmartPointer<vtkPolyData> o = vtkSmartPointer<vtkPolyData>::New();
  o->DeepCopy(keep->GetOutput());
  return o;
}

/** Area-weighted centroid/normal of triangulated faces (triangles, quads, polygons, strips). */
bool ComputeFacePatchCentroidAndNormal(vtkPolyData* patch, double centroid[3], double avgNormal[3])
{
  centroid[0] = centroid[1] = centroid[2] = 0.0;
  avgNormal[0] = avgNormal[1] = avgNormal[2] = 0.0;

  vtkNew<vtkTriangleFilter> triF;
  triF->SetInputData(patch);
  triF->PassVertsOff();
  triF->PassLinesOff();
  triF->Update();
  vtkPolyData* triMesh = triF->GetOutput();
  if (!triMesh || triMesh->GetNumberOfCells() == 0)
  {
    return false;
  }

  vtkNew<vtkPolyDataNormals> nrm;
  nrm->SetInputData(triMesh);
  nrm->ComputeCellNormalsOn();
  nrm->ComputePointNormalsOff();
  nrm->ConsistencyOff();
  nrm->SplittingOff();
  nrm->AutoOrientNormalsOff();
  nrm->Update();
  vtkPolyData* withN = nrm->GetOutput();
  vtkDataArray* cellNormals = withN->GetCellData()->GetNormals();
  if (!cellNormals || cellNormals->GetNumberOfTuples() != withN->GetNumberOfCells())
  {
    return false;
  }

  double totalArea = 0.0;
  const vtkIdType nCells = withN->GetNumberOfCells();
  for (vtkIdType cellId = 0; cellId < nCells; ++cellId)
  {
    vtkCell* cell = withN->GetCell(cellId);
    if (!cell || cell->GetCellType() != VTK_TRIANGLE)
    {
      continue;
    }
    double p0[3], p1[3], p2[3];
    withN->GetPoint(cell->GetPointId(0), p0);
    withN->GetPoint(cell->GetPointId(1), p1);
    withN->GetPoint(cell->GetPointId(2), p2);
    double e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
    double c[3];
    vtkMath::Cross(e1, e2, c);
    const double area = 0.5 * vtkMath::Norm(c);
    if (area < 1e-30)
    {
      continue;
    }
    const double gc[3] = { (p0[0] + p1[0] + p2[0]) / 3.0, (p0[1] + p1[1] + p2[1]) / 3.0,
      (p0[2] + p1[2] + p2[2]) / 3.0 };
    double n[3];
    cellNormals->GetTuple(cellId, n);
    if (vtkMath::Normalize(n) < 1e-15)
    {
      continue;
    }
    totalArea += area;
    centroid[0] += gc[0] * area;
    centroid[1] += gc[1] * area;
    centroid[2] += gc[2] * area;
    avgNormal[0] += n[0] * area;
    avgNormal[1] += n[1] * area;
    avgNormal[2] += n[2] * area;
  }

  if (totalArea < 1e-30)
  {
    return false;
  }

  const double invA = 1.0 / totalArea;
  centroid[0] *= invA;
  centroid[1] *= invA;
  centroid[2] *= invA;
  if (vtkMath::Normalize(avgNormal) < 1e-15)
  {
    avgNormal[0] = 0.0;
    avgNormal[1] = 0.0;
    avgNormal[2] = 1.0;
  }
  return true;
}

bool ComputePlaneFromPolyDataPatch(
  vtkPolyData* pd, vtkDataSet* original, double origin[3], double normal[3])
{
  origin[0] = origin[1] = origin[2] = 0.0;
  normal[0] = 0.0;
  normal[1] = 0.0;
  normal[2] = 1.0;
  if (!pd)
  {
    return false;
  }

  const bool hasFaces = (pd->GetNumberOfPolys() > 0) || (pd->GetNumberOfStrips() > 0);
  if (hasFaces && ComputeFacePatchCentroidAndNormal(pd, origin, normal))
  {
    return true;
  }

  std::vector<std::array<double, 3>> samples;
  const bool hasLines = pd->GetNumberOfLines() > 0;
  if (hasLines)
  {
    const vtkIdType nc = pd->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nc; ++cid)
    {
      vtkCell* cell = pd->GetCell(cid);
      if (!cell || cell->GetCellDimension() != 1)
      {
        continue;
      }
      AppendCellPointSamples(pd, cell, samples);
    }
  }
  else
  {
    AppendAllPointSamples(pd, samples);
  }
  if (samples.empty())
  {
    return false;
  }

  const bool pcaOk = FitPlaneFromPointsPCA(samples, origin, normal);
  double hint[3] = { 0.0, 0.0, 0.0 };
  const bool haveHint = GatherNormalHint(original, pd, hint);

  if (pcaOk)
  {
    if (haveHint)
    {
      OrientNormalWithHint(normal, hint);
    }
    return true;
  }

  if (haveHint)
  {
    normal[0] = hint[0];
    normal[1] = hint[1];
    normal[2] = hint[2];
    if (hasLines || samples.size() >= 2)
    {
      double dir[3];
      if (LineDirectionFromSamples(samples, origin, dir))
      {
        MakeNormalPerpendicularToDir(normal, dir);
      }
    }
    return vtkMath::Normalize(normal) > 1e-15;
  }

  if (hasLines || samples.size() >= 2)
  {
    double dir[3];
    if (LineDirectionFromSamples(samples, origin, dir))
    {
      vtkMath::Perpendiculars(dir, normal, nullptr, 0.0);
      return vtkMath::Normalize(normal) > 1e-15;
    }
  }

  // Lone point with no incident-face / point-normal hint: still place a plane at the point.
  normal[0] = 0.0;
  normal[1] = 0.0;
  normal[2] = 1.0;
  return true;
}

bool ComputePlaneFromDatasetSelectionImpl(
  vtkDataSet* dataset, vtkSelection* selection, double origin[3], double normal[3])
{
  origin[0] = origin[1] = origin[2] = 0.0;
  normal[0] = 0.0;
  normal[1] = 0.0;
  normal[2] = 1.0;
  if (!dataset || !selection)
  {
    return false;
  }
  vtkNew<vtkExtractSelection> extract;
  extract->SetInputData(0, dataset);
  extract->SetInputData(1, selection);
  extract->Update();
  vtkDataSet* extracted = vtkDataSet::SafeDownCast(extract->GetOutputDataObject(0));
  if (!extracted || (extracted->GetNumberOfCells() == 0 && extracted->GetNumberOfPoints() == 0))
  {
    return false;
  }
  vtkPolyData* pd = vtkPolyData::SafeDownCast(extracted);
  vtkSmartPointer<vtkPolyData> surface;
  if (!pd)
  {
    vtkNew<vtkDataSetSurfaceFilter> surf;
    surf->SetInputData(extracted);
    surf->Update();
    surface = vtkSmartPointer<vtkPolyData>::New();
    surface->ShallowCopy(surf->GetOutput());
    pd = surface;
  }
  return ComputePlaneFromPolyDataPatch(pd, dataset, origin, normal);
}

void FillStampNewCellWithMarker(vtkAbstractArray* postArr, vtkIdType cellIdx, double marker)
{
  if (!postArr)
  {
    return;
  }
  if (auto* s = vtkStringArray::SafeDownCast(postArr))
  {
    s->SetValue(cellIdx, "");
    return;
  }
  auto* da = vtkDataArray::SafeDownCast(postArr);
  if (!da)
  {
    return;
  }
  const int nc = da->GetNumberOfComponents();
  for (int c = 0; c < nc; ++c)
  {
    da->SetComponent(cellIdx, c, marker);
  }
}

void SetNewCellTupleFromCell0(vtkAbstractArray* postArr, vtkAbstractArray* preArr, vtkIdType outCell, vtkIdType nPre)
{
  if (!postArr || !preArr || nPre <= 0)
  {
    return;
  }
  postArr->SetTuple(outCell, 0, preArr);
}

/**
 * vtkFillHolesFilter keeps input polys first (DeepCopy) then appends new triangles; it does not pass
 * cell data through. Rebuild output cell data from the pre-fill mesh for cells [0, nPre). On new
 * cells, the stamp scalar array receives the effective marker (see implementation); other arrays
 * copy tuple from cell 0 of the pre-fill mesh.
 */
void RestoreCellDataAfterFillHoles(vtkPolyData* preFill, vtkPolyData* postFill,
  bool useCustomMarker, double markerValue, const char* stampArrayNameFromUser,
  vtkSHYXSelectionPlaneClipper* self)
{
  if (!preFill || !postFill)
  {
    return;
  }
  vtkCellData* preCD = preFill->GetCellData();
  vtkCellData* postCD = postFill->GetCellData();
  const vtkIdType nPre = preFill->GetNumberOfCells();
  const vtkIdType nPost = postFill->GetNumberOfCells();
  if (nPost < nPre)
  {
    vtkWarningWithObjectMacro(
      self, "Fill holes output has fewer cells than pre-fill; skipping cell-data restore.");
    return;
  }

  const bool userNamed = (stampArrayNameFromUser && stampArrayNameFromUser[0] != '\0');
  const std::string effStampName =
    userNamed ? std::string(stampArrayNameFromUser) : std::string(kDefaultFillHoleStampArrayName);
  vtkAbstractArray* existingStampArr = preCD->GetAbstractArray(effStampName.c_str());
  const bool stampExisted = (existingStampArr != nullptr);

  double effMarker = markerValue;
  if (!useCustomMarker)
  {
    // Auto mode: max(existing)+1 when reusing an existing numeric array, else 1 (pre-fill cells get 0).
    if (stampExisted)
    {
      if (auto* da = vtkDataArray::SafeDownCast(existingStampArr))
      {
        if (da->GetNumberOfTuples() > 0)
        {
          double range[2];
          da->GetRange(range);
          effMarker = range[1] + 1.0;
        }
        else
        {
          effMarker = 1.0;
        }
      }
    }
    else
    {
      effMarker = 1.0;
    }
  }

  postCD->Initialize();
  postCD->CopyAllocate(preCD, nPost);

  for (vtkIdType i = 0; i < nPre; ++i)
  {
    postCD->CopyData(preCD, i, i);
  }

  const int nArrays = preCD->GetNumberOfArrays();
  for (vtkIdType i = nPre; i < nPost; ++i)
  {
    for (int ai = 0; ai < nArrays; ++ai)
    {
      vtkAbstractArray* preArr = preCD->GetAbstractArray(ai);
      if (!preArr)
      {
        continue;
      }
      vtkAbstractArray* postArr = postCD->GetAbstractArray(preArr->GetName());
      const bool isStamp = stampExisted && (effStampName == preArr->GetName());
      if (isStamp)
      {
        FillStampNewCellWithMarker(postArr, i, effMarker);
      }
      else
      {
        SetNewCellTupleFromCell0(postArr, preArr, i, nPre);
      }
    }
  }

  if (!stampExisted)
  {
    vtkNew<vtkDoubleArray> stamp;
    stamp->SetName(effStampName.c_str());
    stamp->SetNumberOfComponents(1);
    stamp->SetNumberOfTuples(nPost);
    for (vtkIdType i = 0; i < nPre; ++i)
    {
      stamp->SetTuple1(i, 0.0);
    }
    for (vtkIdType i = nPre; i < nPost; ++i)
    {
      stamp->SetTuple1(i, effMarker);
    }
    postCD->AddArray(stamp);
  }
}

double LoopRadiusAndCentroid(
  vtkPoints* pts, const std::vector<vtkIdType>& loop, double centroid[3])
{
  centroid[0] = centroid[1] = centroid[2] = 0.0;
  if (!pts || loop.empty())
  {
    return 0.0;
  }
  for (vtkIdType id : loop)
  {
    double x[3];
    pts->GetPoint(id, x);
    centroid[0] += x[0];
    centroid[1] += x[1];
    centroid[2] += x[2];
  }
  const double inv = 1.0 / static_cast<double>(loop.size());
  centroid[0] *= inv;
  centroid[1] *= inv;
  centroid[2] *= inv;
  double r = 0.0;
  for (vtkIdType id : loop)
  {
    double x[3];
    pts->GetPoint(id, x);
    r = std::max(r, std::sqrt(vtkMath::Distance2BetweenPoints(x, centroid)));
  }
  return r;
}

bool LoopLiesOnClipPlane(vtkPoints* pts, const std::vector<vtkIdType>& loop, const double origin[3],
  const double normal[3], double radius)
{
  if (!pts || loop.empty())
  {
    return false;
  }
  double acc = 0.0;
  for (vtkIdType id : loop)
  {
    double x[3];
    pts->GetPoint(id, x);
    acc += std::abs(normal[0] * (x[0] - origin[0]) + normal[1] * (x[1] - origin[1]) +
      normal[2] * (x[2] - origin[2]));
  }
  acc /= static_cast<double>(loop.size());
  const double tol = std::max(1e-8, 0.1 * std::max(radius, 1e-12));
  return acc <= tol;
}

/** Closed boundary loops whose circumradius is <= holeSize. Vertex order follows existing face winding. */
void CollectClosedBoundaryLoops(
  vtkPolyData* mesh, double holeSize, std::vector<std::vector<vtkIdType>>& loops)
{
  loops.clear();
  if (!mesh || mesh->GetNumberOfPolys() < 1 || mesh->GetNumberOfPoints() < 3)
  {
    return;
  }
  vtkPoints* inPts = mesh->GetPoints();
  vtkCellArray* inPolys = mesh->GetPolys();
  if (!inPts || !inPolys)
  {
    return;
  }

  vtkNew<vtkPolyData> surf;
  surf->SetPoints(inPts);
  surf->SetPolys(inPolys);
  surf->BuildLinks();

  vtkNew<vtkPolyData> linesPd;
  vtkNew<vtkCellArray> newLines;
  linesPd->SetLines(newLines);
  linesPd->SetPoints(inPts);

  vtkNew<vtkIdList> neighbors;
  vtkIdType npts = 0;
  const vtkIdType* pts = nullptr;
  vtkIdType cellId = 0;
  for (inPolys->InitTraversal(); inPolys->GetNextCell(npts, pts); ++cellId)
  {
    for (vtkIdType i = 0; i < npts; ++i)
    {
      const vtkIdType p1 = pts[i];
      const vtkIdType p2 = pts[(i + 1) % npts];
      surf->GetCellEdgeNeighbors(cellId, p1, p2, neighbors);
      if (neighbors->GetNumberOfIds() < 1)
      {
        newLines->InsertNextCell(2);
        newLines->InsertCellPoint(p1);
        newLines->InsertCellPoint(p2);
      }
    }
  }

  const vtkIdType nLineCells = newLines->GetNumberOfCells();
  if (nLineCells < 3)
  {
    return;
  }

  linesPd->BuildLinks();
  std::vector<char> visited(static_cast<size_t>(nLineCells), 0);
  vtkNew<vtkIdList> endId;
  endId->SetNumberOfIds(1);

  for (vtkIdType lineId = 0; lineId < nLineCells; ++lineId)
  {
    if (visited[static_cast<size_t>(lineId)])
    {
      continue;
    }
    visited[static_cast<size_t>(lineId)] = 1;
    linesPd->GetCellPoints(lineId, npts, pts);
    if (npts < 2)
    {
      continue;
    }
    std::vector<vtkIdType> loop;
    loop.push_back(pts[0]);
    const vtkIdType startId = pts[0];
    endId->SetId(0, pts[1]);
    int valid = 1;
    vtkIdType currentCellId = lineId;
    int guard = 0;
    while (startId != endId->GetId(0) && valid && guard++ < nLineCells + 2)
    {
      loop.push_back(endId->GetId(0));
      linesPd->GetCellNeighbors(currentCellId, endId, neighbors);
      if (neighbors->GetNumberOfIds() != 1)
      {
        valid = 0;
        break;
      }
      const vtkIdType neiId = neighbors->GetId(0);
      visited[static_cast<size_t>(neiId)] = 1;
      linesPd->GetCellPoints(neiId, npts, pts);
      endId->SetId(0, (pts[0] != endId->GetId(0) ? pts[0] : pts[1]));
      currentCellId = neiId;
    }
    if (!valid || loop.size() < 3)
    {
      continue;
    }
    double c[3];
    const double radius = LoopRadiusAndCentroid(inPts, loop, c);
    if (radius <= holeSize)
    {
      loops.push_back(std::move(loop));
    }
  }
}

void ExtendPointDataForNewPoint(vtkPointData* pd, vtkIdType copyFrom)
{
  if (!pd)
  {
    return;
  }
  const int nArrays = pd->GetNumberOfArrays();
  for (int ai = 0; ai < nArrays; ++ai)
  {
    vtkAbstractArray* arr = pd->GetAbstractArray(ai);
    if (!arr)
    {
      continue;
    }
    const vtkIdType src = (copyFrom >= 0 && copyFrom < arr->GetNumberOfTuples()) ? copyFrom : 0;
    if (arr->GetNumberOfTuples() <= 0)
    {
      continue;
    }
    arr->InsertNextTuple(src, arr);
  }
}

/** Fan triangles from hub to each qualifying clip-plane loop. Loop order is existing face winding,
 *  so each cap triangle is (hub, b, a) opposite the wall edge a->b. */
vtkSmartPointer<vtkPolyData> AppendWheelCapsFromPlaneCenter(vtkPolyData* mesh,
  const std::vector<std::vector<vtkIdType>>& loops, const double hub[3])
{
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  out->DeepCopy(mesh);
  if (!mesh || loops.empty())
  {
    return out;
  }

  vtkPoints* pts = out->GetPoints();
  vtkCellArray* polys = out->GetPolys();
  if (!pts || !polys)
  {
    return out;
  }

  double bb[6];
  mesh->GetBounds(bb);
  const double dx = bb[1] - bb[0];
  const double dy = bb[3] - bb[2];
  const double dz = bb[5] - bb[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double reuseTol2 = std::max(1e-24, (diag * 1e-9) * (diag * 1e-9));

  vtkIdType hubId = -1;
  double bestDist2 = -1.0;
  const vtkIdType nOld = pts->GetNumberOfPoints();
  for (vtkIdType i = 0; i < nOld; ++i)
  {
    double x[3];
    pts->GetPoint(i, x);
    const double d2 = vtkMath::Distance2BetweenPoints(x, hub);
    if (bestDist2 < 0.0 || d2 < bestDist2)
    {
      bestDist2 = d2;
      hubId = i;
    }
  }
  if (hubId < 0 || bestDist2 > reuseTol2)
  {
    hubId = pts->InsertNextPoint(hub);
    const vtkIdType copyFrom = loops.front().empty() ? 0 : loops.front().front();
    ExtendPointDataForNewPoint(out->GetPointData(), copyFrom);
  }

  for (const auto& loop : loops)
  {
    const size_t n = loop.size();
    if (n < 3)
    {
      continue;
    }
    for (size_t i = 0; i < n; ++i)
    {
      const vtkIdType a = loop[i];
      const vtkIdType b = loop[(i + 1) % n];
      if (a == b || a == hubId || b == hubId)
      {
        continue;
      }
      double pa[3];
      double pb[3];
      double ph[3];
      pts->GetPoint(a, pa);
      pts->GetPoint(b, pb);
      pts->GetPoint(hubId, ph);
      const double e1[3] = { pb[0] - ph[0], pb[1] - ph[1], pb[2] - ph[2] };
      const double e2[3] = { pa[0] - ph[0], pa[1] - ph[1], pa[2] - ph[2] };
      double cr[3];
      vtkMath::Cross(e1, e2, cr);
      if (vtkMath::Norm(cr) < 1e-30)
      {
        continue;
      }
      polys->InsertNextCell(3);
      polys->InsertCellPoint(hubId);
      polys->InsertCellPoint(b);
      polys->InsertCellPoint(a);
    }
  }
  return out;
}

} // namespace

vtkSHYXSelectionPlaneClipper::vtkSHYXSelectionPlaneClipper()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
  this->SetFillHoleStampCellArrayName(kDefaultFillHoleStampArrayName);
}

vtkSHYXSelectionPlaneClipper::~vtkSHYXSelectionPlaneClipper()
{
  this->SetInteractiveCutPackedString(nullptr);
  this->SetClipPlaneHintPackedString(nullptr);
  this->SetSelectionCellArrayName(nullptr);
  this->SetFillHoleStampCellArrayName(nullptr);
}

void vtkSHYXSelectionPlaneClipper::SetUseInteractiveCutPlanes(int flag)
{
  if (this->UseInteractiveCutPlanes == flag)
  {
    return;
  }
  this->UseInteractiveCutPlanes = flag;
  // Intentionally no Modified(): show/hide widget only (see class doc).
}

bool vtkSHYXSelectionPlaneClipper::ComputePlaneFromDatasetSelection(
  vtkDataSet* dataset, vtkSelection* selection, double origin[3], double normal[3])
{
  return ComputePlaneFromDatasetSelectionImpl(dataset, selection, origin, normal);
}

void vtkSHYXSelectionPlaneClipper::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

void vtkSHYXSelectionPlaneClipper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ClipOffset: " << this->ClipOffset << "\n";
  os << indent << "UseTipConnectivity: " << this->UseTipConnectivity << "\n";
  os << indent << "InvertResult: " << this->InvertResult << "\n";
  os << indent << "RemovePositiveHalfSpace: " << this->RemovePositiveHalfSpace << "\n";
  os << indent << "UseInteractiveCutPlanes: " << this->UseInteractiveCutPlanes << "\n";
  os << indent << "FillHoles: " << this->FillHoles << "\n";
  os << indent << "FillHolesMaximumSize: " << this->FillHolesMaximumSize << "\n";
  os << indent << "WheelCap: " << this->WheelCap << "\n";
  os << indent << "UseCustomFillHoleMarkerValue: " << this->UseCustomFillHoleMarkerValue << "\n";
  os << indent << "FillHoleNewCellDataMarkerValue: " << this->FillHoleNewCellDataMarkerValue << "\n";
  os << indent << "FillHoleStampCellArrayName: "
     << (this->FillHoleStampCellArrayName ? this->FillHoleStampCellArrayName : "(null)") << "\n";
  os << indent << "InteractiveCutPackedString: "
     << (this->InteractiveCutPackedString ? this->InteractiveCutPackedString : "") << "\n";
  os << indent << "ClipPlaneHintPackedString: "
     << (this->ClipPlaneHintPackedString ? this->ClipPlaneHintPackedString : "") << "\n";
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
}

int vtkSHYXSelectionPlaneClipper::FillInputPortInformation(int port, vtkInformation* info)
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

int vtkSHYXSelectionPlaneClipper::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXSelectionPlaneClipper::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* mesh = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

  if (!mesh || !output)
  {
    return 0;
  }
  this->SetClipPlaneHintPackedString(nullptr);

  if (!mesh->GetNumberOfCells())
  {
    vtkWarningMacro("Input mesh is empty.");
    output->Initialize();
    return 1;
  }

  std::vector<double> packed;
  const bool havePacked = ParseInteractivePacked(this->InteractiveCutPackedString, packed);

  double centroid[3] = { 0.0, 0.0, 0.0 };
  double planeNormal[3] = { 0.0, 0.0, 1.0 };
  double origin[3] = { 0.0, 0.0, 0.0 };
  bool havePlane = false;

  if (havePacked)
  {
    const double* o = packed.data();
    const double* d = packed.data() + 3;
    double nx = d[0] - o[0];
    double ny = d[1] - o[1];
    double nz = d[2] - o[2];
    const double nn = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nn > 1e-15)
    {
      origin[0] = o[0];
      origin[1] = o[1];
      origin[2] = o[2];
      planeNormal[0] = nx / nn;
      planeNormal[1] = ny / nn;
      planeNormal[2] = nz / nn;
      centroid[0] = origin[0];
      centroid[1] = origin[1];
      centroid[2] = origin[2];
      havePlane = true;
    }
  }

  if (!havePlane)
  {
    vtkSelection* inputSel = nullptr;
    if (this->GetNumberOfInputConnections(1) > 0)
    {
      vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
      if (selInfo && selInfo->Has(vtkDataObject::DATA_OBJECT()))
      {
        inputSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
        if (inputSel && inputSel->GetNumberOfNodes() == 0)
        {
          inputSel = nullptr;
        }
      }
    }

    vtkNew<vtkSelection> cellArraySel;
    if (!inputSel && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0')
    {
      vtkDataArray* arr = mesh->GetCellData()->GetArray(this->SelectionCellArrayName);
      if (!arr)
      {
        vtkWarningMacro("SelectionCellArrayName \"" << this->SelectionCellArrayName
                                                    << "\" not found on input cell data.");
      }
      else
      {
        vtkNew<vtkIdTypeArray> ids;
        ids->SetNumberOfComponents(1);
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
            ids->InsertNextValue(cid);
          }
        }
        if (ids->GetNumberOfTuples() > 0)
        {
          vtkNew<vtkSelectionNode> node;
          node->SetFieldType(vtkSelectionNode::CELL);
          node->SetContentType(vtkSelectionNode::INDICES);
          node->SetSelectionList(ids);
          cellArraySel->AddNode(node);
          inputSel = cellArraySel;
        }
      }
    }

    if (inputSel && ComputePlaneFromDatasetSelectionImpl(mesh, inputSel, centroid, planeNormal))
    {
      havePlane = true;
      origin[0] = centroid[0] + this->ClipOffset * planeNormal[0];
      origin[1] = centroid[1] + this->ClipOffset * planeNormal[1];
      origin[2] = centroid[2] + this->ClipOffset * planeNormal[2];
    }
    else
    {
      vtkWarningMacro("Could not fit a clip plane from the selection (faces, lines, or points). "
                      "Use Copy Active Selection on any scene node, or set Selection Cell Array Name. "
                      "Pass-through input mesh.");
      output->ShallowCopy(mesh);
      return 1;
    }
  }

  if (!havePlane)
  {
    output->ShallowCopy(mesh);
    return 1;
  }

  double meshBounds[6];
  mesh->GetBounds(meshBounds);

  vtkNew<vtkPlane> plane;
  plane->SetOrigin(origin);
  plane->SetNormal(planeNormal);

  vtkSmartPointer<vtkPolyData> result;

  if (this->UseTipConnectivity)
  {
    vtkSmartPointer<vtkPolyData> sidePos = clipOneSide(mesh, plane, true);
    vtkSmartPointer<vtkPolyData> sideNeg = clipOneSide(mesh, plane, false);

    if ((!sidePos || sidePos->GetNumberOfPoints() == 0) && (!sideNeg || sideNeg->GetNumberOfPoints() == 0))
    {
      vtkWarningMacro("Clip produced empty geometry; pass-through.");
      output->ShallowCopy(mesh);
      return 1;
    }

    vtkSmartPointer<vtkPolyData> closestPos = extractClosestComponent(sidePos, centroid);
    vtkSmartPointer<vtkPolyData> closestNeg = extractClosestComponent(sideNeg, centroid);

    const vtkIdType nClosestPos = closestPos ? closestPos->GetNumberOfPoints() : 0;
    const vtkIdType nClosestNeg = closestNeg ? closestNeg->GetNumberOfPoints() : 0;

    bool tipOnPositive = false;
    if (nClosestPos == 0)
    {
      tipOnPositive = false;
    }
    else if (nClosestNeg == 0)
    {
      tipOnPositive = true;
    }
    else
    {
      tipOnPositive = (nClosestPos <= nClosestNeg);
    }

    vtkPolyData* tipSide = tipOnPositive ? sidePos.GetPointer() : sideNeg.GetPointer();
    vtkPolyData* bodySide = tipOnPositive ? sideNeg.GetPointer() : sidePos.GetPointer();
    vtkSmartPointer<vtkPolyData> tipPiece = tipOnPositive ? closestPos : closestNeg;

    if (this->InvertResult)
    {
      result = tipPiece;
    }
    else
    {
      vtkSmartPointer<vtkPolyData> keptFromTipSide = removeClosestComponent(tipSide, centroid);
      const bool hasKept = (keptFromTipSide && keptFromTipSide->GetNumberOfCells() > 0);

      if (!hasKept)
      {
        result = bodySide;
      }
      else
      {
        vtkNew<vtkAppendPolyData> merger;
        merger->AddInputData(bodySide);
        merger->AddInputData(keptFromTipSide);
        merger->Update();
        vtkNew<vtkCleanPolyData> cleaner;
        cleaner->SetInputConnection(merger->GetOutputPort());
        cleaner->Update();
        result = vtkSmartPointer<vtkPolyData>::New();
        result->DeepCopy(cleaner->GetOutput());
      }
    }
  }
  else
  {
    vtkNew<vtkClipPolyData> clipper;
    clipper->SetInputData(mesh);
    clipper->SetClipFunction(plane);
    clipper->SetValue(0.0);
    const bool removePositive =
      (this->RemovePositiveHalfSpace != 0) != (this->InvertResult != 0);
    if (removePositive)
    {
      clipper->InsideOutOff();
    }
    else
    {
      clipper->InsideOutOn();
    }
    clipper->Update();
    result = vtkSmartPointer<vtkPolyData>::New();
    result->DeepCopy(clipper->GetOutput());
  }

  if (!result || result->GetNumberOfPoints() == 0)
  {
    vtkWarningMacro("Clip result empty; pass-through input.");
    output->ShallowCopy(mesh);
    return 1;
  }

  vtkNew<vtkCleanPolyData> finalClean;
  finalClean->SetInputData(result);
  finalClean->Update();
  vtkPolyData* cleaned = finalClean->GetOutput();

  if (this->FillHoles && cleaned && cleaned->GetNumberOfPoints() > 0)
  {
    double bb[6];
    cleaned->GetBounds(bb);
    const double dx = bb[1] - bb[0];
    const double dy = bb[3] - bb[2];
    const double dz = bb[5] - bb[4];
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double holeSize = (this->FillHolesMaximumSize > 0.0)
      ? this->FillHolesMaximumSize
      : std::max(diag * 0.35, 1e-6);

    vtkSmartPointer<vtkPolyData> toFill = vtkSmartPointer<vtkPolyData>::New();
    toFill->ShallowCopy(cleaned);
    if (this->WheelCap)
    {
      std::vector<std::vector<vtkIdType>> loops;
      CollectClosedBoundaryLoops(cleaned, holeSize, loops);
      std::vector<std::vector<vtkIdType>> wheelLoops;
      vtkPoints* cpts = cleaned->GetPoints();
      for (const auto& loop : loops)
      {
        double lc[3];
        const double radius = LoopRadiusAndCentroid(cpts, loop, lc);
        if (LoopLiesOnClipPlane(cpts, loop, origin, planeNormal, radius))
        {
          wheelLoops.push_back(loop);
        }
      }
      if (!wheelLoops.empty())
      {
        toFill = AppendWheelCapsFromPlaneCenter(cleaned, wheelLoops, origin);
      }
    }

    vtkNew<vtkFillHolesFilter> filler;
    filler->SetInputData(toFill);
    filler->SetHoleSize(holeSize);
    filler->Update();
    RestoreCellDataAfterFillHoles(cleaned, filler->GetOutput(),
      this->UseCustomFillHoleMarkerValue != 0, this->FillHoleNewCellDataMarkerValue,
      this->FillHoleStampCellArrayName, this);
    vtkNew<vtkCleanPolyData> postClean;
    postClean->SetInputConnection(filler->GetOutputPort());
    postClean->Update();
    output->ShallowCopy(postClean->GetOutput());
  }
  else
  {
    output->ShallowCopy(cleaned);
  }
  SetClipPlaneHintPacked(this, meshBounds, origin, planeNormal);
  return 1;
}

VTK_ABI_NAMESPACE_END
