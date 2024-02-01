/**
 * @class   vtkCGALPoissonSurfaceReconstructionDelaunay
 * @brief   Poisson surface reconstruction.
 *
 * vtkCGALPoissonSurfaceReconstructionDelaunay 
 * adapted from
 * https://doc.cgal.org/latest/Poisson_surface_reconstruction_3/Poisson_surface_reconstruction_3_2poisson_reconstruction_function_8cpp-example.html
 */

#ifndef vtkCGALPoissonSurfaceReconstructionDelaunay_h
#define vtkCGALPoissonSurfaceReconstructionDelaunay_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALPoissonSurfaceReconstructionDelaunay : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALPoissonSurfaceReconstructionDelaunay* New();
  vtkTypeMacro(vtkCGALPoissonSurfaceReconstructionDelaunay, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  vtkCGALPoissonSurfaceReconstructionDelaunay()           = default;
  ~vtkCGALPoissonSurfaceReconstructionDelaunay() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALPoissonSurfaceReconstructionDelaunay(const vtkCGALPoissonSurfaceReconstructionDelaunay&) = delete;
  void operator=(const vtkCGALPoissonSurfaceReconstructionDelaunay&)       = delete;
};

#endif
