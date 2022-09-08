#include <iostream>

#include <vtkNew.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALAlphaWrapping.h"

int TestPMPAlphaWrapping(int, char* argv[])
{
  // Open data

  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  // Remesh

  vtkNew<vtkCGALAlphaWrapping> aw;
  aw->SetInputConnection(reader->GetOutputPort());

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(aw->GetOutputPort());
  writer->SetFileName("alpha_wrapping.vtp");
  writer->Write();

  return 0;
}
