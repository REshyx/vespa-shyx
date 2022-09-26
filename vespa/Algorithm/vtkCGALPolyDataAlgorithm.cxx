#include "vtkCGALPolyDataAlgorithm.h"

// VTK related includes
#include "vtkCellData.h"
#include "vtkCellIterator.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
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
  auto cit = vtk::TakeSmartPointer(vtkMesh->NewCellIterator());
  for (cit->InitTraversal(); !cit->IsDoneWithTraversal(); cit->GoToNextCell())
  {
    // Add the cell
    vtkIdList* ids = cit->GetPointIds();
    vtkIdType nbIds = cit->GetNumberOfPoints();
    std::vector<Graph_Verts> cell(nbIds);
    for (vtkIdType i = 0; i < nbIds; i++)
    {
      cell[i] = surfaceVertices[ids->GetId(i)];
    }
    CGAL::Euler::add_face(cell, cgalMesh->surface);
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
  return true;
}

//------------------------------------------------------------------------------
bool vtkCGALPolyDataAlgorithm::copyAttributes(vtkPolyData* input, vtkPolyData* vtkMesh)
{
  if (this->UpdateAttributes)
  {
    vtkMesh->GetPointData()->ShallowCopy(input->GetPointData());
    vtkMesh->GetCellData()->ShallowCopy(input->GetCellData());
  }

  return true;
}
