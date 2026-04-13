#include "vtkCGALPointCloudSurfaceSignedDistance.h"

#include <vtkFloatArray.h>
#include <vtkImplicitPolyDataDistance.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSMPTools.h>

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
  int vtkNotUsed(port), vtkInformation* info)
{
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
  return 1;
}

//------------------------------------------------------------------------------
int vtkCGALPointCloudSurfaceSignedDistance::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* pointCloud = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* surfaceIn  = vtkPolyData::GetData(inputVector[1]);
  vtkPolyData* output     = vtkPolyData::GetData(outputVector);

  if (!pointCloud || !surfaceIn)
  {
    vtkErrorMacro("Point cloud (port 0) and surface (port 1) are required.");
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
