/**
 * @class   vtkCGALisotropicRemesh
 * @brief   remesh using the CGAL isotropic remeshing
 *
 * vtkCGALisotropicRemesh is a filter ...
 */

#ifndef vtkCGALisotropicRemesh_h
#define vtkCGALisotropicRemesh_h

#include "vtkDataSetAlgorithm.h"

#include "VTKCGALModule.h" // For export macro

class VTKCGAL_EXPORT vtkCGALisotropicRemesh : public vtkDataSetAlgorithm
{
public:
  static vtkCGALisotropicRemesh* New();
  vtkTypeMacro(vtkCGALisotropicRemesh, vtkDataSetAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  vtkCGALisotropicRemesh();
  ~vtkCGALisotropicRemesh() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALisotropicRemesh(const vtkCGALisotropicRemesh&) = delete;
  void operator=(const vtkCGALisotropicRemesh&) = delete;
};

#endif
