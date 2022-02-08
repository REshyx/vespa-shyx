#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALIsotropicRemesher.h"

int TestPMPExecution(int argc, char* argv[])
{
  vtkNew<vtkXMLPolyDataReader> reader;
  char* cfname = vtkTestUtilities::ExpandDataFileName(argc, argv, "dragon.vtp");
  reader->SetFileName(cfname);

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
