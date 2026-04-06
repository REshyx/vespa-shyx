#include "vtkCGALPointCloudSurfaceSignedDistance.h"

#include <vtkFloatArray.h>
#include <vtkLogger.h>
#include <vtkMathUtilities.h>
#include <vtkNew.h>
#include <vtkPlaneSource.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkTriangleFilter.h>

int TestPMPPointCloudSurfaceSignedDistance(int, char*[])
{
  vtkNew<vtkPlaneSource> plane;
  plane->SetOrigin(-1.0, -1.0, 0.0);
  plane->SetPoint1(1.0, -1.0, 0.0);
  plane->SetPoint2(-1.0, 1.0, 0.0);
  plane->SetXResolution(4);
  plane->SetYResolution(4);
  plane->Update();

  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputConnection(plane->GetOutputPort());
  tri->Update();

  vtkNew<vtkPoints> pts;
  pts->InsertNextPoint(0.0, 0.0, 0.5);
  pts->InsertNextPoint(0.0, 0.0, -0.5);
  vtkNew<vtkPolyData> cloud;
  cloud->SetPoints(pts);

  vtkNew<vtkCGALPointCloudSurfaceSignedDistance> sdf;
  sdf->SetInputData(0, cloud);
  sdf->SetInputData(1, tri->GetOutput());
  sdf->Update();

  vtkPolyData* out = sdf->GetOutput();
  auto* arr = vtkFloatArray::SafeDownCast(out->GetPointData()->GetArray("SDF"));
  if (!arr || arr->GetNumberOfTuples() != 2)
  {
    vtkLog(ERROR, "Missing or wrong SDF array.");
    return 1;
  }

  const double a0 = arr->GetTuple1(0);
  const double a1 = arr->GetTuple1(1);
  if (!vtkMathUtilities::FuzzyCompare(std::fabs(a0), 0.5, 0.05) ||
    !vtkMathUtilities::FuzzyCompare(std::fabs(a1), 0.5, 0.05))
  {
    vtkLog(ERROR, "Unexpected distance magnitude.");
    return 1;
  }
  if ((a0 > 0.0 && a1 > 0.0) || (a0 < 0.0 && a1 < 0.0))
  {
    vtkLog(ERROR, "Expected opposite signs on opposite sides of the surface.");
    return 1;
  }

  return 0;
}
