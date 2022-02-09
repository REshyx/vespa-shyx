/**
 * @class   vtkCGALIsotropicRemesher
 * @brief   remesh using the CGAL isotropic remeshing
 *
 * vtkCGALisotropicRemesh is a filter allowing to remesh
 * a triangulated ploydata using the CGAL isotropic_remesh method.
 * This filter protect feature edges.
 */

#ifndef vtkCGALisotropicRemesh_h
#define vtkCGALisotropicRemesh_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALIsotropicRemesher : public vtkPolyDataAlgorithm
{
public:
  static vtkCGALIsotropicRemesher* New();
  vtkTypeMacro(vtkCGALIsotropicRemesher, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Get / Set the edge target length for the result
   * Must be specified by the user
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

  //@{
  /**
   * Choose if the result mesh should have the
   * point / cell data attributes of the input.
   * If so, a vtkProbeFilter is called in order
   * to interpolate values to the new mesh.
   * Default is true
   **/
   vtkGetMacro(UpdateAttributes, bool);
   vtkSetMacro(UpdateAttributes, bool);
   vtkBooleanMacro(UpdateAttributes, bool)
  //@}

protected:
  vtkCGALIsotropicRemesher()           = default;
  ~vtkCGALIsotropicRemesher() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  // Fields

  double TargetLength = 0;
  double ProtectAngle = 45;
  int    Iterations   = 1;
  bool   UpdateAttributes = true;

private:
  vtkCGALIsotropicRemesher(const vtkCGALIsotropicRemesher&) = delete;
  void operator=(const vtkCGALIsotropicRemesher&) = delete;
};

#endif
