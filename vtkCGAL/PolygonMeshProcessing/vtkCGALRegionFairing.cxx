#include "vtkCGALRegionFairing.h"

// VTK related includes
#include "vtkCellIterator.h"
#include "vtkExtractSelectedIds.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPointSet.h"
#include "vtkProbeFilter.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"

// CGAL related includes
#include <CGAL/Surface_mesh.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Polygon_mesh_processing/fair.h>

vtkStandardNewMacro(vtkCGALRegionFairing);

namespace pmp = CGAL::Polygon_mesh_processing;

// Domain types
using CGAL_Kernel  = CGAL::Simple_cartesian<double>;
using CGAL_Surface = CGAL::Surface_mesh<CGAL_Kernel::Point_3>;

//------------------------------------------------------------------------------
vtkCGALRegionFairing::vtkCGALRegionFairing()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkCGALRegionFairing::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "UpdateAttributes: " << this->UpdateAttributes << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALRegionFairing::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
  }
  else
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkCGALRegionFairing::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  using Graph_Verts = boost::graph_traits<CGAL_Surface>::vertex_descriptor;
  using Graph_Coord = boost::property_map<CGAL_Surface, CGAL::vertex_point_t>::type;

  // Get the input and output data objects.
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  // Get the selection input
  vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
  if (!selInfo)
  {
    // When not given a selection, nothing to do.
    vtkWarningMacro("No selection made, nothing to do");
    output->ShallowCopy(input);
    return 1;
  }
  vtkSelection* inputSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkNew<vtkExtractSelectedIds> extractSelection;
  extractSelection->SetInputData(0, input);
  extractSelection->SetInputData(1, inputSel);
  extractSelection->Update();
  vtkPointSet* dataSel = vtkPointSet::SafeDownCast(extractSelection->GetOutputDataObject(0));

  // Create the triangle mesh for CGAL
  // --------------------------------

  CGAL_Surface surfaceMesh;
  Graph_Coord  coordsArr = get(CGAL::vertex_point, surfaceMesh);

  // Vertices
  const vtkIdType          inNPts = input->GetNumberOfPoints();
  std::vector<Graph_Verts> surfaceVertices(inNPts);

  for (vtkIdType i = 0; i < inNPts; i++)
  {
    // id
    surfaceVertices[i] = add_vertex(surfaceMesh);

    // coord
    double coords[3];
    input->GetPoint(i, coords);
    put(coordsArr, surfaceVertices[i], CGAL_Kernel::Point_3(coords[0], coords[1], coords[2]));
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
      tri[i] = surfaceVertices[ids->GetId(i)];
    }
    CGAL::Euler::add_face(tri, surfaceMesh);
  }

  // Retrieve the region to fair (ROI)
  // ---------------------------------
  auto gids = vtk::DataArrayValueRange(dataSel->GetPointData()->GetArray("vtkOriginalPointIds"));
  std::vector<Graph_Verts> sel(gids.cbegin(), gids.cend());

  // CGAL Processing
  // ---------------

  // fair selected area
  pmp::fair(surfaceMesh, sel);

  // VTK Output
  // ----------

  // points (vertices in surfaceMesh are not contiguous)
  vtkNew<vtkPoints> pts;
  const vtkIdType   outNPts = num_vertices(surfaceMesh);
  pts->Allocate(outNPts);
  std::vector<vtkIdType> vmap(outNPts);

  for (auto vertex : vertices(surfaceMesh))
  {
    const auto& p = get(coordsArr, vertex);
    vtkIdType   id =
      pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    vmap[vertex] = id;
  }
  pts->Squeeze();

  // cells
  vtkNew<vtkCellArray> cells;
  cells->AllocateEstimate(num_faces(surfaceMesh), 3);

  for (auto face : faces(surfaceMesh))
  {
    vtkNew<vtkIdList> ids;
    for (auto edge : halfedges_around_face(halfedge(face, surfaceMesh), surfaceMesh))
    {
      ids->InsertNextId(vmap[source(edge, surfaceMesh)]);
    }
    cells->InsertNextCell(ids);
  }
  cells->Squeeze();

  // dataset
  vtkNew<vtkPolyData> outputGeo;
  outputGeo->SetPoints(pts);
  outputGeo->SetPolys(cells);

  if (this->UpdateAttributes)
  {
    // attributes
    vtkNew<vtkProbeFilter> probe;
    probe->SetInputData(outputGeo);
    probe->SetSourceData(input);
    probe->SpatialMatchOn();
    probe->Update();

    output->ShallowCopy(probe->GetOutput());
  }
  else
  {
    output->ShallowCopy(outputGeo);
  }
  return 1;
}
