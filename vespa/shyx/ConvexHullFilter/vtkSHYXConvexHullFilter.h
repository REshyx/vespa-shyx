/**
 * @class   vtkSHYXConvexHullFilter
 * @brief   Closed triangle mesh of the 3D convex hull of input point positions (vtkPolyData).
 *
 * VTK does not ship a class named vtkConvexHullFilter. This filter implements the usual VTK
 * convex-hull pipeline: optional vtkCleanPolyData, vtkDelaunay3D with Alpha = 0 (full tetrahedral
 * fill of the hull), vtkDataSetSurfaceFilter to extract the boundary, then vtkTriangleFilter.
 *
 * @sa vtkDelaunay3D vtkDataSetSurfaceFilter vtkCleanPolyData
 */

#ifndef vtkSHYXConvexHullFilter_h
#define vtkSHYXConvexHullFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXConvexHullFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXCONVEXHULLFILTER_EXPORT vtkSHYXConvexHullFilter : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXConvexHullFilter* New();
  vtkTypeMacro(vtkSHYXConvexHullFilter, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** When on (default), merge coincident points with vtkCleanPolyData before Delaunay. */
  vtkSetMacro(CleanInputPoints, int);
  vtkGetMacro(CleanInputPoints, int);
  vtkBooleanMacro(CleanInputPoints, int);

  /** Fraction of the point bounding-box diagonal used as merge tolerance in vtkCleanPolyData
   *  when CleanInputToleranceIsAbsolute is off (default). */
  vtkSetClampMacro(CleanInputTolerance, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(CleanInputTolerance, double);

  vtkSetMacro(CleanInputToleranceIsAbsolute, int);
  vtkGetMacro(CleanInputToleranceIsAbsolute, int);
  vtkBooleanMacro(CleanInputToleranceIsAbsolute, int);

  /** Forwarded to vtkDelaunay3D: discard points closer than this fraction of the diagonal (0..1). */
  vtkSetClampMacro(Tolerance, double, 0.0, 1.0);
  vtkGetMacro(Tolerance, double);

  /** Forwarded to vtkDelaunay3D: scales the initial bounding triangulation (default 2.5). */
  vtkSetClampMacro(Offset, double, 2.5, VTK_DOUBLE_MAX);
  vtkGetMacro(Offset, double);

  /** Forwarded to vtkDelaunay3D: include debugging bounding triangulation in the mesh (default off). */
  vtkSetMacro(BoundingTriangulation, int);
  vtkGetMacro(BoundingTriangulation, int);
  vtkBooleanMacro(BoundingTriangulation, int);

protected:
  vtkSHYXConvexHullFilter();
  ~vtkSHYXConvexHullFilter() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int CleanInputPoints = 1;
  double CleanInputTolerance = 0.0;
  int CleanInputToleranceIsAbsolute = 0;
  double Tolerance = 0.001;
  double Offset = 2.5;
  int BoundingTriangulation = 0;

private:
  vtkSHYXConvexHullFilter(const vtkSHYXConvexHullFilter&) = delete;
  void operator=(const vtkSHYXConvexHullFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
