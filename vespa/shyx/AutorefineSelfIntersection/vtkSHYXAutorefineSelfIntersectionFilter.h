/**
 * @class   vtkSHYXAutorefineSelfIntersectionFilter
 * @brief   CGAL PMP autorefine to remove self-intersections on triangle surface meshes.
 *
 * CGAL 6.0 and later: CGAL::Polygon_mesh_processing::autorefine(). Earlier CGAL: the former
 * experimental::autorefine_and_remove_self_intersections entry point. Operates on a CGAL::Surface_mesh
 * built from vtkPolyData (via vtkCGALHelper). Input must be a pure triangle mesh (CGAL::is_triangle_mesh).
 *
 * @sa vtkSHYXMeshChecker vtkCGALMeshChecker
 */

#ifndef vtkSHYXAutorefineSelfIntersectionFilter_h
#define vtkSHYXAutorefineSelfIntersectionFilter_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXAutorefineSelfIntersectionFilterModule.h"

class VTKSHYXAUTOREFINESELFINTERSECTIONFILTER_EXPORT vtkSHYXAutorefineSelfIntersectionFilter
  : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXAutorefineSelfIntersectionFilter* New();
  vtkTypeMacro(vtkSHYXAutorefineSelfIntersectionFilter, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * When on (default), skip CGAL autorefine if does_self_intersect is false (output is still the
   * CGAL-imported mesh, with attributes interpolated when UpdateAttributes is on).
   */
  vtkGetMacro(OnlyIfSelfIntersecting, bool);
  vtkSetMacro(OnlyIfSelfIntersecting, bool);
  vtkBooleanMacro(OnlyIfSelfIntersecting, bool);
  //@}

  //@{
  /**
   * Before CGAL 6: forwarded as preserve_genus to experimental autorefine. Ignored for CGAL 6 and
   * later (PMP::autorefine).
   */
  vtkGetMacro(PreserveGenus, bool);
  vtkSetMacro(PreserveGenus, bool);
  vtkBooleanMacro(PreserveGenus, bool);
  //@}

protected:
  vtkSHYXAutorefineSelfIntersectionFilter() = default;
  ~vtkSHYXAutorefineSelfIntersectionFilter() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  bool OnlyIfSelfIntersecting = true;
  bool PreserveGenus        = false;

private:
  vtkSHYXAutorefineSelfIntersectionFilter(const vtkSHYXAutorefineSelfIntersectionFilter&) = delete;
  void operator=(const vtkSHYXAutorefineSelfIntersectionFilter&) = delete;
};

#endif
