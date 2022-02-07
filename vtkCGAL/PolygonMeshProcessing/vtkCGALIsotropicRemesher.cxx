#include "vtkCGALIsotropicRemesher.h"

// VTK related includes
#include "vtkCellIterator.h"
#include "vtkDataSet.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"

// CGAL related includes
#include <CGAL/Surface_mesh.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>

vtkStandardNewMacro(vtkCGALIsotropicRemesher);

namespace pmp = CGAL::Polygon_mesh_processing;

// Domain types
using CGAL_Kernel  = CGAL::Simple_cartesian<double>;
using CGAL_Surface = CGAL::Surface_mesh<CGAL_Kernel::Point_3>;

//------------------------------------------------------------------------------
void vtkCGALIsotropicRemesher::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "TargetLength :" << this->TargetLength << std::endl;
  os << indent << "Iterations :" << this->Iterations << std::endl;
}

//------------------------------------------------------------------------------
int vtkCGALIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  using Graph_Verts = boost::graph_traits<CGAL_Surface>::vertex_descriptor;
  using Graph_Coord = boost::property_map<CGAL_Surface, CGAL::vertex_point_t>::type;

  // sanity check
  if (this->TargetLength <= 0)
  {
    vtkErrorMacro(
      "Please, specify a valid TargetLength for edges, current is: " << this->TargetLength);
    return 0;
  }

  // Get the input and output data objects.
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);

  // Create the surface mesh for CGAL
  // --------------------------------

  CGAL_Surface surface_mesh;
  Graph_Coord  coords_arr = get(CGAL::vertex_point, surface_mesh);

  // Vertices
  const vtkIdType          inNPts = input->GetNumberOfPoints();
  std::vector<Graph_Verts> surface_vertices(inNPts);

  for (vtkIdType i = 0; i < inNPts; i++)
  {
    // id
    surface_vertices[i] = add_vertex(surface_mesh);

    // coord
    double coords[3];
    input->GetPoint(i, coords);
    put(coords_arr, surface_vertices[i], CGAL_Kernel::Point_3(coords[0], coords[1], coords[2]));
  }

  // Cells
  std::array<Graph_Verts, 3> tri;

  auto cit = vtk::TakeSmartPointer(input->NewCellIterator());
  for (cit->InitTraversal(); !cit->IsDoneWithTraversal(); cit->GoToNextCell())
  {
    // Sanity check
    if (cit->GetCellType() != VTK_TRIANGLE)
    {
      vtkIdType id = cit->GetCellId();
      vtkErrorMacro("Cell " << id << " is not a triangle. Abort.");
      return 0;
    }

    // Add the triangle
    vtkIdList* ids = cit->GetPointIds();
    for (vtkIdType i = 0; i < 3; i++)
    {
      tri[i] = surface_vertices[ids->GetId(i)];
    }
    CGAL::Euler::add_face(tri, surface_mesh);
  }

  // CGAL Processing
  // ---------------

  // protect feature edges:
  // https://doc.cgal.org/latest/Polygon_mesh_processing/Polygon_mesh_processing_2mesh_smoothing_example_8cpp-example.html#a3
  auto featureEdges = get(CGAL::edge_is_feature, surface_mesh);
  pmp::detect_sharp_edges(surface_mesh, this->ProtectAngle, featureEdges);

  // remesh
  pmp::isotropic_remeshing(surface_mesh.faces(), this->TargetLength, surface_mesh,
    pmp::parameters::number_of_iterations(this->Iterations)
      .protect_constraints(true)
      .edge_is_constrained_map(featureEdges));

  // VTK Output
  // ----------

  // points (vertices in surface_mesh are not contiguous)
  vtkNew<vtkPoints> pts;
  const vtkIdType   outNPts = num_vertices(surface_mesh);
  pts->Allocate(outNPts);
  std::vector<vtkIdType> vmap(outNPts);

  for (auto vertex : vertices(surface_mesh))
  {
    const auto& p = get(coords_arr, vertex);
    vtkIdType   id =
      pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    vmap[vertex] = id;
  }

  // cells
  vtkNew<vtkCellArray> cells;
  // cells->AllocateEstimate(num_faces(surface_mesh), 3);
  cells->Allocate(num_faces(surface_mesh));

  for (auto face : faces(surface_mesh))
  {
    vtkNew<vtkIdList> ids;
    for (auto edge : halfedges_around_face(halfedge(face, surface_mesh), surface_mesh))
    {
      ids->InsertNextId(vmap[source(edge, surface_mesh)]);
    }
    cells->InsertNextCell(ids);
  }

  // dataset
  vtkPolyData* output = vtkPolyData::GetData(outputVector);
  output->SetPoints(pts);
  output->SetPolys(cells);

  return 1;
}
