#include "vtkCGALMeshChecker.h"

// VTK related includes
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"

// VESPA related includes
#include "vtkCGALPatchFilling.h"

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

  if (this->AttemptRepair)
  {
    output->DeepCopy(inputData);
  }
  else
  {
    // mesh won't be modified
    output->ShallowCopy(inputData);
  }

  // Create the surface meshes for CGAL
  // ----------------------------------

  std::unique_ptr<Vespa_surface> cgalMesh = std::make_unique<Vespa_surface>();
  this->toCGAL(output, cgalMesh.get());

  // CGAL Processing
  // ---------------

  // TODO: add fields data
  // TODO avoid VTK copy if still invalid ?

  try
  {
    if (this->CheckWatertight)
    {
      bool closed = CGAL::is_closed(cgalMesh->surface);
      if (!closed)
      {
        vtkWarningMacro("Not closed.");
        if (this->AttemptRepair)
        {
          vtkWarningMacro("Attempt reparation on open mesh: shrink holes.");

          vtkNew<vtkCGALPatchFilling> patchFilling;
          patchFilling->SetInputData(output);
          patchFilling->Update();
          output->DeepCopy(patchFilling->GetOutputDataObject(0));
          this->interpolateAttributes(inputData, output);
          this->toCGAL(output, cgalMesh.get());

          // check reparation
          closed = CGAL::is_closed(cgalMesh->surface);
          vtkWarningMacro("Closing " << (closed ? "successful." : "failed."));
        }
      }

      if (closed) // may have been repaired
      {
        bool volume = CGAL::Polygon_mesh_processing::does_bound_a_volume(cgalMesh->surface);
        if (!volume)
        {
          vtkWarningMacro("Not watertight.");
          if (this->AttemptRepair)
          {
            pmp::orient_to_bound_a_volume(cgalMesh->surface);
            this->toVTK(cgalMesh.get(), output);
            this->interpolateAttributes(inputData, output);

            // check reparation
            volume = CGAL::Polygon_mesh_processing::does_bound_a_volume(cgalMesh->surface);
            vtkWarningMacro("Closing " << (volume ? "successful." : "failed."));
          }
        }
      }
    }

    if (this->CheckIntersect)
    {
      bool intersect = CGAL::Polygon_mesh_processing::does_self_intersect(cgalMesh->surface);
      if (intersect)
      {
        vtkWarningMacro("Self intersection detected");
        if (this->AttemptRepair)
        {
          pmp::experimental::autorefine_and_remove_self_intersections(
            cgalMesh->surface, pmp::parameters::preserve_genus(false));
          this->toVTK(cgalMesh.get(), output);
          this->interpolateAttributes(inputData, output);

          // check reparation
          intersect = CGAL::Polygon_mesh_processing::does_self_intersect(cgalMesh->surface);
          vtkWarningMacro("Remove intersection " << (intersect ? "successful." : "failed."));
        }
      }
    }
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  return 1;
}
