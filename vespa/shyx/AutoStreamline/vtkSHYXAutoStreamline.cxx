#include "vtkSHYXAutoStreamline.h"

#include "vtkCellArray.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkDataSetSurfaceFilter.h"
#include "vtkFloatArray.h"
#include "vtkImplicitPolyDataDistance.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMaskPoints.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkSMPTools.h"
#include "vtkStreamTracer.h"
#include "vtkVortexCore.h"

#include <cmath>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
namespace
{

void EnsureVertexCells(vtkPolyData* pd)
{
  if (!pd || pd->GetNumberOfPoints() == 0)
  {
    return;
  }
  if (pd->GetVerts() && pd->GetVerts()->GetNumberOfCells() > 0)
  {
    return;
  }

  const vtkIdType nPts = pd->GetNumberOfPoints();
  vtkNew<vtkCellArray> verts;
  verts->AllocateEstimate(nPts, 1);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    verts->InsertNextCell(1, &i);
  }
  pd->SetVerts(verts);
}

} // namespace

vtkStandardNewMacro(vtkSHYXAutoStreamline);

//------------------------------------------------------------------------------
vtkSHYXAutoStreamline::vtkSHYXAutoStreamline()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
void vtkSHYXAutoStreamline::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "HigherOrderMethod: " << (this->HigherOrderMethod ? "On" : "Off") << "\n";
  os << indent << "FasterApproximation: " << (this->FasterApproximation ? "On" : "Off") << "\n";
  os << indent << "MinSurfaceDistance: " << this->MinSurfaceDistance << "\n";
  os << indent << "MaximumNumberOfPoints: " << this->MaximumNumberOfPoints << "\n";
  os << indent << "RandomSeed: " << this->RandomSeed << "\n";
  os << indent << "IntegrationDirection: " << this->IntegrationDirection << "\n";
  os << indent << "IntegratorType: " << this->IntegratorType << "\n";
  os << indent << "IntegrationStepUnit: " << this->IntegrationStepUnit << "\n";
  os << indent << "InitialIntegrationStep: " << this->InitialIntegrationStep << "\n";
  os << indent << "MaximumPropagation: " << this->MaximumPropagation << "\n";
  os << indent << "MaximumNumberOfSteps: " << this->MaximumNumberOfSteps << "\n";
  os << indent << "TerminalSpeed: " << this->TerminalSpeed << "\n";
  os << indent << "InterpolatorType: " << this->InterpolatorType << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXAutoStreamline::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAutoStreamline::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAutoStreamline::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* volume = vtkDataSet::GetData(inputVector[0]);
  vtkPolyData* streamlinesOut = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* seedsOut = vtkPolyData::GetData(outputVector, 1);

  if (!volume || !streamlinesOut || !seedsOut)
  {
    vtkErrorMacro("Volume flow field (port 0) is required.");
    return 0;
  }

  vtkDataArray* velocity = this->GetInputArrayToProcess(0, inputVector);
  if (!velocity)
  {
    vtkErrorMacro("Could not access input vector field.");
    return 0;
  }

  // -------------------------------------------------------------------------
  // Extract outer surface from the volume
  // -------------------------------------------------------------------------
  vtkNew<vtkDataSetSurfaceFilter> surfaceFilter;
  surfaceFilter->SetInputData(volume);
  surfaceFilter->SetContainerAlgorithm(this);
  surfaceFilter->Update();

  vtkPolyData* surface = surfaceFilter->GetOutput();
  if (!surface || surface->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Extracted volume surface has no cells.");
    return 0;
  }

  if (this->CheckAbort())
  {
    return 1;
  }

  // -------------------------------------------------------------------------
  // 1. Vortex cores
  // -------------------------------------------------------------------------
  vtkNew<vtkVortexCore> vortex;
  vortex->SetInputData(volume);
  vortex->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, velocity->GetName());
  vortex->SetHigherOrderMethod(this->HigherOrderMethod);
  vortex->SetFasterApproximation(this->FasterApproximation);
  vortex->SetContainerAlgorithm(this);
  vortex->Update();

  if (this->CheckAbort())
  {
    return 1;
  }

  vtkPolyData* cores = vortex->GetOutput();
  if (!cores || cores->GetNumberOfPoints() == 0)
  {
    vtkWarningMacro("Vortex cores produced no points; outputs cleared.");
    streamlinesOut->Initialize();
    seedsOut->Initialize();
    return 1;
  }

  // -------------------------------------------------------------------------
  // 2–3. Absolute SDF to extracted surface, cull near-wall points
  // -------------------------------------------------------------------------
  vtkNew<vtkImplicitPolyDataDistance> distance;
  distance->SetInput(surface);

  const vtkIdType nCorePts = cores->GetNumberOfPoints();
  vtkNew<vtkFloatArray> sdf;
  sdf->SetName("SDF");
  sdf->SetNumberOfComponents(1);
  sdf->SetNumberOfTuples(nCorePts);

  vtkSMPTools::For(0, nCorePts, [&](vtkIdType begin, vtkIdType end) {
    for (vtkIdType i = begin; i < end; ++i)
    {
      double q[3];
      cores->GetPoint(i, q);
      sdf->SetTuple1(i, static_cast<float>(std::abs(distance->EvaluateFunction(q))));
    }
  });

  if (this->CheckAbort())
  {
    return 1;
  }

  std::vector<vtkIdType> keptIds;
  keptIds.reserve(static_cast<size_t>(nCorePts));
  for (vtkIdType i = 0; i < nCorePts; ++i)
  {
    if (sdf->GetTuple1(i) >= this->MinSurfaceDistance)
    {
      keptIds.push_back(i);
    }
  }

  if (keptIds.empty())
  {
    vtkWarningMacro("No vortex-core points remain after surface-distance cull; outputs cleared.");
    streamlinesOut->Initialize();
    seedsOut->Initialize();
    return 1;
  }

  vtkNew<vtkPolyData> culled;
  vtkNew<vtkPoints> culledPts;
  culledPts->SetDataType(cores->GetPoints()->GetDataType());
  culledPts->SetNumberOfPoints(static_cast<vtkIdType>(keptIds.size()));

  vtkPointData* corePD = cores->GetPointData();
  vtkPointData* culledPD = culled->GetPointData();
  culledPD->CopyAllocate(corePD, static_cast<vtkIdType>(keptIds.size()));

  vtkNew<vtkFloatArray> culledSdf;
  culledSdf->SetName("SDF");
  culledSdf->SetNumberOfComponents(1);
  culledSdf->SetNumberOfTuples(static_cast<vtkIdType>(keptIds.size()));

  for (vtkIdType j = 0; j < static_cast<vtkIdType>(keptIds.size()); ++j)
  {
    const vtkIdType src = keptIds[static_cast<size_t>(j)];
    double x[3];
    cores->GetPoint(src, x);
    culledPts->SetPoint(j, x);
    culledPD->CopyData(corePD, src, j);
    culledSdf->SetTuple1(j, sdf->GetTuple1(src));
  }
  culled->SetPoints(culledPts);
  culledPD->AddArray(culledSdf);
  EnsureVertexCells(culled);

  // -------------------------------------------------------------------------
  // 4. Uniform spatial (bounds) MaskPoints
  // -------------------------------------------------------------------------
  vtkNew<vtkMaskPoints> mask;
  mask->SetInputData(culled);
  mask->SetMaximumNumberOfPoints(this->MaximumNumberOfPoints);
  mask->RandomModeOn();
  mask->SetRandomModeType(vtkMaskPoints::UNIFORM_SPATIAL_BOUNDS);
  mask->SetRandomSeed(this->RandomSeed);
  mask->GenerateVerticesOn();
  mask->SetContainerAlgorithm(this);
  mask->Update();

  if (this->CheckAbort())
  {
    return 1;
  }

  vtkPolyData* seeds = mask->GetOutput();
  if (!seeds || seeds->GetNumberOfPoints() == 0)
  {
    vtkWarningMacro("MaskPoints produced no seed points; outputs cleared.");
    streamlinesOut->Initialize();
    seedsOut->Initialize();
    return 1;
  }

  seedsOut->ShallowCopy(seeds);

  // -------------------------------------------------------------------------
  // 5. StreamTracer from seeds
  // -------------------------------------------------------------------------
  vtkNew<vtkStreamTracer> tracer;
  tracer->SetInputData(volume);
  tracer->SetSourceData(seeds);
  tracer->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, velocity->GetName());
  tracer->SetIntegrationDirection(this->IntegrationDirection);
  tracer->SetIntegratorType(this->IntegratorType);
  tracer->SetIntegrationStepUnit(this->IntegrationStepUnit);
  tracer->SetInitialIntegrationStep(this->InitialIntegrationStep);
  tracer->SetMaximumPropagation(this->MaximumPropagation);
  tracer->SetMaximumNumberOfSteps(this->MaximumNumberOfSteps);
  tracer->SetTerminalSpeed(this->TerminalSpeed);
  tracer->SetInterpolatorType(this->InterpolatorType);
  tracer->SetContainerAlgorithm(this);
  tracer->Update();

  if (this->CheckAbort())
  {
    return 1;
  }

  vtkPolyData* lines = tracer->GetOutput();
  if (!lines)
  {
    streamlinesOut->Initialize();
    return 1;
  }

  streamlinesOut->ShallowCopy(lines);
  return 1;
}

VTK_ABI_NAMESPACE_END
