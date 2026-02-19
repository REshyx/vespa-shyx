
#include <vtkConeSource.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkDataSetTriangleFilter.h>
#include <vtkNew.h>
#include <vtkSphereSource.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALBooleanOperation.h"

int TestPMPBooleanExecution(int, char**)
{
  vtkNew<vtkConeSource> cone;
  cone->SetRadius(2.0);

  vtkNew<vtkDataSetTriangleFilter> tri;
  tri->SetInputConnection(cone->GetOutputPort());

  vtkNew<vtkDataSetSurfaceFilter> surf;
  surf->SetInputConnection(tri->GetOutputPort());

  vtkNew<vtkSphereSource> sphere;
  sphere->SetRadius(2.0);
  sphere->SetCenter(1.0, 0., 0.);

  vtkNew<vtkCGALBooleanOperation> boolOp;
  boolOp->SetInputConnection(surf->GetOutputPort());
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
