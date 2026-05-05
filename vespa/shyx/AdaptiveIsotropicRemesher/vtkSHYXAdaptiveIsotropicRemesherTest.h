/**
 * @class vtkSHYXAdaptiveIsotropicRemesherTest
 * @brief Debug filter: evaluates FeatureAwareAdaptiveSizingField maps on the input surface (same
 *        constraint pipeline as vtkSHYXAdaptiveIsotropicRemesher) and writes per-point arrays
 *        VespaAdaptiveSizeGlobal / VespaAdaptiveSizeFeature without remeshing or smoothing.
 */

#ifndef vtkSHYXAdaptiveIsotropicRemesherTest_h
#define vtkSHYXAdaptiveIsotropicRemesherTest_h

#include "vtkSHYXAdaptiveIsotropicRemesher.h"
#include "vtkSHYXAdaptiveIsotropicRemesherModule.h"

class VTKSHYXADAPTIVEISOTROPICREMESHER_EXPORT vtkSHYXAdaptiveIsotropicRemesherTest
  : public vtkSHYXAdaptiveIsotropicRemesher
{
public:
  static vtkSHYXAdaptiveIsotropicRemesherTest* New();
  vtkTypeMacro(vtkSHYXAdaptiveIsotropicRemesherTest, vtkSHYXAdaptiveIsotropicRemesher);

protected:
  vtkSHYXAdaptiveIsotropicRemesherTest() = default;
  ~vtkSHYXAdaptiveIsotropicRemesherTest() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkSHYXAdaptiveIsotropicRemesherTest(const vtkSHYXAdaptiveIsotropicRemesherTest&) = delete;
  void operator=(const vtkSHYXAdaptiveIsotropicRemesherTest&) = delete;
};

#endif
