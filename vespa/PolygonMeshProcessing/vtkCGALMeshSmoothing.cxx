#include "vtkCGALMeshSmoothing.h"

// VTK related includes
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkLogger.h"

// CGAL related includes
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/angle_and_area_smoothing.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/IO/polygon_mesh_io.h>

vtkStandardNewMacro(vtkCGALMeshSmoothing);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
void vtkCGALMeshSmoothing::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "Number of Iterations :" << this->NumberOfIterations << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALMeshSmoothing::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output.");
  }

  // Create the surface mesh for CGAL
  // ----------------------------------

  std::unique_ptr<CGAL_Mesh> cgalMesh = this->toCGAL(input);

  // CGAL Processing
  // ---------------

  try
  {
    auto featureEdges = get(CGAL::edge_is_feature, cgalMesh->surface);
    pmp::detect_sharp_edges(cgalMesh->surface, 60, featureEdges);

    vtkLog(INFO, "Smoothing mesh... (" << this->NumberOfIterations << " iterations)");

    // Smooth with both angle and area criteria + Delaunay flips
    pmp::angle_and_area_smoothing(cgalMesh->surface,
      CGAL::parameters::number_of_iterations(this->NumberOfIterations)
        .use_safety_constraints(this->UseSafetyConstraints) // authorize all moves
        .edge_is_constrained_map(featureEdges));
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // VTK Output
  // ----------

  output->ShallowCopy(this->toVTK(cgalMesh.get()));

  this->copyAttributes(input, output);

  return 1;
}
