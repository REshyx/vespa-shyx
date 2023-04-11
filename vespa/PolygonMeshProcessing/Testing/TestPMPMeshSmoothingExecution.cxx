#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALMeshSmoothing.h"

int TestPMPMeshSmoothingExecution(int, char* argv[])
{
  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/hand.vtp";
  reader->SetFileName(cfname.c_str());

  vtkNew<vtkCGALMeshSmoothing> smoother;
  smoother->SetInputConnection(reader->GetOutputPort());
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
