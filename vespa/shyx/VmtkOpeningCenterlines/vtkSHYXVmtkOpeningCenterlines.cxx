#include "vtkSHYXVmtkOpeningCenterlines.h"

#include "vtkvmtkPolyDataCenterlines.h"

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataArraySelection.h>
#include <vtkDoubleArray.h>
#include <vtkIdTypeArray.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkIdList.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXVmtkOpeningCenterlines);

void vtkSHYXVmtkOpeningCenterlines::ClearAllArrays(vtkDataArraySelection* sel)
{
  if (!sel)
  {
    return;
  }
  for (int j = sel->GetNumberOfArrays() - 1; j >= 0; --j)
  {
    const char* nm = sel->GetArrayName(j);
    if (nm)
    {
      sel->RemoveArrayByName(nm);
    }
  }
}

void vtkSHYXVmtkOpeningCenterlines::InvalidateInletSelectionIfOpeningThresholdChanged()
{
  vtkInformation* ai = this->GetInputArrayInformation(0);
  std::string arrayTag = "|";
  if (ai && ai->Has(vtkDataObject::FIELD_NAME()))
  {
    const char* nm = ai->Get(vtkDataObject::FIELD_NAME());
    arrayTag += nm ? nm : "";
  }
  arrayTag += "|";
  if (ai && ai->Has(vtkDataObject::FIELD_ASSOCIATION()))
  {
    arrayTag += std::to_string(ai->Get(vtkDataObject::FIELD_ASSOCIATION()));
  }
  const std::string fp = arrayTag;
  if (fp != this->CachedOpeningThresholdFingerprint)
  {
    ClearAllArrays(this->InletSelection);
    ClearAllArrays(this->ExcludedOpeningSelection);
    this->CachedOpeningThresholdFingerprint = fp;
    this->InletSelection->Modified();
    this->ExcludedOpeningSelection->Modified();
    this->Modified();
  }
}

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

/** vtkPolyDataConnectivityFilter output only keeps vertices used by extracted cells (unused points dropped). */
vtkIdType ReadTupleAsPointId(vtkDataArray* arr, vtkIdType i)
{
  if (!arr || i < 0 || i >= arr->GetNumberOfTuples())
  {
    return -1;
  }
  return static_cast<vtkIdType>(std::llround(arr->GetTuple1(i)));
}

vtkDataArray* GetOriginalPointIds(vtkPolyData* pd)
{
  if (!pd)
  {
    return nullptr;
  }
  vtkDataArray* a =
    vtkDataArray::SafeDownCast(pd->GetPointData()->GetAbstractArray("OriginalPointIds"));
  if (a && a->GetNumberOfTuples() == pd->GetNumberOfPoints() && a->GetNumberOfComponents() == 1)
  {
    return a;
  }
  return nullptr;
}

vtkDataArray* GetOriginalCellIds(vtkPolyData* pd)
{
  if (!pd)
  {
    return nullptr;
  }
  vtkDataArray* a =
    vtkDataArray::SafeDownCast(pd->GetCellData()->GetAbstractArray("OriginalCellIds"));
  if (a && a->GetNumberOfTuples() == pd->GetNumberOfCells() && a->GetNumberOfComponents() == 1)
  {
    return a;
  }
  return nullptr;
}

bool CollectCellsByScalarGreaterThan(vtkPolyData* pd, vtkDataArray* arrOnField, bool fieldIsPoints,
  double lowerExclusive, std::set<vtkIdType>& selected)
{
  selected.clear();
  if (!pd || !arrOnField)
  {
    return false;
  }

  const vtkIdType nCells = pd->GetNumberOfCells();
  const vtkIdType nPts = pd->GetNumberOfPoints();
  if (fieldIsPoints)
  {
    if (arrOnField->GetNumberOfTuples() != nPts)
    {
      return false;
    }
  }
  else if (arrOnField->GetNumberOfTuples() != nCells)
  {
    return false;
  }

  auto passesThreshold = [&](vtkIdType tupleIdx) -> bool {
    const double m = TupleMagnitude(arrOnField, tupleIdx);
    return m > lowerExclusive;
  };

  if (!fieldIsPoints)
  {
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (passesThreshold(cid))
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
    for (vtkIdType k = 0; k < nptsCorner; ++k)
    {
      anyPass = anyPass || passesThreshold(pids[k]);
    }
    if (anyPass)
    {
      selected.insert(cid);
    }
  }
  return true;
}

bool ExtractSelectedCells(vtkPolyData* input, const std::set<vtkIdType>& selectedCells,
  vtkPolyData* outMesh, vtkIdTypeArray* originalPointIds, vtkIdTypeArray* originalCellIds)
{
  outMesh->Initialize();
  originalPointIds->Initialize();
  originalPointIds->SetName("OriginalPointIds");
  originalPointIds->SetNumberOfComponents(1);
  originalCellIds->Initialize();
  originalCellIds->SetName("OriginalCellIds");
  originalCellIds->SetNumberOfComponents(1);

  if (!input || selectedCells.empty())
  {
    return false;
  }

  vtkPoints* inPts = input->GetPoints();
  if (!inPts)
  {
    return false;
  }

  std::unordered_map<vtkIdType, vtkIdType> oldToNew;
  vtkNew<vtkPoints> pts;
  vtkNew<vtkCellArray> polys;

  for (vtkIdType cid : selectedCells)
  {
    if (cid < 0 || cid >= input->GetNumberOfCells())
    {
      continue;
    }
    vtkIdType npts;
    const vtkIdType* pids;
    input->GetCellPoints(cid, npts, pids);
    vtkNew<vtkIdList> newIds;
    newIds->SetNumberOfIds(npts);
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const vtkIdType oldPid = pids[k];
      vtkIdType newPid = -1;
      const auto it = oldToNew.find(oldPid);
      if (it != oldToNew.end())
      {
        newPid = it->second;
      }
      else
      {
        double x[3];
        inPts->GetPoint(oldPid, x);
        newPid = pts->InsertNextPoint(x);
        originalPointIds->InsertNextValue(oldPid);
        oldToNew.insert({ oldPid, newPid });
      }
      newIds->SetId(k, newPid);
    }
    polys->InsertNextCell(newIds);
    originalCellIds->InsertNextValue(cid);
  }

  outMesh->SetPoints(pts);
  outMesh->SetPolys(polys);
  outMesh->GetPointData()->AddArray(originalPointIds);
  outMesh->GetCellData()->AddArray(originalCellIds);
  return outMesh->GetNumberOfCells() > 0;
}

double ComputeRegionRepresentativeScalar(vtkPolyData* labeled, int regionIndex, vtkDataArray* rid,
  vtkDataArray* thrArr, bool thrPointMode, vtkDataArray* origPt, vtkDataArray* origCell)
{
  std::vector<double> samples;
  if (thrPointMode)
  {
    if (!origPt || origPt->GetNumberOfTuples() != labeled->GetNumberOfPoints())
    {
      return std::numeric_limits<double>::quiet_NaN();
    }
    for (vtkIdType i = 0; i < labeled->GetNumberOfPoints(); ++i)
    {
      if (static_cast<int>(std::llround(rid->GetTuple1(i))) != regionIndex)
      {
        continue;
      }
      samples.push_back(TupleMagnitude(thrArr, ReadTupleAsPointId(origPt, i)));
    }
  }
  else
  {
    if (!origCell || origCell->GetNumberOfTuples() != labeled->GetNumberOfCells())
    {
      return std::numeric_limits<double>::quiet_NaN();
    }
    for (vtkIdType cid = 0; cid < labeled->GetNumberOfCells(); ++cid)
    {
      vtkIdType npts;
      const vtkIdType* pts;
      labeled->GetCellPoints(cid, npts, pts);
      bool cellInRegion = true;
      for (vtkIdType k = 0; k < npts; ++k)
      {
        if (static_cast<int>(std::llround(rid->GetTuple1(pts[k]))) != regionIndex)
        {
          cellInRegion = false;
          break;
        }
      }
      if (!cellInRegion)
      {
        continue;
      }
      samples.push_back(TupleMagnitude(thrArr, ReadTupleAsPointId(origCell, cid)));
    }
  }
  if (samples.empty())
  {
    return 0.0;
  }
  double sum = 0.0;
  for (double v : samples)
  {
    sum += v;
  }
  return sum / static_cast<double>(samples.size());
}

vtkIdType SeedVertexForOpeningRegion(vtkPolyData* labeled, vtkDataArray* rid, vtkDataArray* origPt,
  int regionIndex)
{
  if (!labeled || !rid || !origPt || rid->GetNumberOfTuples() != labeled->GetNumberOfPoints() ||
    origPt->GetNumberOfTuples() != labeled->GetNumberOfPoints())
  {
    return -1;
  }

  std::vector<vtkIdType> indices;
  indices.reserve(static_cast<size_t>(labeled->GetNumberOfPoints()));
  for (vtkIdType i = 0; i < labeled->GetNumberOfPoints(); ++i)
  {
    if (static_cast<int>(std::llround(rid->GetTuple1(i))) == regionIndex)
    {
      indices.push_back(i);
    }
  }
  if (indices.empty())
  {
    return -1;
  }

  double cx = 0.0;
  double cy = 0.0;
  double cz = 0.0;
  for (vtkIdType ii : indices)
  {
    double p[3];
    labeled->GetPoint(ii, p);
    cx += p[0];
    cy += p[1];
    cz += p[2];
  }
  const double inv = 1.0 / static_cast<double>(indices.size());
  cx *= inv;
  cy *= inv;
  cz *= inv;

  vtkIdType bestIdx = indices[0];
  double bestD2 = std::numeric_limits<double>::infinity();
  for (vtkIdType ii : indices)
  {
    double p[3];
    labeled->GetPoint(ii, p);
    const double dx = p[0] - cx;
    const double dy = p[1] - cy;
    const double dz = p[2] - cz;
    const double d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < bestD2)
    {
      bestD2 = d2;
      bestIdx = ii;
    }
  }
  return ReadTupleAsPointId(origPt, bestIdx);
}

std::string MakeSeedPointLabel(vtkIdType sid,
  std::unordered_map<std::string, int>& duplicateCounter)
{
  char buf[160];
  std::snprintf(buf, sizeof(buf), "SeedPoint: %lld", static_cast<long long>(sid));
  std::string base(buf);
  int n = ++duplicateCounter[base];
  if (n > 1)
  {
    return base + " #" + std::to_string(n);
  }
  return base;
}

void EnsurePointGlobalIds(vtkPolyData* pd)
{
  if (!pd)
  {
    return;
  }
  vtkPointData* ptd = pd->GetPointData();
  vtkDataArray* g = vtkDataArray::SafeDownCast(ptd->GetGlobalIds());
  if (g && g->GetNumberOfTuples() == pd->GetNumberOfPoints() && g->GetNumberOfComponents() == 1)
  {
    return;
  }
  vtkNew<vtkIdTypeArray> gg;
  gg->SetName("GlobalIds");
  const vtkIdType n = pd->GetNumberOfPoints();
  for (vtkIdType i = 0; i < n; ++i)
  {
    gg->InsertNextValue(static_cast<vtkTypeInt64>(i));
  }
  ptd->SetGlobalIds(gg);
}

} // namespace

vtkSHYXVmtkOpeningCenterlines::vtkSHYXVmtkOpeningCenterlines()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(2);
  this->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_CELLS, "EndpointIndex");
  this->InletSelection = vtkSmartPointer<vtkDataArraySelection>::New();
  this->ExcludedOpeningSelection = vtkSmartPointer<vtkDataArraySelection>::New();
}

vtkSHYXVmtkOpeningCenterlines::~vtkSHYXVmtkOpeningCenterlines() = default;

vtkDataArraySelection* vtkSHYXVmtkOpeningCenterlines::GetInletSelection()
{
  return this->InletSelection;
}

vtkDataArraySelection* vtkSHYXVmtkOpeningCenterlines::GetExcludedOpeningSelection()
{
  return this->ExcludedOpeningSelection;
}

vtkMTimeType vtkSHYXVmtkOpeningCenterlines::GetMTime()
{
  vtkMTimeType t = this->Superclass::GetMTime();
  if (this->InletSelection)
  {
    t = std::max(t, this->InletSelection->GetMTime());
  }
  if (this->ExcludedOpeningSelection)
  {
    t = std::max(t, this->ExcludedOpeningSelection->GetMTime());
  }
  return t;
}

void vtkSHYXVmtkOpeningCenterlines::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Threshold rule: magnitude > 0 (fixed)\n";
  os << indent << "CalculateCenterline: " << this->CalculateCenterline << "\n";
  os << indent << "FlipNormals: " << this->FlipNormals << "\n";
  os << indent << "StopFastMarchingOnReachingTarget: " << this->StopFastMarchingOnReachingTarget
     << "\n";
  os << indent << "AppendEndPointsToCenterlines: " << this->AppendEndPointsToCenterlines << "\n";
}

int vtkSHYXVmtkOpeningCenterlines::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXVmtkOpeningCenterlines::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port >= 0 && port <= 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXVmtkOpeningCenterlines::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* outCenterlines = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outSeeds = vtkPolyData::GetData(outputVector, 1);

  if (!input || !outSeeds || !outCenterlines)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }

  outSeeds->Initialize();
  outCenterlines->Initialize();

  this->InvalidateInletSelectionIfOpeningThresholdChanged();

  vtkDataArray* thrArr = this->GetInputArrayToProcess(0, inputVector);
  if (!thrArr)
  {
    vtkWarningMacro(<< "No threshold array selected (choose Threshold array on the input surface).");
    ClearAllArrays(this->InletSelection);
    ClearAllArrays(this->ExcludedOpeningSelection);
    this->InletSelection->Modified();
    this->ExcludedOpeningSelection->Modified();
    ++this->OpeningListRevision;
    this->Modified();
    return 1;
  }

  const int assoc = this->GetInputArrayAssociation(0, inputVector);
  const bool fieldIsPoints = (assoc == vtkDataObject::FIELD_ASSOCIATION_POINTS);

  std::set<vtkIdType> selectedCells;
  if (!CollectCellsByScalarGreaterThan(input, thrArr, fieldIsPoints, 0.0, selectedCells) ||
    selectedCells.empty())
  {
    vtkWarningMacro(
      << "No cells passed the scalar threshold (check Threshold array; rule is magnitude > 0).");
    ClearAllArrays(this->InletSelection);
    ClearAllArrays(this->ExcludedOpeningSelection);
    this->InletSelection->Modified();
    this->ExcludedOpeningSelection->Modified();
    ++this->OpeningListRevision;
    this->Modified();
    return 1;
  }

  const bool thrPointMode = fieldIsPoints;

  vtkNew<vtkPolyData> masked;
  vtkNew<vtkIdTypeArray> origIds;
  vtkNew<vtkIdTypeArray> origCellIds;
  if (!ExtractSelectedCells(input, selectedCells, masked, origIds, origCellIds))
  {
    vtkErrorMacro("Failed to extract thresholded cells.");
    return 0;
  }

  vtkNew<vtkPolyDataConnectivityFilter> connAll;
  connAll->SetInputData(masked);
  connAll->SetExtractionModeToAllRegions();
  connAll->ColorRegionsOn();
  connAll->Update();

  vtkPolyData* regionLabeled = connAll->GetOutput();
  const int nReg = connAll->GetNumberOfExtractedRegions();

  vtkDataArray* rid =
    vtkDataArray::SafeDownCast(regionLabeled->GetPointData()->GetAbstractArray("RegionId"));
  vtkDataArray* origPtOnLabeled = GetOriginalPointIds(regionLabeled);
  vtkDataArray* origCellOnLabeled = GetOriginalCellIds(regionLabeled);
  if (!rid || !origPtOnLabeled)
  {
    vtkErrorMacro(
      << "Connectivity output missing RegionId or OriginalPointIds (needed for per-opening seeds).");
    return 0;
  }
  if (!thrPointMode && !origCellOnLabeled)
  {
    vtkErrorMacro(<< "Cell-centered threshold array requires OriginalCellIds on the extracted mesh.");
    return 0;
  }

  std::vector<std::string> names;
  std::vector<vtkIdType> surfacePidPerRegion;
  std::vector<double> openingScalars;
  names.reserve(static_cast<size_t>(nReg));
  surfacePidPerRegion.reserve(static_cast<size_t>(nReg));
  openingScalars.reserve(static_cast<size_t>(nReg));

  std::unordered_map<std::string, int> duplicateLabelCounter;
  for (int r = 0; r < nReg; ++r)
  {
    const vtkIdType sid =
      SeedVertexForOpeningRegion(regionLabeled, rid, origPtOnLabeled, r);
    if (sid < 0)
    {
      vtkErrorMacro(<< "Failed to compute seed point for opening region " << r << ".");
      return 0;
    }

    const double rep = ComputeRegionRepresentativeScalar(regionLabeled, r, rid, thrArr, thrPointMode,
      origPtOnLabeled, origCellOnLabeled);
    const std::string label = MakeSeedPointLabel(sid, duplicateLabelCounter);
    names.emplace_back(label);
    surfacePidPerRegion.push_back(sid);
    openingScalars.push_back(rep);
  }

  // Sync vtkDataArraySelection lists (inlet + remove); unchecked by default for both
  {
    std::set<std::string> current;
    for (const auto& s : names)
    {
      current.insert(s);
    }
    auto syncOne = [&](vtkDataArraySelection* sel) {
      for (int j = sel->GetNumberOfArrays() - 1; j >= 0; --j)
      {
        const char* existing = sel->GetArrayName(j);
        if (existing && current.find(existing) == current.end())
        {
          sel->RemoveArrayByName(existing);
        }
      }
      for (const auto& nm : names)
      {
        if (!sel->ArrayExists(nm.c_str()))
        {
          sel->AddArray(nm.c_str(), false);
        }
      }
    };
    syncOne(this->InletSelection);
    syncOne(this->ExcludedOpeningSelection);
  }

  this->InletSelection->Modified();
  this->ExcludedOpeningSelection->Modified();
  ++this->OpeningListRevision;
  this->Modified();

  std::vector<std::string> activeNames;
  std::vector<vtkIdType> activeSurfacePid;
  std::vector<double> activeScalars;
  activeNames.reserve(names.size());
  activeSurfacePid.reserve(names.size());
  activeScalars.reserve(names.size());
  for (size_t i = 0; i < names.size(); ++i)
  {
    if (this->ExcludedOpeningSelection->ArrayIsEnabled(names[i].c_str()))
    {
      continue;
    }
    activeNames.push_back(names[i]);
    activeSurfacePid.push_back(surfacePidPerRegion[i]);
    activeScalars.push_back(openingScalars[i]);
  }

  // Port 1: seed vertices (non-excluded openings only)
  if (!activeNames.empty())
  {
    vtkNew<vtkPoints> spts;
    vtkNew<vtkCellArray> verts;
    vtkNew<vtkDoubleArray> arrPos;
    arrPos->SetName("SeedPosition");
    arrPos->SetNumberOfComponents(3);
    vtkNew<vtkIntArray> arrIdx;
    arrIdx->SetName("OpeningIndex");
    arrIdx->SetNumberOfComponents(1);
    vtkNew<vtkDoubleArray> arrScalar;
    arrScalar->SetName("OpeningArrayValue");
    arrScalar->SetNumberOfComponents(1);
    vtkNew<vtkIdTypeArray> arrSurf;
    arrSurf->SetName("SurfacePointId");
    arrSurf->SetNumberOfComponents(1);

    for (size_t i = 0; i < activeNames.size(); ++i)
    {
      const vtkIdType sid = activeSurfacePid[i];
      double x[3];
      input->GetPoint(sid, x);
      const vtkIdType pid = spts->InsertNextPoint(x);
      verts->InsertNextCell(1);
      verts->InsertCellPoint(pid);
      arrPos->InsertNextTuple3(x[0], x[1], x[2]);
      arrIdx->InsertNextValue(static_cast<int>(i));
      arrScalar->InsertNextValue(activeScalars[i]);
      arrSurf->InsertNextValue(static_cast<vtkTypeInt64>(sid));
    }
    outSeeds->SetPoints(spts);
    outSeeds->SetVerts(verts);
    outSeeds->GetPointData()->AddArray(arrPos);
    outSeeds->GetPointData()->AddArray(arrIdx);
    outSeeds->GetPointData()->AddArray(arrScalar);
    outSeeds->GetPointData()->AddArray(arrSurf);
    vtkNew<vtkIntArray> arrInlet;
    arrInlet->SetName("IsInlet");
    arrInlet->SetNumberOfComponents(1);
    for (size_t i = 0; i < activeNames.size(); ++i)
    {
      const int v = this->InletSelection->ArrayIsEnabled(activeNames[i].c_str()) ? 1 : 0;
      arrInlet->InsertNextValue(v);
    }
    outSeeds->GetPointData()->AddArray(arrInlet);
    outSeeds->GetPointData()->SetActiveScalars("SurfacePointId");
  }

  if (!this->CalculateCenterline)
  {
    this->Modified();
    return 1;
  }

  if (activeNames.size() < 2)
  {
    vtkWarningMacro(
      << "Calculate Centerline requires at least two non-removed openings (found "
      << activeNames.size() << ").");
    this->Modified();
    return 1;
  }

  vtkNew<vtkIdList> sourceIds;
  vtkNew<vtkIdList> targetIds;
  for (size_t i = 0; i < activeNames.size(); ++i)
  {
    const vtkIdType pid = activeSurfacePid[i];
    if (this->InletSelection->ArrayIsEnabled(activeNames[i].c_str()))
    {
      sourceIds->InsertNextId(pid);
    }
    else
    {
      targetIds->InsertNextId(pid);
    }
  }

  if (sourceIds->GetNumberOfIds() < 1 || targetIds->GetNumberOfIds() < 1)
  {
    vtkWarningMacro(<< "Calculate Centerline needs at least one checked inlet (source) and one "
                        "unchecked outlet (target). Adjust Inlets (openings).");
    this->Modified();
    return 1;
  }

  vtkNew<vtkPolyData> vmtkSurface;
  vmtkSurface->ShallowCopy(input);
  EnsurePointGlobalIds(vmtkSurface);

  vtkNew<vtkvmtkPolyDataCenterlines> centerlines;
  centerlines->SetInputData(vmtkSurface);
  centerlines->SetSourceSeedIds(sourceIds);
  centerlines->SetTargetSeedIds(targetIds);
  centerlines->SetRadiusArrayName("MaximumInscribedSphereRadius");
  centerlines->SetFlipNormals(this->FlipNormals);
  centerlines->SetDelaunayTolerance(1e-3);
  centerlines->SetCenterlineResampling(0);
  centerlines->SetResamplingStepLength(1.0);
  centerlines->SetAppendEndPointsToCenterlines(this->AppendEndPointsToCenterlines);
  centerlines->SetSimplifyVoronoi(0);
  centerlines->SetStopFastMarchingOnReachingTarget(this->StopFastMarchingOnReachingTarget);
  centerlines->Update();
  outCenterlines->ShallowCopy(centerlines->GetOutput());

  this->Modified();
  return 1;
}

VTK_ABI_NAMESPACE_END
