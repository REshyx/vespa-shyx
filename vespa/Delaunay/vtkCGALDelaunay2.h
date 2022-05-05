/**
 * @class   vtkCGALDelaunay2
 * @brief   remesh using the CGAL delaunay
 *
 * vtkCGALDelaunay2 allows to create plannar delaunay meshes
 * from a set of planar points, edges and polygons.
 * From now on, the input mesh needs to be planar along x, y or z.
 * Constraints should not overlap each others.
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
