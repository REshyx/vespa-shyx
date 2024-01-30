/**
 * @class   vtkCGALPoissonSurfaceReconstructionDelaunay
 * @brief   Smoothes a surface mesh by moving its vertices.
 *
 * vtkCGALPoissonSurfaceReconstructionDelaunay is a filter that moves vertices to optimize geometry around each vertex: it can
        try to (1) equalize the angles between incident edges, or (and) move vertices so that areas of
        adjacent triangles tend to equalize (angle and area smoothing), or (2) moves vertices following
        an area-based Laplacian smoothing scheme, performed at each vertex in an estimated tangent plane
        to the surface (tangential relaxation). Border vertices are considered constrained and do not move
        at any step of the procedure. No vertices are inserted at any time.
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
