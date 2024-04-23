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
  pcaen->SetNeighborhood(1);
  pcaen->SetNumberOfNeighbors(18);
  pcaen->SetRadiusFactor(2.0);
  pcaen->SetOrientNormals(true);
  pcaen->SetDeleteUnoriented(true);
  pcaen->Update();

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(pcaen->GetOutputPort());
  writer->SetFileName("kitten_pca_normals.vtp");
  writer->Write();

  return 0;
}
