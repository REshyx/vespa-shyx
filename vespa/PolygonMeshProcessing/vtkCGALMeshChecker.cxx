#include "vtkCGALMeshChecker.h"

// VTK related includes
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"

// CGAL related includes
#include <CGAL/Polygon_mesh_processing/corefinement.h>

vtkStandardNewMacro(vtkCGALMeshChecker);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
void vtkCGALMeshChecker::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALMeshChecker::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and source data object
  vtkPolyData* inputData = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output    = vtkPolyData::GetData(outputVector);

  if (!inputData)
  {
    vtkErrorMacro("Missing input mesh.");
  }

  // Create the surface meshes for CGAL
  // ----------------------------------

  std::unique_ptr<CGAL_Mesh> cgalInputMesh = this->toCGAL(inputData);
  std::unique_ptr<CGAL_Mesh> cgalOutMesh   = this->toCGAL(output);

  // CGAL Processing
  // ---------------

  try
  {
    if (this->CheckWatertight)
    {
      bool closed = CGAL::is_closed(cgalInputMesh->surface);
      if (!closed)
      {
        vtkWarningMacro("Not closed, you may try to execute the vtkCGALPatchFilling filters is "
                        "holes are trivials.");
      }
      else
      {
        bool volume = CGAL::Polygon_mesh_processing::does_bound_a_volume(cgalInputMesh->surface);
        if (!volume)
        {
          vtkWarningMacro("Not watertight try repairing");
        }
        if (!volume && this->RepairWatertight)
        {
          pmp::orient_to_bound_a_volume(cgalInputMesh->surface);
          volume = CGAL::Polygon_mesh_processing::does_bound_a_volume(cgalInputMesh->surface);
          vtkWarningMacro("Repair " << (volume ? "successful." : "failed."));
        }
      }
      // TODO: add close and volume as field data ?
    }

    if (this->CheckIntersect)
    {
      bool intersect = CGAL::Polygon_mesh_processing::does_self_intersect(cgalInputMesh->surface);
      if (intersect)
      {
        vtkWarningMacro("Self intersection detected");
      }
      // TODO add intersect as field data
      // TODO as a cell data ?
    }
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // VTK Output
  // ----------

  output->ShallowCopy(this->toVTK(cgalOutMesh.get()));
  this->interpolateAttributes(inputData, output);

  return 1;
}
