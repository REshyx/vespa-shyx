#include <iostream>

#include <vtkNew.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALDelaunay.h"

int TestDelaunayExecution(int, char* argv[])
{
  // Open data

  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  // Remesh

  vtkNew<vtkCGALDelaunay> rm;
  rm->SetInputConnection(reader->GetOutputPort());

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(rm->GetOutputPort());
  writer->SetFileName("delaunay_remesh.vtp");
  writer->Write();

  return 0;
}
