#include <iostream>

#include <vtkNew.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALDelaunay2.h"
#include "vtkCGALDelaunay3.h"

int TestDelaunayExecution(int, char* argv[])
{
  // Open data

  // vtkNew<vtkXMLPolyDataReader> reader3;
  // std::string                  cfname3(argv[1]);
  // cfname3 += "/dragon.vtp";
  // reader3->SetFileName(cfname3.c_str());

  vtkNew<vtkXMLPolyDataReader> reader2;
  std::string                  cfname2(argv[1]);
  cfname2 += "/pts.vtp";
  reader2->SetFileName(cfname2.c_str());

  // Remesh

  vtkNew<vtkCGALDelaunay2> rm2;
  rm2->SetInputConnection(reader2->GetOutputPort());

  // vtkNew<vtkCGALDelaunay3> rm3;
  // rm3->SetInputConnection(reader3->GetOutputPort());

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;

  writer->SetInputConnection(rm2->GetOutputPort());
  writer->SetFileName("delaunay2_remesh.vtp");
  writer->Write();

  // writer->SetInputConnection(rm3->GetOutputPort());
  // writer->SetFileName("delaunay3_remesh.vtp");
  // writer->Write();

  return 0;
}
