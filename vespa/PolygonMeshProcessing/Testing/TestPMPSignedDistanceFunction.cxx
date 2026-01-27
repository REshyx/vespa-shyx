#include "vtkCGALSignedDistanceFunction.h"

#include "vtkFloatArray.h"
#include "vtkImageData.h"
#include "vtkLogger.h"
#include "vtkMathUtilities.h"
#include "vtkNew.h"
#include "vtkPointData.h"
#include "vtkSphereSource.h"
#include "vtkTriangleFilter.h"
#include "vtkXMLImageDataWriter.h"

int TestPMPSignedDistanceFunction(int, char* argv[])
{
  float radius = 1.0f;
  vtkNew<vtkSphereSource> sphere;
  sphere->SetThetaResolution(20);
  sphere->SetPhiResolution(20);
  sphere->SetRadius(radius);
  sphere->Update();

  vtkNew<vtkTriangleFilter> triangulator;
  triangulator->SetInputData(sphere->GetOutput());
  triangulator->Update();

  unsigned int resolution = 21;
  vtkNew<vtkCGALSignedDistanceFunction> sdf;
  sdf->SetInputConnection(triangulator->GetOutputPort());
  sdf->SetBaseResolution(resolution);
  sdf->Update();
  vtkImageData*  output = vtkImageData::SafeDownCast(sdf->GetOutput());
  vtkFloatArray* signedDistanceArray =
    vtkFloatArray::SafeDownCast(output->GetPointData()->GetScalars());

  int dims[3];
  output->GetDimensions(dims);
  if (dims[0] != resolution || dims[1] != resolution || dims[2] != resolution)
  {
    vtkLog(ERROR, "Invalid image dimensions.");
  }
  if (!vtkMathUtilities::FuzzyCompare(signedDistanceArray->GetTuple1(0), sqrt(3) - radius, 0.01))
  {
    vtkLog(ERROR, "Wrong sdf value.");
  }

  // Test padding
  sdf->SetPadding(2);
  sdf->Update();
  output = vtkImageData::SafeDownCast(sdf->GetOutput());
  output->GetDimensions(dims);
  signedDistanceArray = vtkFloatArray::SafeDownCast(output->GetPointData()->GetScalars());
  if (dims[0] != resolution + 2 * sdf->GetPadding() ||
    dims[1] != resolution + 2 * sdf->GetPadding() || dims[2] != resolution + 2 * sdf->GetPadding())
  {
    vtkLog(ERROR, "Invalid image dimensions with padding.");
  }

  return 0;
}
