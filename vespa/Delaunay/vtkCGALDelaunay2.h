/**
 * @class   vtkCGALDelaunay2
 * @brief   remesh using CGAL's 2D Delaunay triangulation.
 *
 * vtkCGALDelaunay2 allows to create planar Delaunay triangulations
 * from a set of planar points, edges, and polygons.
 * From now on, the input data needs to be planar along x, y, or z.
 * Constraints should not overlap each other.
 */

#ifndef vtkCGALDelaunay2_h
#define vtkCGALDelaunay2_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkCGALDelaunayModule.h" // For export macro

class VTKCGALDELAUNAY_EXPORT vtkCGALDelaunay2 : public vtkPolyDataAlgorithm
{
public:
  static vtkCGALDelaunay2* New();
  vtkTypeMacro(vtkCGALDelaunay2, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  vtkCGALDelaunay2()           = default;
  ~vtkCGALDelaunay2() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALDelaunay2(const vtkCGALDelaunay2&) = delete;
  void operator=(const vtkCGALDelaunay2&) = delete;
};

#endif
