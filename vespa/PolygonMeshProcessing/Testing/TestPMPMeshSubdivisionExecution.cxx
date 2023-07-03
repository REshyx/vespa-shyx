#include <iostream>

#include "vtkNew.h"
#include "vtkSphereSource.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALMeshSubdivision.h"

int TestPMPMeshSubdivisionExecution(int, char* argv[])
{
  vtkNew<vtkSphereSource> sphere;
  sphere->SetRadius(2.0);
  sphere->SetThetaResolution(8);
  sphere->SetPhiResolution(8);

  // Create subdivision filter
  vtkNew<vtkCGALMeshSubdivision> subdivider;
  subdivider->SetInputConnection(sphere->GetOutputPort());
  subdivider->SetUpdateAttributes(false);

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(subdivider->GetOutputPort());
  writer->SetFileName("sphere_subdivision_sqrt3.vtp");
  writer->Write();

  // Change to Catmull-Clark method
  subdivider->SetSubdivisionType(vtkCGALMeshSubdivision::CATMULL_CLARK);
  writer->SetFileName("sphere_subdivision_catmull_clark.vtp");
  writer->Write();

  // Change to Loop method
  subdivider->SetSubdivisionType(vtkCGALMeshSubdivision::LOOP);
  writer->SetFileName("sphere_subdivision_loop.vtp");
  writer->Write();

  // Change to Doo-Sabin method
  subdivider->SetSubdivisionType(vtkCGALMeshSubdivision::DOO_SABIN);
  writer->SetFileName("sphere_subdivision_doo_sabin.vtp");
  writer->Write();

  return 0;
}
