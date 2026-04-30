#include "vtkSHYXEdgeCollapse.h"

#include "vtkCGALHelper.h"

#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

#include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_count_ratio_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_length_cost.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_plane_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/LindstromTurk.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Midpoint_placement.h>
#include <CGAL/boost/graph/helpers.h>

#include <exception>
#include <memory>

vtkStandardNewMacro(vtkSHYXEdgeCollapse);

namespace SMS = CGAL::Surface_mesh_simplification;

//------------------------------------------------------------------------------
void vtkSHYXEdgeCollapse::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "EdgeCountRatio: " << this->EdgeCountRatio << std::endl;
  os << indent << "CostStrategy: " << this->CostStrategy << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXEdgeCollapse::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);
  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }

  if (input->GetNumberOfCells() == 0)
  {
    output->Initialize();
    return 1;
  }

  if (!(this->EdgeCountRatio > 0.0 && this->EdgeCountRatio <= 1.0))
  {
    vtkErrorMacro("EdgeCountRatio must be in (0, 1], got " << this->EdgeCountRatio);
    return 0;
  }

  auto cgalMesh = std::make_unique<vtkCGALHelper::Vespa_surface>();
  if (!vtkCGALHelper::toCGAL(input, cgalMesh.get()))
  {
    vtkErrorMacro("vtkCGALHelper::toCGAL failed.");
    return 0;
  }

  if (!CGAL::is_triangle_mesh(cgalMesh->surface))
  {
    vtkErrorMacro(
      "Input must be a pure triangle mesh (CGAL::is_triangle_mesh). "
      "Use vtkTriangleFilter upstream.");
    return 0;
  }

  try
  {
    SMS::Edge_count_ratio_stop_predicate<CGAL_Surface> stop(this->EdgeCountRatio);
    switch (this->CostStrategy)
    {
      case 0:
        (void)SMS::edge_collapse(cgalMesh->surface, stop,
          CGAL::parameters::get_cost(SMS::LindstromTurk_cost<CGAL_Surface>())
            .get_placement(SMS::LindstromTurk_placement<CGAL_Surface>()));
        break;
      case 1:
      {
        SMS::GarlandHeckbert_plane_policies<CGAL_Surface, CGAL_Kernel> gh(cgalMesh->surface);
        (void)SMS::edge_collapse(cgalMesh->surface, stop,
          CGAL::parameters::get_cost(gh.get_cost()).get_placement(gh.get_placement()));
        break;
      }
      case 2:
        (void)SMS::edge_collapse(cgalMesh->surface, stop,
          CGAL::parameters::get_cost(SMS::Edge_length_cost<CGAL_Surface>())
            .get_placement(SMS::Midpoint_placement<CGAL_Surface>()));
        break;
      default:
        vtkErrorMacro("Invalid CostStrategy " << this->CostStrategy);
        return 0;
    }
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Surface_mesh_simplification::edge_collapse: " << e.what());
    return 0;
  }

  if (!vtkCGALHelper::toVTK(cgalMesh.get(), output))
  {
    vtkErrorMacro("vtkCGALHelper::toVTK failed.");
    return 0;
  }

  this->interpolateAttributes(input, output);
  return 1;
}
