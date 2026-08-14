#include <iostream>

#include <vtkNew.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALIsotropicRemesher.h"

int TestPMPIsotropicExecution(int, char* argv[])
{
  // Open data

  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  // Remesh

  vtkNew<vtkCGALIsotropicRemesher> rm;
  rm->SetInputConnection(reader->GetOutputPort());
  rm->SetNumberOfIterations(3);

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(rm->GetOutputPort());
  writer->SetFileName("isotropic_remesh.vtp");
  writer->Write();

  return 0;
}
