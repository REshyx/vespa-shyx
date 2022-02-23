#include "vtkCGALBooleanOperation.h"

// VTK related includes
#include "vtkCellIterator.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkProbeFilter.h"

// CGAL related includes
#include <CGAL/Surface_mesh.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Polygon_mesh_processing/corefinement.h>

vtkStandardNewMacro(vtkCGALBooleanOperation);

namespace pmp = CGAL::Polygon_mesh_processing;

// Domain types
using CGAL_Kernel  = CGAL::Simple_cartesian<double>;
using CGAL_Surface = CGAL::Surface_mesh<CGAL_Kernel::Point_3>;

//------------------------------------------------------------------------------
vtkCGALBooleanOperation::vtkCGALBooleanOperation()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkCGALBooleanOperation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "UpdateAttributes: " << (this->UpdateAttributes ? "true" : "false") << std::endl;

  switch (this->OperationType)
  {
    case vtkCGALBooleanOperation::DIFFERENCE:
      os << indent << "OperationType: " << "Difference" << std::endl;
      break;
    case vtkCGALBooleanOperation::INTERSECTION:
      os << indent << "OperationType: " << "Intersection" << std::endl;
      break;
    case vtkCGALBooleanOperation::UNION:
      os << indent << "OperationType: " << "Union" << std::endl;
      break;
    default:
      os << indent << "OperationType: " << "Unknown" << std::endl;
      break;
  }
}

//------------------------------------------------------------------------------
void vtkCGALBooleanOperation::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
int vtkCGALBooleanOperation::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  using Graph_Verts = boost::graph_traits<CGAL_Surface>::vertex_descriptor;
  using Graph_Coord = boost::property_map<CGAL_Surface, CGAL::vertex_point_t>::type;

  // Get the input and source data object
  vtkPolyData* inputData  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* sourceData = vtkPolyData::GetData(inputVector[1]);

  if (!inputData || !sourceData)
  {
    vtkErrorMacro("Missing input or source.");
  }

  // Create the surface meshes for CGAL
  // ----------------------------------

  CGAL_Surface inputSurfaceMesh;
  CGAL_Surface sourceSurfaceMesh;
  Graph_Coord  inputCoordsArr  = get(CGAL::vertex_point, inputSurfaceMesh);
  Graph_Coord  sourceCoordsArr = get(CGAL::vertex_point, sourceSurfaceMesh);

  // Vertices
  const vtkIdType          inNPts     = inputData->GetNumberOfPoints();
  const vtkIdType          sourceNPts = sourceData->GetNumberOfPoints();
  std::vector<Graph_Verts> inputSurfaceVertices(inNPts);
  std::vector<Graph_Verts> sourceSurfaceVertices(sourceNPts);

  for (vtkIdType i = 0; i < inNPts; i++)
  {
    // IDs
    inputSurfaceVertices[i] = add_vertex(inputSurfaceMesh);

    // Coordinates
    double coords[3];
    inputData->GetPoint(i, coords);
    put(inputCoordsArr, inputSurfaceVertices[i],
      CGAL_Kernel::Point_3(coords[0], coords[1], coords[2]));
  }

  for (vtkIdType i = 0; i < sourceNPts; i++)
  {
    // IDs
    sourceSurfaceVertices[i] = add_vertex(sourceSurfaceMesh);

    // Coordinates
    double coords[3];
    sourceData->GetPoint(i, coords);
    put(sourceCoordsArr, sourceSurfaceVertices[i],
      CGAL_Kernel::Point_3(coords[0], coords[1], coords[2]));
  }

  // Cells
  std::array<Graph_Verts, 3> tri;

  auto cit = vtk::TakeSmartPointer(inputData->NewCellIterator());
  for (cit->InitTraversal(); !cit->IsDoneWithTraversal(); cit->GoToNextCell())
  {
    // Sanity check
    if (cit->GetCellType() != VTK_TRIANGLE)
    {
      vtkIdType id = cit->GetCellId();
      vtkErrorMacro("Cell " << id << " in input is not a triangle. Abort.");
      return 0;
    }

    // Add the triangle
    vtkIdList* ids = cit->GetPointIds();
    for (vtkIdType i = 0; i < 3; i++)
    {
      tri[i] = inputSurfaceVertices[ids->GetId(i)];
    }
    CGAL::Euler::add_face(tri, inputSurfaceMesh);
  }

  cit = vtk::TakeSmartPointer(sourceData->NewCellIterator());
  for (cit->InitTraversal(); !cit->IsDoneWithTraversal(); cit->GoToNextCell())
  {
    // Sanity check
    if (cit->GetCellType() != VTK_TRIANGLE)
    {
      vtkIdType id = cit->GetCellId();
      vtkErrorMacro("Cell " << id << " in source is not a triangle. Abort.");
      return 0;
    }

    // Add the triangle
    vtkIdList* ids = cit->GetPointIds();
    for (vtkIdType i = 0; i < 3; i++)
    {
      tri[i] = sourceSurfaceVertices[ids->GetId(i)];
    }
    CGAL::Euler::add_face(tri, sourceSurfaceMesh);
  }

  // CGAL Processing
  // ---------------

  switch (this->OperationType)
  {
    case vtkCGALBooleanOperation::DIFFERENCE:
      pmp::corefine_and_compute_difference(inputSurfaceMesh, sourceSurfaceMesh,
        inputSurfaceMesh, pmp::parameters::all_default(), pmp::parameters::all_default());
      break;
    case vtkCGALBooleanOperation::INTERSECTION:
      pmp::corefine_and_compute_intersection(inputSurfaceMesh, sourceSurfaceMesh,
        inputSurfaceMesh, pmp::parameters::all_default(), pmp::parameters::all_default());
      break;
    case vtkCGALBooleanOperation::UNION:
      pmp::corefine_and_compute_union(inputSurfaceMesh, sourceSurfaceMesh, inputSurfaceMesh,
        pmp::parameters::all_default(), pmp::parameters::all_default());
      break;
    default:
      vtkErrorMacro("Unknown boolean operation!");
      break;
  }

  // VTK Output
  // ----------

  // Points (vertices in inputSurfaceMesh are not contiguous)
  vtkNew<vtkPoints> pts;
  const vtkIdType   outNPts = num_vertices(inputSurfaceMesh);
  pts->Allocate(outNPts);
  std::vector<vtkIdType> vmap(outNPts);

  for (auto vertex : vertices(inputSurfaceMesh))
  {
    const auto& p = get(inputCoordsArr, vertex);
    vtkIdType   id =
      pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    vmap[vertex] = id;
  }
  pts->Squeeze();

  // Cells
  vtkNew<vtkCellArray> cells;
  cells->AllocateEstimate(num_faces(inputSurfaceMesh), 3);

  for (auto face : faces(inputSurfaceMesh))
  {
    vtkNew<vtkIdList> ids;
    for (auto edge : halfedges_around_face(halfedge(face, inputSurfaceMesh), inputSurfaceMesh))
    {
      ids->InsertNextId(vmap[source(edge, inputSurfaceMesh)]);
    }
    cells->InsertNextCell(ids);
  }
  cells->Squeeze();

  // Dataset
  vtkNew<vtkPolyData> outputGeo;
  outputGeo->SetPoints(pts);
  outputGeo->SetPolys(cells);

  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  if (this->UpdateAttributes)
  {
    // Attributes
    vtkNew<vtkProbeFilter> probe;
    probe->SetInputData(outputGeo);
    probe->SetSourceData(inputData);
    probe->SpatialMatchOn();
    probe->Update();

    vtkPolyData* output = vtkPolyData::GetData(outputVector);
    output->ShallowCopy(probe->GetOutput());
  }
  else
  {
    output->ShallowCopy(outputGeo);
  }

  return 1;
}
