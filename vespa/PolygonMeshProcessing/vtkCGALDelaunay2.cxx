#include "vtkCGALDelaunay2.h"

// VTK related includes
#include "vtkDataSet.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"

// CGAL related includes
#include <CGAL/Delaunay_triangulation_2.h>

vtkStandardNewMacro(vtkCGALDelaunay2);

typedef CGAL::Delaunay_triangulation_2<CGAL_Kernel> DT2;

//------------------------------------------------------------------------------
void vtkCGALDelaunay2::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALDelaunay2::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects.
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  // Create the surface mesh for CGAL
  // --------------------------------

  vtkPoints*    vtkPts     = input->GetPoints();
  vtkIdType     nbPts      = input->GetNumberOfPoints();
  vtkDataArray* ptsArr     = vtkPts->GetData();
  const auto    pointRange = vtk::DataArrayTupleRange<3>(ptsArr);

  double rangeVal[3];
  for (int i = 0; i < 3; i++)
  {
    double range[2];
    ptsArr->GetRange(range, i);
    rangeVal[i] = range[1] - range[0];
  }

  if (rangeVal[0] && rangeVal[1] && rangeVal[2])
  {
    vtkErrorMacro("This dataset is 3D");
  }
  int d1 = 0, d2 = 1; // z null
  if (!rangeVal[0])
  {
    d1 = 1;
    d2 = 2;
  }
  if (!rangeVal[1])
  {
    d1 = 0;
    d2 = 2;
  }

  std::vector<CGAL_Kernel::Point_2> pts;
  pts.reserve(nbPts);
  for (const auto pt : pointRange)
  {
    pts.emplace_back(pt[d1], pt[d2]);
  }

  // CGAL Processing
  // ---------------

  DT2 dt2(pts.begin(), pts.end());

  // VTK Output
  // ----------
  vtkNew<vtkPoints> outPts;
  const vtkIdType   outNPts = dt2.number_of_vertices();
  outPts->Allocate(outNPts);
  std::map<CGAL::Point_2<CGAL::Simple_cartesian<double>>, vtkIdType> vmap;

  for (auto vertex : dt2.finite_vertex_handles())
  {
    vtkIdType id =
      outPts->InsertNextPoint(vertex->point()[0], vertex->point()[1], vertex->point()[2]);
    vmap[vertex->point()] = id;
  }
  outPts->Squeeze();

  // cells
  vtkNew<vtkCellArray> cells;
  cells->AllocateEstimate(dt2.number_of_faces(), 3);

  for (auto face : dt2.finite_face_handles())
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
