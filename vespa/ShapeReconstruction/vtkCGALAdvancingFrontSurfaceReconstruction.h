/**
 * @class   vtkCGALAdvancingFrontSurfaceReconstruction
 * @brief   Smoothes a surface mesh by moving its vertices.
 *
 * vtkCGALAdvancingFrontSurfaceReconstruction is a filter that moves vertices to optimize geometry around each vertex: it can
        try to (1) equalize the angles between incident edges, or (and) move vertices so that areas of
        adjacent triangles tend to equalize (angle and area smoothing), or (2) moves vertices following
        an area-based Laplacian smoothing scheme, performed at each vertex in an estimated tangent plane
        to the surface (tangential relaxation). Border vertices are considered constrained and do not move
        at any step of the procedure. No vertices are inserted at any time.
 */

#ifndef vtkCGALAdvancingFrontSurfaceReconstruction_h
#define vtkCGALAdvancingFrontSurfaceReconstruction_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALSRModule.h" // For export macro

class VTKCGALSR_EXPORT vtkCGALAdvancingFrontSurfaceReconstruction : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALAdvancingFrontSurfaceReconstruction* New();
  vtkTypeMacro(vtkCGALAdvancingFrontSurfaceReconstruction, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

 vtkGetMacro(Per, double);
  vtkSetMacro(Per, double);

   vtkGetMacro(RadiusRatioBound, double);
  vtkSetMacro(RadiusRatioBound, double);

protected:
  vtkCGALAdvancingFrontSurfaceReconstruction()           = default;
  ~vtkCGALAdvancingFrontSurfaceReconstruction() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALAdvancingFrontSurfaceReconstruction(const vtkCGALAdvancingFrontSurfaceReconstruction&) = delete;
  void operator=(const vtkCGALAdvancingFrontSurfaceReconstruction&)       = delete;

  double Per;
  double RadiusRatioBound;
};

#endif
