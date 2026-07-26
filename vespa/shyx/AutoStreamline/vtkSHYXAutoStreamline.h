/**
 * @class   vtkSHYXAutoStreamline
 * @brief   Auto-seed streamlines from vortex cores away from the volume boundary
 *
 * Pure VTK pipeline (no VESPA filter dependency), single volume input:
 *   1. Extract outer surface with vtkDataSetSurfaceFilter
 *   2. vtkVortexCore on the volume vector field
 *   3. Absolute distance of vortex-core points to that surface
 *      (vtkImplicitPolyDataDistance)
 *   4. Cull points with |SDF| < MinSurfaceDistance
 *   5. vtkMaskPoints: MaximumNumberOfPoints + Uniform Spatial (Bounds Based)
 *   6. vtkStreamTracer with the remaining points as seeds
 *
 * Input port 0: volume flow field (vtkDataSet) with a 3-component vector array.
 *
 * Output port 0: streamlines (vtkPolyData).
 * Output port 1: sampled seed points after distance cull + MaskPoints.
 *
 * @sa vtkVortexCore vtkStreamTracer vtkMaskPoints vtkImplicitPolyDataDistance
 *     vtkDataSetSurfaceFilter
 */

#ifndef vtkSHYXAutoStreamline_h
#define vtkSHYXAutoStreamline_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXAutoStreamlineModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXAUTOSTREAMLINE_EXPORT vtkSHYXAutoStreamline : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXAutoStreamline* New();
  vtkTypeMacro(vtkSHYXAutoStreamline, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  ///@{
  /** Forwarded to vtkVortexCore. */
  vtkSetMacro(HigherOrderMethod, vtkTypeBool);
  vtkGetMacro(HigherOrderMethod, vtkTypeBool);
  vtkBooleanMacro(HigherOrderMethod, vtkTypeBool);

  vtkSetMacro(FasterApproximation, bool);
  vtkGetMacro(FasterApproximation, bool);
  vtkBooleanMacro(FasterApproximation, bool);
  ///@}

  ///@{
  /**
   * Keep vortex-core points whose absolute distance to the extracted volume
   * surface is at least this value (removes points too close to the wall).
   * Default 0.
   */
  vtkSetClampMacro(MinSurfaceDistance, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(MinSurfaceDistance, double);
  ///@}

  ///@{
  /** Forwarded to vtkMaskPoints (Uniform Spatial Bounds sampling). */
  vtkSetClampMacro(MaximumNumberOfPoints, vtkIdType, 0, VTK_ID_MAX);
  vtkGetMacro(MaximumNumberOfPoints, vtkIdType);

  vtkSetMacro(RandomSeed, int);
  vtkGetMacro(RandomSeed, int);
  ///@}

  ///@{
  /** Forwarded to vtkStreamTracer. */
  vtkSetClampMacro(IntegrationDirection, int, 0, 2);
  vtkGetMacro(IntegrationDirection, int);

  vtkSetClampMacro(IntegratorType, int, 0, 2);
  vtkGetMacro(IntegratorType, int);

  vtkSetClampMacro(IntegrationStepUnit, int, 1, 2);
  vtkGetMacro(IntegrationStepUnit, int);

  vtkSetClampMacro(InitialIntegrationStep, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(InitialIntegrationStep, double);

  vtkSetClampMacro(MaximumPropagation, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(MaximumPropagation, double);

  vtkSetClampMacro(MaximumNumberOfSteps, vtkIdType, 0, VTK_ID_MAX);
  vtkGetMacro(MaximumNumberOfSteps, vtkIdType);

  vtkSetClampMacro(TerminalSpeed, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(TerminalSpeed, double);

  vtkSetClampMacro(InterpolatorType, int, 0, 1);
  vtkGetMacro(InterpolatorType, int);
  ///@}

protected:
  vtkSHYXAutoStreamline();
  ~vtkSHYXAutoStreamline() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  vtkTypeBool HigherOrderMethod = 0;
  bool FasterApproximation = false;

  double MinSurfaceDistance = 0.0;

  vtkIdType MaximumNumberOfPoints = 1000;
  int RandomSeed = 1;

  int IntegrationDirection = 2; // BOTH
  int IntegratorType = 2;       // RK45
  int IntegrationStepUnit = 2;  // CELL_LENGTH_UNIT
  double InitialIntegrationStep = 0.2;
  double MaximumPropagation = 1.0;
  vtkIdType MaximumNumberOfSteps = 2000;
  double TerminalSpeed = 1.0e-12;
  int InterpolatorType = 0; // point locator

private:
  vtkSHYXAutoStreamline(const vtkSHYXAutoStreamline&) = delete;
  void operator=(const vtkSHYXAutoStreamline&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
