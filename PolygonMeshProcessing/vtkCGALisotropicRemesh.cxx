#include "vtkCGALisotropicRemesh.h"

#include "vtkCellData.h"
#include "vtkDataSet.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"

vtkStandardNewMacro(vtkCGALisotropicRemesh);

//------------------------------------------------------------------------------
// Begin the class proper
vtkCGALisotropicRemesh::vtkCGALisotropicRemesh() = default;

//------------------------------------------------------------------------------
vtkCGALisotropicRemesh::~vtkCGALisotropicRemesh() = default;

//------------------------------------------------------------------------------
void vtkCGALisotropicRemesh::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALisotropicRemesh::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects.
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0]);
  vtkDataSet* output = vtkDataSet::GetData(outputVector);

  // TODO add CGAL stuff here

  // Copy all the input geometry and data to the output.
  output->CopyStructure(input);
  output->GetPointData()->PassData(input->GetPointData());
  output->GetCellData()->PassData(input->GetCellData());

  return 1;
}
