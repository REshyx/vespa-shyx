#include "vtkSHYXVmtkPolyDataCenterlines.h"

#include <cmath>
#include <string>
#include <unordered_map>

#include <vtkAlgorithmOutput.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkLongArray.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkTypeInt64Array.h>
#include <vtkUnsignedIntArray.h>
#include <vtkUnsignedLongArray.h>

#include "vtkvmtkPolyDataCenterlines.h"

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXVmtkPolyDataCenterlines);

namespace
{
vtkDataArray* GetPointGlobalIdsArray(vtkDataSet* ds)
{
  if (!ds)
  {
    return nullptr;
  }
  vtkPointData* ptd = ds->GetPointData();
  if (!ptd)
  {
    return nullptr;
  }
  const vtkIdType nPts = ds->GetNumberOfPoints();
  vtkDataArray* g = vtkDataArray::SafeDownCast(ptd->GetGlobalIds());
  if (g && g->GetNumberOfTuples() == nPts && g->GetNumberOfComponents() == 1)
  {
    return g;
  }
  g = vtkDataArray::SafeDownCast(ptd->GetAbstractArray("GlobalIds"));
  if (g && g->GetNumberOfTuples() == nPts && g->GetNumberOfComponents() == 1)
  {
    return g;
  }
  g = vtkDataArray::SafeDownCast(ptd->GetAbstractArray("GlobalId"));
  if (g && g->GetNumberOfTuples() == nPts && g->GetNumberOfComponents() == 1)
  {
    return g;
  }
  return nullptr;
}

long long ReadGlobalId(vtkDataArray* arr, vtkIdType i)
{
  switch (arr->GetDataType())
  {
    case VTK_ID_TYPE:
      return static_cast<long long>(vtkIdTypeArray::SafeDownCast(arr)->GetValue(i));
    case VTK_INT:
      return static_cast<long long>(vtkIntArray::SafeDownCast(arr)->GetValue(i));
    case VTK_UNSIGNED_INT:
      return static_cast<long long>(vtkUnsignedIntArray::SafeDownCast(arr)->GetValue(i));
    case VTK_LONG:
      return static_cast<long long>(vtkLongArray::SafeDownCast(arr)->GetValue(i));
    case VTK_UNSIGNED_LONG:
      return static_cast<long long>(vtkUnsignedLongArray::SafeDownCast(arr)->GetValue(i));
    case VTK_TYPE_INT64:
      // VTK_LONG_LONG can equal VTK_TYPE_INT64 on some platforms; do not duplicate case labels.
      return static_cast<long long>(vtkTypeInt64Array::SafeDownCast(arr)->GetValue(i));
    default:
      return static_cast<long long>(std::llround(arr->GetTuple1(i)));
  }
}

bool BuildSurfaceGlobalIdToPointId(vtkPolyData* surface, vtkDataArray* surfaceGids,
  std::unordered_map<long long, vtkIdType>& outMap, std::string& err)
{
  outMap.clear();
  const vtkIdType n = surface->GetNumberOfPoints();
  for (vtkIdType i = 0; i < n; ++i)
  {
    const long long gid = ReadGlobalId(surfaceGids, i);
    const auto ins = outMap.insert({ gid, i });
    if (!ins.second)
    {
      err = "Duplicate GlobalId " + std::to_string(gid) + " on surface (port 0).";
      return false;
    }
  }
  return true;
}

bool MapSeedGlobalIdsToSurfaceIds(vtkDataSet* seeds, vtkDataArray* seedGids,
  const std::unordered_map<long long, vtkIdType>& surfaceMap, vtkIdList* outIds, std::string& err)
{
  outIds->Initialize();
  const vtkIdType n = seeds->GetNumberOfPoints();
  for (vtkIdType i = 0; i < n; ++i)
  {
    const long long gid = ReadGlobalId(seedGids, i);
    const auto it = surfaceMap.find(gid);
    if (it == surfaceMap.end())
    {
      err = "GlobalId " + std::to_string(gid) + " from seeds not found on surface (port 0).";
      return false;
    }
    outIds->InsertNextId(it->second);
  }
  return true;
}
} // namespace

vtkSHYXVmtkPolyDataCenterlines::vtkSHYXVmtkPolyDataCenterlines()
{
  this->SetNumberOfInputPorts(3);
  this->SetNumberOfOutputPorts(1);
}

void vtkSHYXVmtkPolyDataCenterlines::SetSourceSeedsConnection(vtkAlgorithmOutput* output)
{
  this->SetInputConnection(1, output);
}

void vtkSHYXVmtkPolyDataCenterlines::SetTargetSeedsConnection(vtkAlgorithmOutput* output)
{
  this->SetInputConnection(2, output);
}

void vtkSHYXVmtkPolyDataCenterlines::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FlipNormals: " << this->FlipNormals << "\n";
  os << indent << "DelaunayTolerance: " << this->DelaunayTolerance << "\n";
  os << indent << "CenterlineResampling: " << this->CenterlineResampling << "\n";
  os << indent << "ResamplingStepLength: " << this->ResamplingStepLength << "\n";
  os << indent << "AppendEndPointsToCenterlines: " << this->AppendEndPointsToCenterlines << "\n";
  os << indent << "SimplifyVoronoi: " << this->SimplifyVoronoi << "\n";
  os << indent << "StopFastMarchingOnReachingTarget: " << this->StopFastMarchingOnReachingTarget
     << "\n";
}

int vtkSHYXVmtkPolyDataCenterlines::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  if (port == 1 || port == 2)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

int vtkSHYXVmtkPolyDataCenterlines::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector,
  vtkInformationVector* outputVector)
{
  vtkPolyData* surface = vtkPolyData::GetData(inputVector[0], 0);
  vtkDataSet* sourceSeeds = vtkDataSet::GetData(inputVector[1], 0);
  vtkDataSet* targetSeeds = vtkDataSet::GetData(inputVector[2], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

  if (!surface || surface->GetNumberOfPoints() < 3 || surface->GetNumberOfCells() < 1)
  {
    vtkErrorMacro(<< "Port 0 must be a surface vtkPolyData with geometry.");
    return 0;
  }
  if (!sourceSeeds || sourceSeeds->GetNumberOfPoints() < 1)
  {
    vtkErrorMacro(<< "Port 1 (source seeds) must have at least one point.");
    return 0;
  }
  if (!targetSeeds || targetSeeds->GetNumberOfPoints() < 1)
  {
    vtkErrorMacro(<< "Port 2 (target seeds) must have at least one point.");
    return 0;
  }

  vtkDataArray* surfaceGids = GetPointGlobalIdsArray(surface);
  if (!surfaceGids)
  {
    vtkErrorMacro(<< "Port 0 (surface) must have point GlobalIds: set point GlobalIds attribute, or "
                      "a point array named GlobalIds / GlobalId.");
    return 0;
  }
  vtkDataArray* sourceGids = GetPointGlobalIdsArray(sourceSeeds);
  if (!sourceGids)
  {
    vtkErrorMacro(<< "Port 1 (source seeds) must have point GlobalIds: set point GlobalIds "
                      "attribute, or a point array named GlobalIds / GlobalId.");
    return 0;
  }
  vtkDataArray* targetGids = GetPointGlobalIdsArray(targetSeeds);
  if (!targetGids)
  {
    vtkErrorMacro(<< "Port 2 (target seeds) must have point GlobalIds: set point GlobalIds "
                      "attribute, or a point array named GlobalIds / GlobalId.");
    return 0;
  }

  std::unordered_map<long long, vtkIdType> surfaceMap;
  std::string err;
  if (!BuildSurfaceGlobalIdToPointId(surface, surfaceGids, surfaceMap, err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }

  vtkNew<vtkIdList> sourceIds;
  vtkNew<vtkIdList> targetIds;
  if (!MapSeedGlobalIdsToSurfaceIds(sourceSeeds, sourceGids, surfaceMap, sourceIds, err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }
  if (!MapSeedGlobalIdsToSurfaceIds(targetSeeds, targetGids, surfaceMap, targetIds, err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }

  vtkNew<vtkvmtkPolyDataCenterlines> centerlines;
  centerlines->SetInputData(surface);
  centerlines->SetSourceSeedIds(sourceIds);
  centerlines->SetTargetSeedIds(targetIds);
  // Name used for Voronoi circumsphere radii and output centerline radius field (VMTK requires it).
  centerlines->SetRadiusArrayName("MaximumInscribedSphereRadius");
  centerlines->SetFlipNormals(this->FlipNormals);
  centerlines->SetDelaunayTolerance(this->DelaunayTolerance);
  centerlines->SetCenterlineResampling(this->CenterlineResampling);
  centerlines->SetResamplingStepLength(this->ResamplingStepLength);
  centerlines->SetAppendEndPointsToCenterlines(this->AppendEndPointsToCenterlines);
  centerlines->SetSimplifyVoronoi(this->SimplifyVoronoi);
  centerlines->SetStopFastMarchingOnReachingTarget(this->StopFastMarchingOnReachingTarget);
  centerlines->Update();

  output->ShallowCopy(centerlines->GetOutput());
  return 1;
}

VTK_ABI_NAMESPACE_END
