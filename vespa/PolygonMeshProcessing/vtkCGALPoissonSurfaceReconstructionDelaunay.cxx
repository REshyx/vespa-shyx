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
#include <CGAL/IO/read_points.h>
#include <vector>
#include <fstream>

typedef CGAL::Exact_predicates_inexact_constructions_kernel Kernel;
typedef Kernel::Point_3                                     Point;
typedef Kernel::Vector_3                                    Vector;
typedef std::pair<Point, Vector>                            Pwn;
typedef CGAL::Polyhedron_3<Kernel>                          Polyhedron;

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

  try
  {
    std::vector<Pwn> points;

    for (int k = 0; k < input->GetNumberOfPoints(); k++)
    {
      auto pin = input->GetPoint(k);
      auto vin = input->GetPointData()->GetArray("Normals")->GetTuple3(k);
      points.push_back(
        std::make_pair(Point(pin[0], pin[1], pin[2]), Vector(vin[0], vin[1], vin[2])));
    }

    // if (!CGAL::IO::read_points("/Users/theusst/kitten.ply",
    //       std::back_inserter(points),
    //       CGAL::parameters::point_map(CGAL::First_of_pair_property_map<Pwn>())
    //         .normal_map(CGAL::Second_of_pair_property_map<Pwn>())))
    // {
    //   std::cerr << "Error: cannot read input file!" << std::endl;
    //   return EXIT_FAILURE;
    // }

    // Polyhedron output_mesh;
    double average_spacing = CGAL::compute_average_spacing<CGAL::Sequential_tag>(
      points, 6, CGAL::parameters::point_map(CGAL::First_of_pair_property_map<Pwn>()));
    CGAL::poisson_surface_reconstruction_delaunay(points.begin(), points.end(),
      CGAL::First_of_pair_property_map<Pwn>(), CGAL::Second_of_pair_property_map<Pwn>(),
      cgalMesh->surface, average_spacing);
    // std::ofstream out("/Users/theusst/kitten_poisson-20-30-0.375.off");
    // out << cgalMesh->surface;
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

  output->set
  return 1;
}
