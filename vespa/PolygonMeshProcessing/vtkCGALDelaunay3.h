/**
 * @class   vtkCGALDelaunay3
 * @brief   remesh using the CGAL delaunay
 *
 * vtkCGALDelaunay3 is a filter allowing to remesh
 * ...
 */

#ifndef vtkCGALDelaunay3_h
#define vtkCGALDelaunay3_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALDelaunay3 : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALDelaunay3* New();
  vtkTypeMacro(vtkCGALDelaunay3, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  vtkCGALDelaunay3()           = default;
  ~vtkCGALDelaunay3() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALDelaunay3(const vtkCGALDelaunay3&) = delete;
  void operator=(const vtkCGALDelaunay3&) = delete;
};

#endif
