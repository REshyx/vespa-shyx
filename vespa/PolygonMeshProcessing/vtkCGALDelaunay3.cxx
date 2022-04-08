#include "vtkCGALDelaunay3.h"

// VTK related includes
#include "vtkDataSet.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"

// CGAL related includes
#include <CGAL/Delaunay_triangulation_3.h>

vtkStandardNewMacro(vtkCGALDelaunay3);

typedef CGAL::Delaunay_triangulation_3<CGAL_Kernel> DT3;

//------------------------------------------------------------------------------
void vtkCGALDelaunay3::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALDelaunay3::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects.
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  // Create the surface mesh for CGAL
  // --------------------------------

  vtkPoints*                        vtkPts     = input->GetPoints();
  vtkIdType                         nbPts      = input->GetNumberOfPoints();
  const auto                        pointRange = vtk::DataArrayTupleRange<3>(vtkPts->GetData());
  std::vector<CGAL_Kernel::Point_3> pts;
  pts.reserve(nbPts);
  for (const auto pt : pointRange)
  {
    pts.emplace_back(pt[0], pt[1], pt[2]);
  }

  // CGAL Processing
  // ---------------

  DT3 dt3(pts.begin(), pts.end());

  // VTK Output
  // ----------
  vtkNew<vtkPoints> outPts;
  const vtkIdType   outNPts = dt3.number_of_vertices();
  outPts->Allocate(outNPts);
  std::map<CGAL::Point_3<CGAL::Simple_cartesian<double>>, vtkIdType> vmap;

  for (auto vertex : dt3.finite_vertex_handles())
  {
    vtkIdType id =
      outPts->InsertNextPoint(vertex->point()[0], vertex->point()[1], vertex->point()[2]);
    vmap[vertex->point()] = id;
  }
  outPts->Squeeze();

  // cells
  vtkNew<vtkCellArray> cells;
  cells->AllocateEstimate(dt3.number_of_cells(), 3);

  for (auto face : dt3.finite_cell_handles())
  {
    vtkNew<vtkIdList> ids;
    ids->InsertNextId(vmap[face->vertex(0)->point()]);
    ids->InsertNextId(vmap[face->vertex(1)->point()]);
    ids->InsertNextId(vmap[face->vertex(2)->point()]);

    cells->InsertNextCell(ids);
  }
  cells->Squeeze();

  // VTK dataset
  output->SetPoints(outPts);
  output->SetPolys(cells);

  return 1;
}
