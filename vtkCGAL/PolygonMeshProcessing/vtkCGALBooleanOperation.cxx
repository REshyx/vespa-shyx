#include "vtkCGALBooleanOperation.h"

// VTK related includes
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"

// CGAL related includes
#include <CGAL/Polygon_mesh_processing/corefinement.h>

vtkStandardNewMacro(vtkCGALBooleanOperation);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
vtkCGALBooleanOperation::vtkCGALBooleanOperation()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkCGALBooleanOperation::PrintSelf(ostream& os, vtkIndent indent)
{
  switch (this->OperationType)
  {
    case vtkCGALBooleanOperation::DIFFERENCE:
      os << indent << "OperationType: "
         << "Difference" << std::endl;
      break;
    case vtkCGALBooleanOperation::INTERSECTION:
      os << indent << "OperationType: "
         << "Intersection" << std::endl;
      break;
    case vtkCGALBooleanOperation::UNION:
      os << indent << "OperationType: "
         << "Union" << std::endl;
      break;
    default:
      os << indent << "OperationType: "
         << "Unknown" << std::endl;
      break;
  }

  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkCGALBooleanOperation::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
int vtkCGALBooleanOperation::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and source data object
  vtkPolyData* inputData  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* sourceData = vtkPolyData::GetData(inputVector[1]);
  vtkPolyData* output     = vtkPolyData::GetData(outputVector);

  if (!inputData || !sourceData)
  {
    vtkErrorMacro("Missing input or source.");
  }

  // Create the surface meshes for CGAL
  // ----------------------------------

  std::unique_ptr<CGAL_Mesh> cgalInputMesh  = this->toCGAL(inputData);
  std::unique_ptr<CGAL_Mesh> cgalSourceMesh = this->toCGAL(sourceData);

  // CGAL Processing
  // ---------------

  switch (this->OperationType)
  {
    case vtkCGALBooleanOperation::DIFFERENCE:
      pmp::corefine_and_compute_difference(cgalInputMesh->surface, cgalSourceMesh->surface,
        cgalInputMesh->surface, pmp::parameters::all_default(), pmp::parameters::all_default());
      break;
    case vtkCGALBooleanOperation::INTERSECTION:
      pmp::corefine_and_compute_intersection(cgalInputMesh->surface, cgalSourceMesh->surface,
        cgalInputMesh->surface, pmp::parameters::all_default(), pmp::parameters::all_default());
      break;
    case vtkCGALBooleanOperation::UNION:
      pmp::corefine_and_compute_union(cgalInputMesh->surface, cgalSourceMesh->surface,
        cgalInputMesh->surface, pmp::parameters::all_default(), pmp::parameters::all_default());
      break;
    default:
      vtkErrorMacro("Unknown boolean operation!");
      break;
  }

  // VTK Output
  // ----------

  output->ShallowCopy(this->toVTK(cgalInputMesh.get()));

  this->interpolateAttributes(inputData, output);

  return 1;
}
