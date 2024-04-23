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

#include "vtkCGALSRModule.h" // For export macro

class VTKCGALSR_EXPORT vtkCGALPoissonSurfaceReconstructionDelaunay : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALPoissonSurfaceReconstructionDelaunay* New();
  vtkTypeMacro(vtkCGALPoissonSurfaceReconstructionDelaunay, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(MinTriangleAngle, double);
  vtkSetMacro(MinTriangleAngle, double);

  /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(MaxTriangleSize, double);
  vtkSetMacro(MaxTriangleSize, double);

  /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(Distance, double);
  vtkSetMacro(Distance, double);

protected:
  vtkCGALPoissonSurfaceReconstructionDelaunay()           = default;
  ~vtkCGALPoissonSurfaceReconstructionDelaunay() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALPoissonSurfaceReconstructionDelaunay(const vtkCGALPoissonSurfaceReconstructionDelaunay&) = delete;
  void operator=(const vtkCGALPoissonSurfaceReconstructionDelaunay&)       = delete;

  double MinTriangleAngle;
  double MaxTriangleSize;
  double Distance;
};

#endif
