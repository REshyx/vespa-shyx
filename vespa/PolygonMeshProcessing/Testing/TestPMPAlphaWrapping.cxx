#include <vtkConvertToPointCloud.h>
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

  // Remesh surface
  vtkNew<vtkCGALAlphaWrapping> aw1;
  aw1->SetInputConnection(reader->GetOutputPort());

  // Remesh point cloud
  vtkNew<vtkConvertToPointCloud> toPC;
  toPC->SetInputConnection(aw1->GetOutputPort());
  toPC->SetCellGenerationMode(vtkConvertToPointCloud::VERTEX_CELLS); 

  // Remesh point cloud
  vtkNew<vtkCGALAlphaWrapping> aw2;
  aw2->SetInputConnection(toPC->GetOutputPort());
  aw2->SetAlpha(2);

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(aw2->GetOutputPort());
  writer->SetFileName("alpha_wrapping.vtp");
  writer->Write();

  return 0;
}
