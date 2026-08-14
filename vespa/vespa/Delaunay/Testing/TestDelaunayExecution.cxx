#include <iostream>

#include <vtkNew.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALDelaunay2.h"

int TestDelaunayExecution(int, char* argv[])
{
  // Open data

  vtkNew<vtkXMLPolyDataReader> reader2;
  std::string                  cfname2(argv[1]);
  cfname2 += "shrink_plane.vtp";
  reader2->SetFileName(cfname2.c_str());

  // Remesh

  vtkNew<vtkCGALDelaunay2> rm2;
  rm2->SetInputConnection(reader2->GetOutputPort());

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;

  writer->SetInputConnection(rm2->GetOutputPort());
  writer->SetFileName("delaunay2_remesh.vtp");
  writer->Write();

  return 0;
}
