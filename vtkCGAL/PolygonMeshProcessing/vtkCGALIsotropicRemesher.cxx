#include "vtkCGALIsotropicRemesher.h"

// VTK related includes
#include "vtkCellIterator.h"
#include "vtkDataSet.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"

// CGAL related includes
#include <CGAL/Surface_mesh.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>

vtkStandardNewMacro(vtkCGALIsotropicRemesher);

namespace pmp = CGAL::Polygon_mesh_processing;

// Geomerty types
using CGAL_Kernel  = CGAL::Simple_cartesian<double>;
using CGAL_Surface = CGAL::Surface_mesh<CGAL_Kernel::Point_3>;

//------------------------------------------------------------------------------
void vtkCGALIsotropicRemesher::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects.
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);

  using Graph_Verts = boost::graph_traits<CGAL_Surface>::vertex_descriptor;
  using Graph_Coord = boost::property_map<CGAL_Surface, CGAL::vertex_point_t>::type;

  // Create the surface mesh for CGAL
  // --------------------------------

  CGAL_Surface surface_mesh;
  Graph_Coord  coords_arr = get(CGAL::vertex_point, surface_mesh);

  // Vertices
  const vtkIdType          nbPts = input->GetNumberOfPoints();
  std::vector<Graph_Verts> surface_vertices(nbPts);

  for (vtkIdType i = 0; i < nbPts; i++)
  {
    // id
    surface_vertices[i] = add_vertex(surface_mesh);

    // coord
    double coords[3];
    input->GetPoint(i, coords);
    put(coords_arr, surface_vertices[i], CGAL_Kernel::Point_3(coords[0], coords[1], coords[2]));
  }

  // Cells
  vtkNew<vtkGenericCell>     cell;
  std::array<Graph_Verts, 3> tri;

  auto cit = input->NewCellIterator();
  for (cit->InitTraversal(); cit->IsDoneWithTraversal(); cit->GoToNextCell())
  {
    cit->GetCell(cell);
    if (cell->GetCellType() != VTK_TRIANGLE)
    {
      vtkIdType id = cit->GetCellId();
      vtkErrorMacro("Cell " << id << " is not a triangle. Abort.");
      return 0;
    }

    for (int i = 0; i < 3; i++)
    {
      tri[i] = surface_vertices[cell->GetPointId(i)];
    }
    CGAL::Euler::add_face(tri, surface_mesh);
  }

  // CGAL Processing
  // ---------------
  pmp::isotropic_remeshing(surface_mesh.faces(), 1, surface_mesh);

  // VTK Output
  // ----------

  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  // points
  vtkNew<vtkPoints> pts;
  pts->SetNumberOfPoints(surface_mesh.number_of_vertices());

  vtkIdType id = 0;
  for (auto vertex : vertices(surface_mesh))
  {
    const auto& p = get(coords_arr, vertex);
    pts->SetPoint(id++, CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
  }

  // cells
  vtkNew<vtkCellArray> cells;
  cells->SetNumberOfCells(surface_mesh.number_of_faces());
  for (auto f : faces(surface_mesh))
  {
    vtkNew<vtkIdList> c;
    for (auto h : halfedges_around_face(halfedge(f, surface_mesh), surface_mesh))
    {
      c->InsertNextId(target(h, surface_mesh));
    }
    cells->InsertNextCell(cell);
  }

  // dataset
  output->SetPoints(pts);
  output->SetPolys(cells);

  return 1;
}
