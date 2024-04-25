#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALXYZReader.h"
#include "vtkCGALPCAEstimateNormals.h"

int TestPSPPCAEstimateNormals(int, char* argv[])
{
  vtkNew<vtkCGALXYZReader> reader;
  std::string              cfname(argv[1]);
  cfname += "/kitten.xyz";
  reader->SetFileName(cfname.c_str());

  vtkNew<vtkCGALPCAEstimateNormals> pcaen;
  pcaen->SetInputConnection(reader->GetOutputPort());
  pcaen->Update();

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(pcaen->GetOutputPort());
  writer->SetFileName("kitten_pca_normals.vtp");
  writer->Write();

  return 0;
}
