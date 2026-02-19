#include <vtkNew.h>
#include <vtkSphereSource.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALMeshSmoothing.h"
int TestPMPMeshSmoothingExecution(int, char**)
{
  vtkNew<vtkSphereSource> sphere;

  vtkNew<vtkCGALMeshSmoothing> smoother;
  smoother->SetInputConnection(sphere->GetOutputPort());
  smoother->SetNumberOfIterations(10);

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(smoother->GetOutputPort());

  // change to tangential relaxation
  smoother->SetSmoothingMethod(1);
  writer->SetFileName("smooth_hand_tangential_relaxation.vtp");
  writer->Write();

  // change to angle and area smoothing
  smoother->SetSmoothingMethod(2);
  writer->SetFileName("smooth_hand_angle_and_area_smoothing.vtp");
  writer->Write();

  return 0;
}
