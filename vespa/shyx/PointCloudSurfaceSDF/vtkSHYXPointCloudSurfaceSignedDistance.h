/**
 * @class   vtkSHYXPointCloudSurfaceSignedDistance
 * @brief   Signed distance from point cloud samples to a reference surface (VTK only)
 *
 * For each point in the first input (point cloud; vtkPolyData or any vtkDataSet whose
 * points are sampled), computes the signed distance to the second input using
 * vtkImplicitPolyDataDistance (cell locator + angle-weighted pseudonormals). The reference
 * surface accepts vtkPolyData or vtkDataSet; non-polydata inputs are converted with
 * vtkDataSetSurfaceFilter. The surface does not need to be closed. Optional cell normals
 * on the surface are used when present; otherwise VTK computes triangle normals from
 * geometry.
 */

#ifndef vtkSHYXPointCloudSurfaceSignedDistance_h
#define vtkSHYXPointCloudSurfaceSignedDistance_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkSHYXPointCloudSurfaceSDFModule.h" // For export macro

class VTKSHYXPOINTCLOUDSURFACESDF_EXPORT vtkSHYXPointCloudSurfaceSignedDistance : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXPointCloudSurfaceSignedDistance* New();
  vtkTypeMacro(vtkSHYXPointCloudSurfaceSignedDistance, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Set the surface mesh connection (input port 1).
   */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * If ON (default), store |signed distance| in the SDF array instead of the signed value.
   */
  vtkGetMacro(TakeAbsoluteValue, vtkTypeBool);
  vtkSetMacro(TakeAbsoluteValue, vtkTypeBool);
  vtkBooleanMacro(TakeAbsoluteValue, vtkTypeBool);

protected:
  vtkSHYXPointCloudSurfaceSignedDistance();
  ~vtkSHYXPointCloudSurfaceSignedDistance() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  vtkTypeBool TakeAbsoluteValue = 1;

private:
  vtkSHYXPointCloudSurfaceSignedDistance(const vtkSHYXPointCloudSurfaceSignedDistance&) = delete;
  void operator=(const vtkSHYXPointCloudSurfaceSignedDistance&) = delete;
};

#endif
