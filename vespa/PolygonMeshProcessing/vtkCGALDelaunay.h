/**
 * @class   vtkCGALDelaunay
 * @brief   remesh using the CGAL delaunay
 *
 * vtkCGALDelaunay is a filter allowing to remesh
 * ...
 */

#ifndef vtkCGALDelaunay_h
#define vtkCGALDelaunay_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALDelaunay : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALDelaunay* New();
  vtkTypeMacro(vtkCGALDelaunay, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  vtkCGALDelaunay()           = default;
  ~vtkCGALDelaunay() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALDelaunay(const vtkCGALDelaunay&) = delete;
  void operator=(const vtkCGALDelaunay&) = delete;
};

#endif
