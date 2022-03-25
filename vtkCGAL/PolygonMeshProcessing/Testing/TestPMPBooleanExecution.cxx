#include <iostream>

#include "vtkNew.h"
#include "vtkSphereSource.h"
#include "vtkTestUtilities.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkXMLPolyDataWriter.h"

#include "vtkCGALBooleanOperation.h"

int TestPMPBooleanExecution(int, char* argv[])
{
  vtkNew<vtkXMLPolyDataReader> reader;
  std::string cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  vtkNew<vtkSphereSource> sphere;
  sphere->SetRadius(2.0);

  vtkNew<vtkCGALBooleanOperation> boolOp;
  boolOp->SetInputConnection(reader->GetOutputPort());
  boolOp->SetSourceConnection(sphere->GetOutputPort());

  // Compute difference
  boolOp->SetOperationType(vtkCGALBooleanOperation::DIFFERENCE);

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(boolOp->GetOutputPort());
  writer->SetFileName("boolean_operation_difference.vtp");
  writer->Write();

  // Compute intersection
  boolOp->SetOperationType(vtkCGALBooleanOperation::INTERSECTION);
  writer->SetFileName("boolean_operation_intersection.vtp");
  writer->Write();

  // Compute union
  boolOp->SetOperationType(vtkCGALBooleanOperation::UNION);
  writer->SetFileName("boolean_operation_union.vtp");
  writer->Write();

  return 0;
}
