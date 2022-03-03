#include "vtkCGALPolyDataAlgorithm.h"

// VTK related includes
#include "vtkCellIterator.h"
#include "vtkObjectFactory.h"
#include "vtkProbeFilter.h"

vtkStandardNewMacro(vtkCGALPolyDataAlgorithm);

//------------------------------------------------------------------------------
void vtkCGALPolyDataAlgorithm::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "UpdateAttributes:" << this->UpdateAttributes << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
std::unique_ptr<CGAL_Mesh> vtkCGALPolyDataAlgorithm::toCGAL(vtkPolyData* vtkMesh)
{
  std::unique_ptr<CGAL_Mesh> cgalMesh(new CGAL_Mesh());

  // Vertices
  const vtkIdType          inNPts = vtkMesh->GetNumberOfPoints();
  std::vector<Graph_Verts> surfaceVertices(inNPts);

  for (vtkIdType i = 0; i < inNPts; i++)
  {
    // id
    surfaceVertices[i] = add_vertex(cgalMesh->surface);

    // coord
    double coords[3];
    vtkMesh->GetPoint(i, coords);
    put(
      cgalMesh->coords, surfaceVertices[i], CGAL_Kernel::Point_3(coords[0], coords[1], coords[2]));
  }

  // Cells
  std::array<Graph_Verts, 3> tri;

  auto cit = vtk::TakeSmartPointer(vtkMesh->NewCellIterator());
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
      tri[i] = surfaceVertices[ids->GetId(i)];
    }
    CGAL::Euler::add_face(tri, cgalMesh->surface);
  }

  return cgalMesh;
}

//------------------------------------------------------------------------------

vtkSmartPointer<vtkPolyData> vtkCGALPolyDataAlgorithm::toVTK(CGAL_Mesh* cgalMesh)
{
  // points (vertices in surfaceMesh are not contiguous)
  vtkNew<vtkPoints> pts;
  const vtkIdType   outNPts = num_vertices(cgalMesh->surface);
  pts->Allocate(outNPts);
  std::vector<vtkIdType> vmap(outNPts);

  for (auto vertex : vertices(cgalMesh->surface))
  {
    const auto& p = get(cgalMesh->coords, vertex);
    vtkIdType   id =
      pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    vmap[vertex] = id;
  }
  pts->Squeeze();

  // cells
  vtkNew<vtkCellArray> cells;
  cells->AllocateEstimate(num_faces(cgalMesh->surface), 3);

  for (auto face : faces(cgalMesh->surface))
  {
    vtkNew<vtkIdList> ids;
    for (auto edge : halfedges_around_face(halfedge(face, cgalMesh->surface), cgalMesh->surface))
    {
      ids->InsertNextId(vmap[source(edge, cgalMesh->surface)]);
    }
    cells->InsertNextCell(ids);
  }
  cells->Squeeze();

  // VTK dataset
  auto vtkMesh = vtkSmartPointer<vtkPolyData>::New();
  vtkMesh->SetPoints(pts);
  vtkMesh->SetPolys(cells);
  return vtkMesh;
}

//------------------------------------------------------------------------------
bool vtkCGALPolyDataAlgorithm::interpolateAttributes(vtkPolyData* input, vtkPolyData* vtkMesh)
{
  if (this->UpdateAttributes)
  {
    vtkNew<vtkProbeFilter> probe;
    probe->SetInputData(vtkMesh);
    probe->SetSourceData(input);
    probe->SpatialMatchOn();
    probe->Update();

    vtkMesh->ShallowCopy(probe->GetOutput());
  }
  return 1;
}
