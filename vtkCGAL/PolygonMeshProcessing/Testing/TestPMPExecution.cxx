#include <iostream>

#include "vtkNew.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALIsotropicRemesher.h"

int TestPMPExecution(int, char* argv[])
{
  vtkNew<vtkXMLPolyDataReader> reader;
  reader->SetFileName(argv[1]);

  vtkNew<vtkCGALIsotropicRemesher> rm;
  rm->SetInputConnection(reader->GetOutputPort());
  rm->SetIterations(3);
  rm->SetTargetLength(10.0);

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(rm->GetOutputPort());
  writer->SetFileName("isotropic_remesh.vtp");
  writer->Update();
  writer->Write();

  return 0;
}
