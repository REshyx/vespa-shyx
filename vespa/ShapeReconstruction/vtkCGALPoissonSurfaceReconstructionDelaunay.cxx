#define CGAL_PMP_USE_CERES_SOLVER

#include "vtkCGALPoissonSurfaceReconstructionDelaunay.h"

// VTK related includes
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkLogger.h"

// CGAL related includes
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/poisson_surface_reconstruction.h>

#include <vector>
#include <fstream>

typedef CGAL_Kernel::FT                                      FT;
typedef CGAL_Kernel::Point_3                                 Point;
typedef CGAL_Kernel::Vector_3                                Vector;
typedef CGAL_Kernel::Sphere_3                                Sphere;
typedef std::pair<Point, Vector>                             Pwn;
typedef CGAL::First_of_pair_property_map<Pwn>                Point_map;
typedef CGAL::Second_of_pair_property_map<Pwn>               Normal_map;
typedef CGAL::Poisson_reconstruction_function<CGAL_Kernel>   Poisson_reconstruction_function;
typedef CGAL::Surface_mesh_default_triangulation_3           STr;
typedef CGAL::Surface_mesh_complex_2_in_triangulation_3<STr> C2t3;
typedef CGAL::Implicit_surface_3<CGAL_Kernel, Poisson_reconstruction_function> Surface_3;

vtkStandardNewMacro(vtkCGALPoissonSurfaceReconstructionDelaunay);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
void vtkCGALPoissonSurfaceReconstructionDelaunay::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALPoissonSurfaceReconstructionDelaunay::RequestData(
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

  std::unique_ptr<Vespa_surface> cgalMesh = std::make_unique<Vespa_surface>();
  this->toCGAL(input, cgalMesh.get());

  // CGAL Processing
  // ---------------

  // Poisson options
  FT sm_angle    = this->MinTriangleAngle; // Min triangle angle in degrees.
  FT sm_radius   = this->MaxTriangleSize;  // Max triangle size w.r.t. point set average spacing.
  FT sm_distance = this->Distance; // Surface Approximation error w.r.t. point set average spacing.

  auto normals = input->GetPointData()->GetArray("Normals");

  if (!normals)
  {
    vtkErrorMacro("Point normals required.");
    return 0;
  }

  try
  {
    std::vector<Pwn> points;

    for (int k = 0; k < input->GetNumberOfPoints(); k++)
    {
      auto pin = input->GetPoint(k);
      auto vin = normals->GetTuple3(k);
      points.push_back(
        std::make_pair(Point(pin[0], pin[1], pin[2]), Vector(vin[0], vin[1], vin[2])));
    }

    // Note: this method requires an iterator over points
    // + property maps to access each point's position and normal.
    Poisson_reconstruction_function function(
      points.begin(), points.end(), Point_map(), Normal_map());

    // Computes the Poisson indicator function f()
    // at each vertex of the triangulation.
    if (!function.compute_implicit_function())
    {
      vtkErrorMacro("Failed to compute the Poisson indicator function.");
      return 0;
    }

    // Computes average spacing
    FT average_spacing = CGAL::compute_average_spacing<CGAL::Sequential_tag>(
      points, 6 /* knn = 1 ring */, CGAL::parameters::point_map(Point_map()));
    // Gets one point inside the implicit surface
    // and computes implicit function bounding sphere radius.
    Point  inner_point = function.get_inner_point();
    Sphere bsphere     = function.bounding_sphere();
    FT     radius      = std::sqrt(bsphere.squared_radius());
    // Defines the implicit surface: requires defining a
    // conservative bounding sphere centered at inner point.
    FT sm_sphere_radius = 5.0 * radius;
    FT sm_dichotomy_error =
      sm_distance * average_spacing / 1000.0; // Dichotomy error must be << sm_distance
    Surface_3 surface(function, Sphere(inner_point, sm_sphere_radius * sm_sphere_radius),
      sm_dichotomy_error / sm_sphere_radius);

    // Defines surface mesh generation criteria
    CGAL::Surface_mesh_default_criteria_3<STr> criteria(sm_angle, // Min triangle angle (degrees)
      sm_radius * average_spacing,                                // Max triangle size
      sm_distance * average_spacing);                             // Approximation error
    // Generates surface mesh with manifold option
    STr  tr;                               // 3D Delaunay triangulation for surface mesh generation
    C2t3 c2t3(tr);                         // 2D complex in 3D Delaunay triangulation
    CGAL::make_surface_mesh(c2t3,          // reconstructed mesh
      surface,                             // implicit surface
      criteria,                            // meshing criteria
      CGAL::Manifold_with_boundary_tag()); // require manifold mesh

    CGAL::facets_in_complex_2_to_triangle_mesh(c2t3, cgalMesh->surface);
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // VTK Output
  // ----------

  this->toVTK(cgalMesh.get(), output);
  this->copyAttributes(input, output);

  return 1;
}
