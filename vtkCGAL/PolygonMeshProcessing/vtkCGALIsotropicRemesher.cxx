#include "vtkCGALIsotropicRemesher.h"

// VTK related includes
#include "vtkCellData.h"
#include "vtkDataSet.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"

// CGAL related includes
#include <CGAL/Surface_mesh.h>

vtkStandardNewMacro(vtkCGALIsotropicRemesher);

//------------------------------------------------------------------------------
void vtkCGALIsotropicRemesher::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects.
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0]);
  vtkDataSet* output = vtkDataSet::GetData(outputVector);

  // TODO add CGAL stuff here
  std::cout << "CGAL stuff created" << std::endl;

  // Copy all the input geometry and data to the output.
  output->CopyStructure(input);
  output->GetPointData()->PassData(input->GetPointData());
  output->GetCellData()->PassData(input->GetCellData());

  return 1;
}
