#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALIsotropicRemesher.h"

int TestPMPExecution(int, char* argv[])
{
  vtkNew<vtkXMLPolyDataReader> reader;
  std::string cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  vtkNew<vtkCGALIsotropicRemesher> rm;
  rm->SetInputConnection(reader->GetOutputPort());
  rm->SetIterations(3);

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(rm->GetOutputPort());
  writer->SetFileName("isotropic_remesh.vtp");
  writer->Update();
  writer->Write();

  return 0;
}
