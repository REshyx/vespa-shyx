#include "vtkSHYXAutorefineSelfIntersectionFilter.h"

#include "vtkCGALHelper.h"

#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

#include <CGAL/version.h>
#include <CGAL/Polygon_mesh_processing/intersection.h>
#if CGAL_VERSION_NR >= 1060000000
#  include <CGAL/Polygon_mesh_processing/autorefinement.h>
#endif
#include <CGAL/boost/graph/helpers.h>

#include <exception>

vtkStandardNewMacro(vtkSHYXAutorefineSelfIntersectionFilter);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
void vtkSHYXAutorefineSelfIntersectionFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "OnlyIfSelfIntersecting: " << (this->OnlyIfSelfIntersecting ? "on\n" : "off\n");
  os << indent << "PreserveGenus: " << (this->PreserveGenus ? "on\n" : "off\n");
}

//------------------------------------------------------------------------------
int vtkSHYXAutorefineSelfIntersectionFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Null input or output.");
    return 0;
  }

  if (input->GetNumberOfCells() == 0)
  {
    output->Initialize();
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
    const bool has_ix = pmp::does_self_intersect(surf.surface);
    if (!this->OnlyIfSelfIntersecting || has_ix)
    {
#if CGAL_VERSION_NR >= 1060000000
      (void)this->PreserveGenus;
      pmp::autorefine(surf.surface);
#else
      pmp::experimental::autorefine_and_remove_self_intersections(
        surf.surface, pmp::parameters::preserve_genus(this->PreserveGenus));
#endif
    }

    if (!vtkCGALHelper::toVTK(&surf, output))
    {
      vtkErrorMacro(<< "vtkCGALHelper::toVTK failed.");
      return 0;
    }
    this->interpolateAttributes(input, output);
  }
  catch (const std::exception& e)
  {
    vtkErrorMacro(<< "CGAL exception: " << e.what());
    return 0;
  }

  return 1;
}
