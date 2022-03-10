/**
 * @class   vtkCGALIsotropicRemesher
 * @brief   remesh using the CGAL isotropic remeshing
 *
 * vtkCGALIsotropicRemesher is a filter allowing to remesh
 * a triangulated polydata using the CGAL isotropic_remesh method.
 * This filter protect feature edges.
 */

#ifndef vtkCGALIsotropicRemesher_h
#define vtkCGALIsotropicRemesher_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALIsotropicRemesher : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALIsotropicRemesher* New();
  vtkTypeMacro(vtkCGALIsotropicRemesher, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Get / Set the edge target length for the result
   * If non is specified by the user, the value will
   * be set as 1% of the boundary box diagonal
   **/
  vtkGetMacro(TargetLength, double);
  vtkSetMacro(TargetLength, double);
  //@}

  //@{
  /**
   * Get / Set the feature edge angle threshold.
   * These edges will be protected during remeshing
   * Default is 45°
   **/
  vtkGetMacro(ProtectAngle, double);
  vtkSetMacro(ProtectAngle, double);
  //@}

  //@{
  /**
   * Get / Set the number of iteration for the
   * CGAL isotropic_remeshing.
   * Default is 1
   **/
  vtkGetMacro(Iterations, int);
  vtkSetMacro(Iterations, int);
  //@}

protected:
  vtkCGALIsotropicRemesher()           = default;
  ~vtkCGALIsotropicRemesher() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  // Fields

  double TargetLength = -1;
  double ProtectAngle = 45;
  int    Iterations   = 1;

private:
  vtkCGALIsotropicRemesher(const vtkCGALIsotropicRemesher&) = delete;
  void operator=(const vtkCGALIsotropicRemesher&) = delete;
};

#endif
