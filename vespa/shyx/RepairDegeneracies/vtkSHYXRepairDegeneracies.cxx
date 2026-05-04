#include "vtkSHYXRepairDegeneracies.h"

#include "vtkCGALHelper.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkTriangle.h>

#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/Polygon_mesh_processing/shape_predicates.h>
#include <CGAL/boost/graph/helpers.h>

#include <boost/graph/graph_traits.hpp>

#include <exception>
#include <iterator>
#include <vector>

vtkStandardNewMacro(vtkSHYXRepairDegeneracies);

namespace pmp = CGAL::Polygon_mesh_processing;

namespace
{
using SMHalfedge = boost::graph_traits<CGAL_Surface>::halfedge_descriptor;

void append_face_triangle(const CGAL_Surface& mesh, const Graph_Coord& coords, Graph_Faces f,
  vtkPoints* pts, vtkCellArray* polys, vtkIntArray* region, int region_value)
{
  SMHalfedge h = mesh.halfedge(f);
  vtkNew<vtkTriangle> tri;
  int k = 0;
  SMHalfedge cur = h;
  int guard = 0;
  do
  {
    if (k >= 3 || guard++ > 16)
    {
      return;
    }
    const auto& p = get(coords, mesh.target(cur));
    const vtkIdType pid =
      pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    tri->GetPointIds()->SetId(k++, pid);
    cur = mesh.next(cur);
  } while (cur != h);
  if (k != 3)
  {
    return;
  }
  polys->InsertNextCell(tri);
  region->InsertNextTuple1(region_value);
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXRepairDegeneracies::vtkSHYXRepairDegeneracies()
{
  this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
int vtkSHYXRepairDegeneracies::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXRepairDegeneracies::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "RemoveAlmostDegenerate: " << (this->RemoveAlmostDegenerate ? "on\n" : "off\n");
  os << indent << "CapThreshold: " << this->CapThreshold << "\n";
  os << indent << "NeedleThreshold: " << this->NeedleThreshold << "\n";
  os << indent << "CollapseLengthThreshold: " << this->CollapseLengthThreshold << "\n";
  os << indent << "FlipTriangleHeightThreshold: " << this->FlipTriangleHeightThreshold << "\n";
  os << indent << "PreserveGenus: " << (this->PreserveGenus ? "on\n" : "off\n");
}

//------------------------------------------------------------------------------
int vtkSHYXRepairDegeneracies::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outRegion = vtkPolyData::GetData(outputVector, 1);
  if (!input || !output || !outRegion)
  {
    vtkErrorMacro(<< "Null input or output (expect two vtkPolyData output ports).");
    return 0;
  }

  vtkNew<vtkPoints> diagPts;
  vtkNew<vtkCellArray> diagPolys;
  vtkNew<vtkIntArray> region;
  region->SetName("SHYX_RepairRegion");
  region->SetNumberOfComponents(1);

  if (input->GetNumberOfCells() == 0)
  {
    output->Initialize();
    outRegion->Initialize();
    outRegion->SetPoints(diagPts);
    outRegion->SetPolys(diagPolys);
    outRegion->GetCellData()->AddArray(region);
    return 1;
  }

  vtkCGALHelper::Vespa_surface surf;
  if (!vtkCGALHelper::toCGAL(input, &surf))
  {
    vtkErrorMacro(<< "vtkCGALHelper::toCGAL failed.");
    return 0;
  }

  if (!CGAL::is_triangle_mesh(surf.surface))
  {
    vtkErrorMacro(<< "Input must be a pure triangle mesh (CGAL::is_triangle_mesh). "
                      "Use vtkTriangleFilter upstream if needed.");
    return 0;
  }

  try
  {
    // Port 1: exact degenerate triangles on the input (before remove_degenerate_faces).
    {
      std::vector<Graph_Faces> degen;
      pmp::degenerate_faces(surf.surface, std::back_inserter(degen));
      for (Graph_Faces f : degen)
      {
        append_face_triangle(surf.surface, surf.coords, f, diagPts.Get(), diagPolys.Get(), region.Get(), 1);
      }
    }

    const bool degenerate_ok = pmp::remove_degenerate_faces(
      surf.surface, pmp::parameters::preserve_genus(this->PreserveGenus));
    if (!degenerate_ok)
    {
      vtkWarningMacro(<< "CGAL remove_degenerate_faces returned false (some degeneracies may remain).");
    }

    if (this->RemoveAlmostDegenerate)
    {
      const SMHalfedge null_h = boost::graph_traits<CGAL_Surface>::null_halfedge();
      for (Graph_Faces f : faces(surf.surface))
      {
        if (pmp::is_degenerate_triangle_face(f, surf.surface))
        {
          continue;
        }
        const SMHalfedge needle_h =
          pmp::is_needle_triangle_face(f, surf.surface, this->NeedleThreshold);
        const SMHalfedge cap_h = pmp::is_cap_triangle_face(f, surf.surface, this->CapThreshold);
        const bool needle = (needle_h != null_h);
        const bool cap = (cap_h != null_h);
        int code = 0;
        if (needle && cap)
        {
          code = 4;
        }
        else if (needle)
        {
          code = 2;
        }
        else if (cap)
        {
          code = 3;
        }
        if (code != 0)
        {
          append_face_triangle(surf.surface, surf.coords, f, diagPts.Get(), diagPolys.Get(), region.Get(), code);
        }
      }

      const bool almost_ok = pmp::remove_almost_degenerate_faces(surf.surface,
        pmp::parameters::cap_threshold(this->CapThreshold)
          .needle_threshold(this->NeedleThreshold)
          .collapse_length_threshold(this->CollapseLengthThreshold)
          .flip_triangle_height_threshold(this->FlipTriangleHeightThreshold));
      if (!almost_ok)
      {
        vtkWarningMacro(<< "CGAL remove_almost_degenerate_faces returned false (some caps/needles may remain).");
      }
    }

    if (!vtkCGALHelper::toVTK(&surf, output))
    {
      vtkErrorMacro(<< "vtkCGALHelper::toVTK failed.");
      return 0;
    }
    this->interpolateAttributes(input, output);

    outRegion->Initialize();
    outRegion->SetPoints(diagPts);
    outRegion->SetPolys(diagPolys);
    outRegion->GetCellData()->AddArray(region);
    outRegion->GetCellData()->SetActiveScalars(region->GetName());
  }
  catch (const std::exception& e)
  {
    vtkErrorMacro(<< "CGAL exception: " << e.what());
    return 0;
  }

  return 1;
}
