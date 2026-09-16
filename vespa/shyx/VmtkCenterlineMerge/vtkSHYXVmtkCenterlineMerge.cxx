#include "vtkSHYXVmtkCenterlineMerge.h"

#include "vtkvmtkCenterlineBranchExtractor.h"
#include "vtkvmtkMergeCenterlines.h"
#include "vtkvmtkPolyBallModeller.h"

#include <vtkAppendPolyData.h>
#include <vtkBoundingBox.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkFlyingEdges3D.h>
#include <vtkImageData.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXVmtkCenterlineMerge);

namespace
{
constexpr const char kRadiusArrayName[] = "MaximumInscribedSphereRadius";
constexpr const char kGroupIdsArrayName[] = "GroupIds";
constexpr const char kCenterlineIdsArrayName[] = "CenterlineIds";
constexpr const char kTractIdsArrayName[] = "TractIds";
constexpr const char kBlankingArrayName[] = "Blanking";

bool CopyIfNonEmpty(vtkPolyData* dst, vtkPolyData* src)
{
  if (!dst || !src || src->GetNumberOfCells() == 0)
  {
    return false;
  }
  dst->ShallowCopy(src);
  return true;
}

double LongestBBoxSide(vtkPolyData* pd)
{
  if (!pd)
  {
    return 0.0;
  }
  double bounds[6];
  pd->GetBounds(bounds);
  vtkBoundingBox box(bounds);
  return box.GetMaxLength();
}

vtkIdType CountLineCells(vtkPolyData* pd)
{
  if (!pd)
  {
    return 0;
  }
  vtkIdType n = 0;
  const vtkIdType nCells = pd->GetNumberOfCells();
  for (vtkIdType i = 0; i < nCells; ++i)
  {
    const int t = pd->GetCellType(i);
    if (t == VTK_LINE || t == VTK_POLY_LINE)
    {
      ++n;
    }
  }
  return n;
}

bool HasBranchGroupArrays(vtkPolyData* pd)
{
  if (!pd || !pd->GetCellData() || pd->GetNumberOfCells() == 0)
  {
    return false;
  }
  vtkCellData* cd = pd->GetCellData();
  auto has = [&](const char* name) -> bool {
    vtkDataArray* a = cd->GetArray(name);
    return a && a->GetNumberOfTuples() == pd->GetNumberOfCells();
  };
  return has(kGroupIdsArrayName) && has(kCenterlineIdsArrayName) && has(kTractIdsArrayName) &&
    has(kBlankingArrayName);
}

bool ReconstructPolyball(vtkPolyData* lines, const char* radiusName, const int sampleDim[3],
  double isoValue, vtkPolyData* outSurface, vtkObject* self)
{
  vtkDataArray* radius =
    lines && lines->GetPointData() ? lines->GetPointData()->GetArray(radiusName) : nullptr;
  if (!radius || radius->GetNumberOfTuples() != lines->GetNumberOfPoints())
  {
    vtkErrorWithObjectMacro(self, << "Reconstruct needs radius array '" << radiusName
                                  << "' on the centerlines being modelled.");
    return false;
  }

  int dims[3] = { std::max(sampleDim[0], 2), std::max(sampleDim[1], 2),
    std::max(sampleDim[2], 2) };

  vtkNew<vtkvmtkPolyBallModeller> modeller;
  modeller->SetInputData(lines);
  modeller->SetRadiusArrayName(radiusName);
  modeller->UsePolyBallLineOn();
  modeller->SetNegateFunction(0);
  modeller->SetSampleDimensions(dims);
  modeller->Update();
  vtkImageData* image = modeller->GetOutput();
  if (!image || image->GetNumberOfPoints() == 0)
  {
    vtkErrorWithObjectMacro(self, "Polyball modeller produced an empty image.");
    return false;
  }

  vtkNew<vtkFlyingEdges3D> iso;
  iso->SetInputData(image);
  iso->SetValue(0, isoValue);
  iso->ComputeNormalsOn();
  iso->Update();
  if (!CopyIfNonEmpty(outSurface, iso->GetOutput()))
  {
    vtkErrorWithObjectMacro(self, << "Isosurface at " << isoValue
                                  << " is empty. Try larger SampleDimensions or a slightly "
                                     "positive IsoValue.");
    return false;
  }
  return true;
}

vtkIdType FindRoot(std::vector<vtkIdType>& parent, vtkIdType i)
{
  while (parent[static_cast<size_t>(i)] != i)
  {
    parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
    i = parent[static_cast<size_t>(i)];
  }
  return i;
}

void UnionIds(std::vector<vtkIdType>& parent, vtkIdType a, vtkIdType b)
{
  a = FindRoot(parent, a);
  b = FindRoot(parent, b);
  if (a != b)
  {
    parent[static_cast<size_t>(b)] = a;
  }
}

bool EndpointsNear(const double a[3], double ra, const double b[3], double rb)
{
  const double lim = std::max(ra, rb);
  if (lim <= 0.0)
  {
    return false;
  }
  return vtkMath::Distance2BetweenPoints(a, b) <= lim * lim;
}

std::vector<std::vector<vtkIdType>> PartitionBySharedEndpoints(
  vtkPolyData* pd, vtkDataArray* radiusArr)
{
  std::vector<std::vector<vtkIdType>> clusters;
  if (!pd || !radiusArr)
  {
    return clusters;
  }
  const vtkIdType nCells = pd->GetNumberOfCells();
  std::vector<vtkIdType> lineCells;
  struct Ends
  {
    double p0[3];
    double p1[3];
    double r0;
    double r1;
  };
  std::vector<Ends> ends;
  for (vtkIdType i = 0; i < nCells; ++i)
  {
    const int t = pd->GetCellType(i);
    if (t != VTK_LINE && t != VTK_POLY_LINE)
    {
      continue;
    }
    vtkCell* cell = pd->GetCell(i);
    const vtkIdType np = cell->GetNumberOfPoints();
    if (np < 2)
    {
      continue;
    }
    Ends e{};
    cell->GetPoints()->GetPoint(0, e.p0);
    cell->GetPoints()->GetPoint(np - 1, e.p1);
    e.r0 = radiusArr->GetTuple1(cell->GetPointId(0));
    e.r1 = radiusArr->GetTuple1(cell->GetPointId(np - 1));
    lineCells.push_back(i);
    ends.push_back(e);
  }
  const vtkIdType n = static_cast<vtkIdType>(lineCells.size());
  if (n < 1)
  {
    return clusters;
  }
  std::vector<vtkIdType> parent(static_cast<size_t>(n));
  for (vtkIdType i = 0; i < n; ++i)
  {
    parent[static_cast<size_t>(i)] = i;
  }
  for (vtkIdType i = 0; i < n; ++i)
  {
    for (vtkIdType j = i + 1; j < n; ++j)
    {
      const Ends& a = ends[static_cast<size_t>(i)];
      const Ends& b = ends[static_cast<size_t>(j)];
      if (EndpointsNear(a.p0, a.r0, b.p0, b.r0) || EndpointsNear(a.p0, a.r0, b.p1, b.r1) ||
        EndpointsNear(a.p1, a.r1, b.p0, b.r0) || EndpointsNear(a.p1, a.r1, b.p1, b.r1))
      {
        UnionIds(parent, i, j);
      }
    }
  }
  std::vector<std::vector<vtkIdType>> buckets(static_cast<size_t>(n));
  for (vtkIdType i = 0; i < n; ++i)
  {
    buckets[static_cast<size_t>(FindRoot(parent, i))].push_back(lineCells[static_cast<size_t>(i)]);
  }
  for (auto& b : buckets)
  {
    if (!b.empty())
    {
      clusters.push_back(std::move(b));
    }
  }
  return clusters;
}

vtkSmartPointer<vtkPolyData> CopyLineCells(
  vtkPolyData* src, const std::vector<vtkIdType>& cellIds, const char* radiusName)
{
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  if (!src || cellIds.empty())
  {
    return out;
  }
  vtkDataArray* srcR = src->GetPointData() ? src->GetPointData()->GetArray(radiusName) : nullptr;
  vtkNew<vtkPoints> pts;
  if (src->GetPoints())
  {
    pts->SetDataType(src->GetPoints()->GetDataType());
  }
  vtkNew<vtkCellArray> lines;
  vtkNew<vtkDoubleArray> rad;
  rad->SetName(radiusName);
  rad->SetNumberOfComponents(1);
  for (vtkIdType cid : cellIds)
  {
    vtkCell* cell = src->GetCell(cid);
    if (!cell)
    {
      continue;
    }
    const vtkIdType np = cell->GetNumberOfPoints();
    if (np < 2)
    {
      continue;
    }
    lines->InsertNextCell(np);
    for (vtkIdType k = 0; k < np; ++k)
    {
      const vtkIdType oldId = cell->GetPointId(k);
      double q[3];
      src->GetPoint(oldId, q);
      const vtkIdType nid = pts->InsertNextPoint(q);
      lines->InsertCellPoint(nid);
      rad->InsertNextValue(srcR ? srcR->GetTuple1(oldId) : 0.0);
    }
  }
  out->SetPoints(pts);
  out->SetLines(lines);
  out->GetPointData()->AddArray(rad);
  out->GetPointData()->SetActiveScalars(radiusName);
  return out;
}

vtkSmartPointer<vtkPolyData> RunBranchExtract(vtkPolyData* src, const char* radiusName)
{
  vtkNew<vtkvmtkCenterlineBranchExtractor> extractor;
  extractor->SetInputData(src);
  extractor->SetRadiusArrayName(radiusName);
  extractor->SetGroupIdsArrayName(kGroupIdsArrayName);
  extractor->SetCenterlineIdsArrayName(kCenterlineIdsArrayName);
  extractor->SetTractIdsArrayName(kTractIdsArrayName);
  extractor->SetBlankingArrayName(kBlankingArrayName);
  extractor->Update();
  vtkPolyData* tracts = extractor->GetOutput();
  if (!tracts || tracts->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  out->DeepCopy(tracts);
  return out;
}

vtkSmartPointer<vtkPolyData> RunMergeCenterlines(
  vtkPolyData* src, const char* radiusName, double step, int mergeBlanked)
{
  vtkNew<vtkvmtkMergeCenterlines> merger;
  merger->SetInputData(src);
  merger->SetRadiusArrayName(radiusName);
  merger->SetGroupIdsArrayName(kGroupIdsArrayName);
  merger->SetCenterlineIdsArrayName(kCenterlineIdsArrayName);
  merger->SetTractIdsArrayName(kTractIdsArrayName);
  merger->SetBlankingArrayName(kBlankingArrayName);
  merger->SetResamplingStepLength(step);
  merger->SetMergeBlanked(mergeBlanked);
  merger->Update();
  vtkPolyData* merged = merger->GetOutput();
  if (!merged || merged->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  out->DeepCopy(merged);
  return out;
}

void OffsetCellArray(vtkPolyData* pd, const char* name, double offset)
{
  if (!pd || !name || offset == 0.0)
  {
    return;
  }
  vtkDataArray* a = pd->GetCellData() ? pd->GetCellData()->GetArray(name) : nullptr;
  if (!a)
  {
    return;
  }
  for (vtkIdType i = 0; i < a->GetNumberOfTuples(); ++i)
  {
    a->SetTuple1(i, a->GetTuple1(i) + offset);
  }
}

double MaxTuple(vtkPolyData* pd, const char* name)
{
  vtkDataArray* a = pd && pd->GetCellData() ? pd->GetCellData()->GetArray(name) : nullptr;
  if (!a || a->GetNumberOfTuples() < 1)
  {
    return -1.0;
  }
  double m = a->GetTuple1(0);
  for (vtkIdType i = 1; i < a->GetNumberOfTuples(); ++i)
  {
    m = std::max(m, a->GetTuple1(i));
  }
  return m;
}

vtkSmartPointer<vtkPolyData> AppendPieces(const std::vector<vtkSmartPointer<vtkPolyData>>& pieces)
{
  vtkNew<vtkAppendPolyData> app;
  for (const auto& p : pieces)
  {
    if (p && p->GetNumberOfCells() > 0)
    {
      app->AddInputData(p);
    }
  }
  if (app->GetNumberOfInputConnections(0) < 1)
  {
    return nullptr;
  }
  app->Update();
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  out->ShallowCopy(app->GetOutput());
  return out;
}

vtkSmartPointer<vtkPolyData> AppendWithUniqueGroupIds(
  const std::vector<vtkSmartPointer<vtkPolyData>>& pieces)
{
  std::vector<vtkSmartPointer<vtkPolyData>> shifted;
  double gOff = 0.0;
  double cOff = 0.0;
  for (const auto& p : pieces)
  {
    if (!p || p->GetNumberOfCells() < 1)
    {
      continue;
    }
    vtkSmartPointer<vtkPolyData> q = vtkSmartPointer<vtkPolyData>::New();
    q->DeepCopy(p);
    OffsetCellArray(q, kGroupIdsArrayName, gOff);
    OffsetCellArray(q, kCenterlineIdsArrayName, cOff);
    gOff = MaxTuple(q, kGroupIdsArrayName) + 1.0;
    cOff = MaxTuple(q, kCenterlineIdsArrayName) + 1.0;
    shifted.push_back(q);
  }
  return AppendPieces(shifted);
}
} // namespace

vtkSHYXVmtkCenterlineMerge::vtkSHYXVmtkCenterlineMerge()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, kRadiusArrayName);
}

void vtkSHYXVmtkCenterlineMerge::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ExtractCenterlineBranches: " << this->ExtractCenterlineBranches << "\n";
  os << indent << "MergeCenterlines: " << this->MergeCenterlines << "\n";
  os << indent << "ResamplingStepLength: " << this->ResamplingStepLength << "\n";
  os << indent << "MergeBlanked: " << this->MergeBlanked << "\n";
  os << indent << "SplitBySharedEndpoints: " << this->SplitBySharedEndpoints << "\n";
  os << indent << "ReconstructSurface: " << this->ReconstructSurface << "\n";
  os << indent << "SampleDimensions: (" << this->SampleDimensions[0] << ", "
     << this->SampleDimensions[1] << ", " << this->SampleDimensions[2] << ")\n";
  os << indent << "IsoValue: " << this->IsoValue << "\n";
}

int vtkSHYXVmtkCenterlineMerge::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXVmtkCenterlineMerge::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

bool vtkSHYXVmtkCenterlineMerge::CanReuseExtractCache(
  vtkPolyData* input, vtkDataArray* radiusArr, const char* radiusName) const
{
  if (!input || !radiusArr || !radiusName || !radiusName[0] ||
    this->ExtractedClusterCache.empty() || this->ExtractedCache->GetNumberOfCells() < 1)
  {
    return false;
  }
  vtkPoints* pts = input->GetPoints();
  vtkCellArray* lines = input->GetLines();
  const vtkMTimeType pointsMTime = pts ? pts->GetMTime() : 0;
  const vtkMTimeType linesMTime = lines ? lines->GetMTime() : 0;
  return this->ExtractCacheSplitByEndpoints == this->SplitBySharedEndpoints &&
    this->ExtractCachePointsMTime == pointsMTime &&
    this->ExtractCacheLinesMTime == linesMTime &&
    this->ExtractCacheRadiusMTime == radiusArr->GetMTime() &&
    this->ExtractCacheInputNPoints == input->GetNumberOfPoints() &&
    this->ExtractCacheInputNCells == input->GetNumberOfCells() &&
    this->ExtractCacheRadiusName == radiusName;
}

void vtkSHYXVmtkCenterlineMerge::CaptureExtractCacheKey(
  vtkPolyData* input, vtkDataArray* radiusArr, const char* radiusName)
{
  vtkPoints* pts = input ? input->GetPoints() : nullptr;
  vtkCellArray* lines = input ? input->GetLines() : nullptr;
  this->ExtractCachePointsMTime = pts ? pts->GetMTime() : 0;
  this->ExtractCacheLinesMTime = lines ? lines->GetMTime() : 0;
  this->ExtractCacheRadiusMTime = radiusArr ? radiusArr->GetMTime() : 0;
  this->ExtractCacheInputNPoints = input ? input->GetNumberOfPoints() : -1;
  this->ExtractCacheInputNCells = input ? input->GetNumberOfCells() : -1;
  this->ExtractCacheRadiusName = radiusName ? radiusName : "";
  this->ExtractCacheSplitByEndpoints = this->SplitBySharedEndpoints;
}

int vtkSHYXVmtkCenterlineMerge::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }

  output->Initialize();

  if (!this->ExtractCenterlineBranches && !this->MergeCenterlines && !this->ReconstructSurface)
  {
    output->ShallowCopy(input);
    return 1;
  }

  vtkDataArray* radiusArr = this->GetInputArrayToProcess(0, inputVector);
  if (!radiusArr || radiusArr->GetNumberOfTuples() != input->GetNumberOfPoints() ||
    radiusArr->GetNumberOfComponents() < 1)
  {
    vtkErrorMacro(<< "Need a point-data radius array (default " << kRadiusArrayName
                  << ") with one tuple per centerline point.");
    return 0;
  }

  const char* radiusName = radiusArr->GetName();
  if (!radiusName || radiusName[0] == '\0')
  {
    radiusName = kRadiusArrayName;
  }

  const vtkIdType nLines = CountLineCells(input);
  if (nLines < 1)
  {
    vtkErrorMacro("Input has no LINE / POLY_LINE cells. Pass overlapping centerline polylines "
                  "(e.g. Append Geometry of Voronoi source–target paths).");
    return 0;
  }

  vtkPolyData* current = input;
  vtkNew<vtkPolyData> extractHolder;
  vtkSmartPointer<vtkPolyData> mergedHolder;

  if (this->ExtractCenterlineBranches)
  {
    if (this->CanReuseExtractCache(input, radiusArr, radiusName))
    {
      vtkWarningMacro("Extract cache hit; reusing " << this->ExtractedClusterCache.size()
                                                    << " cluster(s), "
                                                    << this->ExtractedCache->GetNumberOfCells()
                                                    << " tracts.");
      this->SetProgressText("Using cached branch extract");
    }
    else
    {
      vtkWarningMacro("Extract cache miss; running vtkvmtkCenterlineBranchExtractor.");
      this->SetProgressText("Extracting centerline branches");
      // Snapshot geometry identity before the extractor may BuildCells() on input.
      this->CaptureExtractCacheKey(input, radiusArr, radiusName);
      this->ExtractedClusterCache.clear();
      this->ExtractedCache->Initialize();

      std::vector<std::vector<vtkIdType>> groups;
      if (this->SplitBySharedEndpoints)
      {
        groups = PartitionBySharedEndpoints(input, radiusArr);
        vtkWarningMacro("Split by shared endpoints: " << groups.size()
                                                      << " cluster(s); extract"
                                                      << (this->MergeCenterlines ? "+merge" : "")
                                                      << " each.");
      }

      if (!this->SplitBySharedEndpoints || groups.size() <= 1)
      {
        vtkSmartPointer<vtkPolyData> extracted = RunBranchExtract(input, radiusName);
        if (!extracted)
        {
          vtkErrorMacro("Branch extractor produced no cells. Input should be full overlapping "
                        "source–target polylines with a valid radius array, not already-split short "
                        "segments.");
          return 0;
        }
        this->ExtractedClusterCache.push_back(extracted);
      }
      else
      {
        for (size_t i = 0; i < groups.size(); ++i)
        {
          vtkSmartPointer<vtkPolyData> piece = CopyLineCells(input, groups[i], radiusName);
          if (!piece || piece->GetNumberOfCells() < 1)
          {
            this->ExtractedClusterCache.clear();
            vtkErrorMacro("Empty cluster " << i << " after endpoint split.");
            return 0;
          }
          vtkSmartPointer<vtkPolyData> extracted = RunBranchExtract(piece, radiusName);
          if (!extracted)
          {
            this->ExtractedClusterCache.clear();
            vtkErrorMacro("Branch extractor produced no cells for cluster "
              << i << " (" << groups[i].size() << " polylines).");
            return 0;
          }
          this->ExtractedClusterCache.push_back(extracted);
        }
      }

      vtkSmartPointer<vtkPolyData> combined = AppendWithUniqueGroupIds(this->ExtractedClusterCache);
      if (!combined)
      {
        this->ExtractedClusterCache.clear();
        vtkErrorMacro("Failed to append extracted clusters.");
        return 0;
      }
      this->ExtractedCache->DeepCopy(combined);
    }

    extractHolder->DeepCopy(this->ExtractedCache);
    vtkDataArray* groupedRadius =
      extractHolder->GetPointData() ? extractHolder->GetPointData()->GetArray(radiusName) : nullptr;
    if (!groupedRadius || groupedRadius->GetNumberOfTuples() != extractHolder->GetNumberOfPoints())
    {
      this->ExtractedCache->Initialize();
      this->ExtractedClusterCache.clear();
      vtkErrorMacro(<< "Branch extractor dropped radius array '" << radiusName << "'.");
      return 0;
    }
    current = extractHolder;
  }

  if (this->MergeCenterlines && this->ExtractCenterlineBranches)
  {
    double step = this->ResamplingStepLength;
    if (step <= 0.0)
    {
      step = 0.01 * std::max(LongestBBoxSide(input), 0.0);
    }
    if (step <= 0.0)
    {
      vtkErrorMacro("Resampling step length is not positive.");
      return 0;
    }

    std::vector<vtkSmartPointer<vtkPolyData>> mergedPieces;
    for (size_t i = 0; i < this->ExtractedClusterCache.size(); ++i)
    {
      vtkPolyData* src = this->ExtractedClusterCache[i];
      if (!HasBranchGroupArrays(src))
      {
        vtkErrorMacro("Merge needs GroupIds / CenterlineIds / TractIds / Blanking. Turn on Extract "
                      "branches, or pass already-split centerlines from a previous Extract.");
        return 0;
      }
      vtkSmartPointer<vtkPolyData> merged =
        RunMergeCenterlines(src, radiusName, step, this->MergeBlanked);
      if (!merged)
      {
        vtkErrorMacro("Merge centerlines produced empty output for cluster " << i << ".");
        return 0;
      }
      mergedPieces.push_back(merged);
    }
    mergedHolder = AppendWithUniqueGroupIds(mergedPieces);
    if (!mergedHolder)
    {
      vtkErrorMacro("Merge centerlines produced empty output.");
      return 0;
    }
    current = mergedHolder;
  }

  if (this->ReconstructSurface)
  {
    if (!ReconstructPolyball(
          current, radiusName, this->SampleDimensions, this->IsoValue, output, this))
    {
      return 0;
    }
    return 1;
  }

  output->ShallowCopy(current);
  return 1;
}

VTK_ABI_NAMESPACE_END
