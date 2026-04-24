/**
 * @class   vtkSHYXBooleanOperationFilter
 * @brief   CGAL PMP boolean (union / intersection / difference) with relaxed input checks.
 *
 * Unlike vtkCGALBooleanOperation, this filter does not require closed meshes and does not enable
 * CGAL's strict self-intersection throw by default. Results may be empty or invalid for inputs
 * that are far from the documented CGAL PMP boolean preconditions; use the options to tighten
 * checks when needed.
 */

#ifndef vtkSHYXBooleanOperationFilter_h
#define vtkSHYXBooleanOperationFilter_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXBooleanOperationFilterModule.h"

class VTKSHYXBOOLEANOPERATIONFILTER_EXPORT vtkSHYXBooleanOperationFilter
  : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXBooleanOperationFilter* New();
  vtkTypeMacro(vtkSHYXBooleanOperationFilter, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum Operation
  {
    DIFFERENCE = 0,
    INTERSECTION,
    UNION
  };

  vtkGetMacro(OperationType, int);
  vtkSetClampMacro(OperationType, int, vtkSHYXBooleanOperationFilter::DIFFERENCE,
    vtkSHYXBooleanOperationFilter::UNION);

  /**
   * When true (default false), CGAL may throw if self-intersections are detected during
   * corefinement, matching stricter vtkCGALBooleanOperation behavior.
   */
  vtkGetMacro(ThrowOnSelfIntersection, bool);
  vtkSetMacro(ThrowOnSelfIntersection, bool);
  vtkBooleanMacro(ThrowOnSelfIntersection, bool);

  /**
   * When true (default), if a mesh does not bound a volume according to CGAL, run
   * orient_to_bound_a_volume() before the boolean (same optional step as VESPA's filter, but
   * without requiring closed meshes first).
   */
  vtkGetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkSetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkBooleanMacro(OrientToBoundVolumeWhenNeeded, bool);

  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

protected:
  vtkSHYXBooleanOperationFilter();
  ~vtkSHYXBooleanOperationFilter() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  int OperationType = vtkSHYXBooleanOperationFilter::DIFFERENCE;
  bool ThrowOnSelfIntersection = false;
  bool OrientToBoundVolumeWhenNeeded = true;

private:
  vtkSHYXBooleanOperationFilter(const vtkSHYXBooleanOperationFilter&) = delete;
  void operator=(const vtkSHYXBooleanOperationFilter&) = delete;
};

#endif
