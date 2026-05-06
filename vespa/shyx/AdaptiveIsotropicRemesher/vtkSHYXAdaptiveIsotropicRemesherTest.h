/**
 * @class vtkSHYXAdaptiveIsotropicRemesherTest
 * @brief Debug filter: evaluates FeatureAwareAdaptiveSizingField maps on the input surface (same
 *        constraint pipeline as vtkSHYXAdaptiveIsotropicRemesher) and writes per-point arrays
 *        VespaAdaptiveSizeGlobal / VespaAdaptiveSizeFeature plus VespaIccVertexNormal (ICC normals after
 *        PrepareIccVertexNormalsForAdaptiveSizing: global `v:vespa_icc_normal` plus dual-region maps when the mask is on;
 *        ICC uses per-face corner normals consistent with each triangle's mask side). Vertex ICC curvature scalars
 *        (VespaIccPrincipalCurvatureMin/Max, VespaIccMeanCurvature, VespaIccGaussianCurvature), and
 *        cell arrays VespaIccTriangleMu0 / Mu1 / Mu2 (per-triangle ICC closed-form measures) without remeshing
 *        or smoothing.
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
