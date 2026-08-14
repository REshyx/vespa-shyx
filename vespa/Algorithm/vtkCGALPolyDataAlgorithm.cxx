#include "vtkCGALPolyDataAlgorithm.h"
#include "vtkVESPAAttributeTransfer.h"

#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

vtkStandardNewMacro(vtkCGALPolyDataAlgorithm);

//------------------------------------------------------------------------------
void vtkCGALPolyDataAlgorithm::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "UpdateAttributes:" << this->UpdateAttributes << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
bool vtkCGALPolyDataAlgorithm::interpolateAttributes(vtkPolyData* input, vtkPolyData* vtkMesh)
{
  if (!this->UpdateAttributes)
  {
    return true;
  }
  return vtkVESPAAttributeTransfer::Interpolate(input, vtkMesh);
}

//------------------------------------------------------------------------------
bool vtkCGALPolyDataAlgorithm::copyAttributes(vtkPolyData* input, vtkPolyData* vtkMesh)
{
  if (!this->UpdateAttributes)
  {
    return true;
  }
  return vtkVESPAAttributeTransfer::Copy(input, vtkMesh);
}
