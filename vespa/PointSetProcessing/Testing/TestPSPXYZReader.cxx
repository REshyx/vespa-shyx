#include <iostream>

#include "vtkNew.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALXYZReader.h"

int TestPSPXYZReader(int, char* argv[])
{
  vtkNew<vtkCGALXYZReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/kitten.xyz";
  reader->SetFileName(cfname.c_str());

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(reader->GetOutputPort());
  writer->SetFileName("kitten_xyz_reader.vtp");
  writer->Write();

  return 0;
}
