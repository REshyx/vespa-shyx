/**
 * @class   vtkCGALDelaunay2
 * @brief   remesh using the CGAL delaunay
 *
 * vtkCGALDelaunay2 is a filter allowing to remesh
 * ...
 */

#ifndef vtkCGALDelaunay2_h
#define vtkCGALDelaunay2_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALDelaunay2 : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALDelaunay2* New();
  vtkTypeMacro(vtkCGALDelaunay2, vtkCGALPolyDataAlgorithm);
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
