/**
 * @class   vtkCGALPointCloudSurfaceSignedDistance
 * @brief   Signed distance from point cloud samples to a reference surface (VTK only)
 *
 * For each point in the first input (point cloud as vtkPolyData), computes the signed
 * distance to the second input using vtkImplicitPolyDataDistance (cell locator + angle-
 * weighted pseudonormals). The surface does not need to be closed. Optional cell normals
 * on the surface are used when present; otherwise VTK computes triangle normals from
 * geometry.
 */

#ifndef vtkCGALPointCloudSurfaceSignedDistance_h
#define vtkCGALPointCloudSurfaceSignedDistance_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkSHYXPointCloudSurfaceSDFModule.h" // For export macro

class VTKSHYXPOINTCLOUDSURFACESDF_EXPORT vtkCGALPointCloudSurfaceSignedDistance : public vtkPolyDataAlgorithm
{
public:
  static vtkCGALPointCloudSurfaceSignedDistance* New();
  vtkTypeMacro(vtkCGALPointCloudSurfaceSignedDistance, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Set the surface mesh connection (input port 1).
   */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

protected:
  vtkCGALPointCloudSurfaceSignedDistance();
  ~vtkCGALPointCloudSurfaceSignedDistance() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

private:
  vtkCGALPointCloudSurfaceSignedDistance(const vtkCGALPointCloudSurfaceSignedDistance&) = delete;
  void operator=(const vtkCGALPointCloudSurfaceSignedDistance&) = delete;
};

#endif
