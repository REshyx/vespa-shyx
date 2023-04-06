#include "vtkCGALMeshSmoothing.h"

// VTK related includes
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"

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

  std::unique_ptr<CGAL_Mesh> cgalInputMesh = this->toCGAL(input);

  // CGAL Processing
  // ---------------

  try
  {
    typedef boost::property_map<CGAL_Surface, CGAL::edge_is_feature_t>::type EIFMap;
    typedef boost::graph_traits<CGAL_Surface>::edge_descriptor               edge_descriptor;

    EIFMap eif = get(CGAL::edge_is_feature, cgalInputMesh->surface);

    pmp::detect_sharp_edges(cgalInputMesh->surface, 60, eif);

    int sharp_counter = 0;
    for (edge_descriptor e : edges(cgalInputMesh->surface))
      if (get(eif, e))
        ++sharp_counter;

    std::cout << sharp_counter << " sharp edges" << std::endl;
    std::cout << "Smoothing mesh... (" << this->NumberOfIterations << " iterations)" << std::endl;

    // Smooth with both angle and area criteria + Delaunay flips
    pmp::angle_and_area_smoothing(cgalInputMesh->surface,
      CGAL::parameters::number_of_iterations(this->NumberOfIterations)
        .use_safety_constraints(this->UseSafetyConstraints) // authorize all moves
        .edge_is_constrained_map(eif));
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // VTK Output
  // ----------

  output->ShallowCopy(this->toVTK(cgalInputMesh.get()));

  this->copyAttributes(input, output);

  return 1;
}
