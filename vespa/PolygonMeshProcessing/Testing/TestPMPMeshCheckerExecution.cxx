#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALMeshChecker.h"

int TestPMPMeshCheckerExecution(int, char* argv[])
{
  // Open data
  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/pegasus.vtp";
  reader->SetFileName(cfname.c_str());

  // Create checker filter
  vtkNew<vtkCGALMeshChecker> checker;
  checker->SetInputConnection(reader->GetOutputPort());
  checker->AttemptRepairOn();

  std::cout << "EXPECTED WARNING|: Not closed, Attempt reparation, Closing successful." << std::endl;

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(checker->GetOutputPort());
  writer->SetFileName("checker.vtp");
  writer->Write();

  return 0;
}
