#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALXYZReader.h"
#include "vtkCGALPoissonSurfaceReconstructionDelaunay.h"

int TestSRPoissonSurfaceReconstructionDelaunay(int, char* argv[])
{
  vtkNew<vtkCGALXYZReader> reader;
  std::string              cfname(argv[1]);
  cfname += "/kitten.xyz";
  reader->SetFileName(cfname.c_str());

  vtkNew<vtkCGALPoissonSurfaceReconstructionDelaunay> psrd;
  psrd->SetInputConnection(reader->GetOutputPort());
  psrd->SetMinTriangleAngle(20.0);
  psrd->SetMaxTriangleSize(2.0);
  psrd->SetDistance(0.375);
  psrd->Update();

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(psrd->GetOutputPort());
  writer->SetFileName("kitten_poisson_surface_reconstruction_delaunay.vtp");
  writer->Write();

  return 0;
}
