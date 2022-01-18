/**
 * @class   vtkCGALIsotropicRemesher
 * @brief   remesh using the CGAL isotropic remeshing
 *
 * vtkCGALisotropicRemesh is a dummy filter
 */

#ifndef vtkCGALisotropicRemesh_h
#define vtkCGALisotropicRemesh_h

#include "vtkDataSetAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALIsotropicRemesher : public vtkDataSetAlgorithm
{
public:
  static vtkCGALIsotropicRemesher* New();
  vtkTypeMacro(vtkCGALIsotropicRemesher, vtkDataSetAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  vtkCGALIsotropicRemesher() = default;
  ~vtkCGALIsotropicRemesher() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALIsotropicRemesher(const vtkCGALIsotropicRemesher&) = delete;
  void operator=(const vtkCGALIsotropicRemesher&) = delete;
};

#endif
