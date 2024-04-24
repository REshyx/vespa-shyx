#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALXYZReader.h"
#include "vtkCGALAdvancingFrontSurfaceReconstruction.h"

int TestSRAdvancingFrontSurfaceReconstruction(int, char* argv[])
{
  vtkNew<vtkCGALXYZReader> reader;
  std::string              cfname(argv[1]);
  cfname += "/kitten.xyz";
  reader->SetFileName(cfname.c_str());

  vtkNew<vtkCGALAdvancingFrontSurfaceReconstruction> afsr;
  afsr->SetInputConnection(reader->GetOutputPort());
  afsr->SetPer(0.0);
  afsr->SetRadiusRatioBound(5.0);
  afsr->Update();

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(afsr->GetOutputPort());
  writer->SetFileName("kitten_advancing_front_surface_reconstruction.vtp");
  writer->Write();

  return 0;
}
