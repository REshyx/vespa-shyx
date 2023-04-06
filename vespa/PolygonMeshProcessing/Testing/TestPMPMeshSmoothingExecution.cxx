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
  smoother->SetNumberOfIterations(5);

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(smoother->GetOutputPort());
  writer->SetFileName("smooth_hand.vtp");
  writer->Write();

  return 0;
}
