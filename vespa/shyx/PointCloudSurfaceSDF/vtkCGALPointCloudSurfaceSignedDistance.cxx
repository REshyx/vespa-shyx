#include "vtkCGALPointCloudSurfaceSignedDistance.h"

#include <vtkCellArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkFloatArray.h>
#include <vtkImplicitPolyDataDistance.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSMPTools.h>

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
  if (pd->GetNumberOfPolys() > 0 || pd->GetNumberOfLines() > 0 || pd->GetNumberOfStrips() > 0)
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

vtkPolyData* ToPointCloudPolyData(vtkDataSet* input, vtkPolyData* cache)
{
  if (!input)
  {
    return nullptr;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(input))
  {
    return pd;
  }

  cache->Initialize();
  const vtkIdType nPts = input->GetNumberOfPoints();
  vtkNew<vtkPoints> pts;
  pts->SetNumberOfPoints(nPts);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    double x[3];
    input->GetPoint(i, x);
    pts->SetPoint(i, x);
  }
  cache->SetPoints(pts);

  vtkPointData* inPD = input->GetPointData();
  vtkPointData* outPD = cache->GetPointData();
  outPD->CopyAllocate(inPD, nPts);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    outPD->CopyData(inPD, i, i);
  }
  EnsureVertexCells(cache);
  return cache;
}

vtkPolyData* ToSurfacePolyData(vtkDataSet* input, vtkPolyData* cache)
{
  if (!input)
  {
    return nullptr;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(input))
  {
    return pd;
  }

  vtkNew<vtkDataSetSurfaceFilter> surface;
  surface->SetInputData(input);
  surface->Update();
  cache->ShallowCopy(surface->GetOutput());
  return cache;
}

} // namespace

vtkStandardNewMacro(vtkCGALPointCloudSurfaceSignedDistance);

//------------------------------------------------------------------------------
vtkCGALPointCloudSurfaceSignedDistance::vtkCGALPointCloudSurfaceSignedDistance()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkCGALPointCloudSurfaceSignedDistance::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkCGALPointCloudSurfaceSignedDistance::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
int vtkCGALPointCloudSurfaceSignedDistance::FillInputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkCGALPointCloudSurfaceSignedDistance::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* pointCloudIn = vtkDataSet::GetData(inputVector[0]);
  vtkDataSet* surfaceInDs  = vtkDataSet::GetData(inputVector[1]);
  vtkPolyData* output      = vtkPolyData::GetData(outputVector);

  vtkNew<vtkPolyData> pointCloudWork;
  vtkNew<vtkPolyData> surfaceWork;
  vtkPolyData* pointCloud = ToPointCloudPolyData(pointCloudIn, pointCloudWork);
  vtkPolyData* surfaceIn  = ToSurfacePolyData(surfaceInDs, surfaceWork);

  if (!pointCloud || !surfaceIn)
  {
    vtkErrorMacro("Point cloud (port 0) and surface (port 1) are required.");
    return 0;
  }

  if (pointCloud->GetNumberOfPoints() == 0)
  {
    vtkErrorMacro("Point cloud has no points.");
    return 0;
  }

  if (surfaceIn->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Surface has no cells.");
    return 0;
  }

  vtkNew<vtkImplicitPolyDataDistance> distance;
  distance->SetInput(surfaceIn);

  output->ShallowCopy(pointCloud);
  EnsureVertexCells(output);

  const vtkIdType nPts = pointCloud->GetNumberOfPoints();
  vtkNew<vtkFloatArray> sdf;
  sdf->SetName("SDF");
  sdf->SetNumberOfComponents(1);
  sdf->SetNumberOfTuples(nPts);

  vtkSMPTools::For(0, nPts, [&](vtkIdType begin, vtkIdType end) {
    for (vtkIdType i = begin; i < end; ++i)
    {
      double q[3];
      pointCloud->GetPoint(i, q);
      const float v = static_cast<float>(distance->EvaluateFunction(q));
      sdf->SetTuple1(i, v);
    }
  });

  output->GetPointData()->RemoveArray("SDF");
  output->GetPointData()->AddArray(sdf);
  return 1;
}
