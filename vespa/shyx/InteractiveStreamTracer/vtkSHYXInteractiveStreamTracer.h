/**
 * @class   vtkSHYXInteractiveStreamTracer
 * @brief   StreamTracer with interactive line / sphere / rectangle (plane) seed shapes.
 *
 * One input port: the flow field (vtkDataSet) with a 3-component vector array. The seed
 * geometry is built from properties instead of a second pipeline input, so a 3D widget can
 * drive it directly:
 *
 *  - SeedShape 0 Line:     LinePoint1 / LinePoint2, LineResolution samples.
 *  - SeedShape 1 Sphere:   SphereCenter / SphereRadius surface samples.
 *  - SeedShape 2 Rectangle: RectangleOrigin / RectanglePoint1 / RectanglePoint2 plane samples.
 *
 * Snap flags (LineSnapPoint1, LineSnapPoint2, SphereSnapCenter, RectangleSnapOrigin) move the
 * corresponding point to the nearest vertex of the input surface before tracing. The input may
 * be a volume or a surface; for volumes the outer surface is extracted with
 * vtkDataSetSurfaceFilter. A single input is used deliberately: vessel wall and stent surface
 * vertices are both valid snap targets when they live in the same dataset.
 *
 * StreamTracer parameters are forwarded verbatim (see SHYXAutoStreamline for the same naming).
 *
 * Output port 0: streamlines (vtkPolyData).
 * Output port 1: seed geometry (vtkPolyData; points / line / sphere / plane).
 *
 * @sa vtkStreamTracer vtkLineSource vtkSphereSource vtkPlaneSource vtkDataSetSurfaceFilter
 */

#ifndef vtkSHYXInteractiveStreamTracer_h
#define vtkSHYXInteractiveStreamTracer_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXInteractiveStreamTracerModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXINTERACTIVESTREAMTRACER_EXPORT vtkSHYXInteractiveStreamTracer
  : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXInteractiveStreamTracer* New();
  vtkTypeMacro(vtkSHYXInteractiveStreamTracer, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum SeedShapeType
  {
    Line = 0,
    Sphere = 1,
    Rectangle = 2
  };

  ///@{
  /** Active seed shape. */
  vtkSetClampMacro(SeedShape, int, Line, Rectangle);
  vtkGetMacro(SeedShape, int);
  ///@}

  ///@{
  /** Line seed: endpoints and number of subdivisions (LineResolution + 1 seed points). */
  vtkSetVector3Macro(LinePoint1, double);
  vtkGetVector3Macro(LinePoint1, double);
  vtkSetVector3Macro(LinePoint2, double);
  vtkGetVector3Macro(LinePoint2, double);
  vtkSetClampMacro(LineResolution, int, 1, 100000);
  vtkGetMacro(LineResolution, int);
  ///@}

  ///@{
  /** Per-endpoint "snap to nearest input-surface vertex before tracing". */
  vtkSetMacro(LineSnapPoint1, vtkTypeBool);
  vtkGetMacro(LineSnapPoint1, vtkTypeBool);
  vtkBooleanMacro(LineSnapPoint1, vtkTypeBool);
  vtkSetMacro(LineSnapPoint2, vtkTypeBool);
  vtkGetMacro(LineSnapPoint2, vtkTypeBool);
  vtkBooleanMacro(LineSnapPoint2, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Sphere seed distribution.
   *  0 Surface: vertices of a vtkSphereSource tessellation. The seed count is set by
   *    SphereThetaResolution x SpherePhiResolution, not by a point count.
   *  1 Interior: vtkPointSource with a uniform distribution inside the ball; the seed count is
   *    exactly SphereInteriorPointCount.
   */
  enum SphereSeedModeType
  {
    SphereSurface = 0,
    SphereInterior = 1
  };

  vtkSetClampMacro(SphereSeedMode, int, SphereSurface, SphereInterior);
  vtkGetMacro(SphereSeedMode, int);
  ///@}

  ///@{
  /**
   * Sphere seed. For SphereSurface the tessellation is
   * ThetaResolution (longitude / around the axis) x PhiResolution (latitude pole to pole);
   * VTK produces (ThetaResolution) * (PhiResolution - 2) + 2 vertices, so 32 x 24 is 722 points.
   * For SphereInterior only the center and radius matter; the count comes from
   * SphereInteriorPointCount.
   */
  vtkSetVector3Macro(SphereCenter, double);
  vtkGetVector3Macro(SphereCenter, double);
  vtkSetClampMacro(SphereRadius, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(SphereRadius, double);
  vtkSetClampMacro(SphereThetaResolution, int, 3, 1000);
  vtkGetMacro(SphereThetaResolution, int);
  vtkSetClampMacro(SpherePhiResolution, int, 3, 1000);
  vtkGetMacro(SpherePhiResolution, int);

  /** Number of seeds sampled uniformly inside the ball when SphereSeedMode is SphereInterior. */
  vtkSetClampMacro(SphereInteriorPointCount, vtkIdType, 1, VTK_ID_MAX);
  vtkGetMacro(SphereInteriorPointCount, vtkIdType);

  /**
   * Seed of the interior sampler, so the same count gives the same points on every Apply.
   * Ignored for the surface mode (a tessellation is deterministic).
   */
  vtkSetMacro(SphereRandomSeed, int);
  vtkGetMacro(SphereRandomSeed, int);
  ///@}

  ///@{
  /** Snap the sphere center to the nearest input-surface vertex before tracing. */
  vtkSetMacro(SphereSnapCenter, vtkTypeBool);
  vtkGetMacro(SphereSnapCenter, vtkTypeBool);
  vtkBooleanMacro(SphereSnapCenter, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Rectangle (plane) seed, defined the same way the 3D editor drives it:
   *   1. a center point (the only thing that snaps to the surface),
   *   2. a normal (Screen camera on Place in view, so the quad faces the viewer),
   *   3. Width / Height around that center.
   * The two in-plane axes are derived from the normal, so Width / Height are the full edge
   * lengths; the center stays put when they change.
   */
  vtkSetVector3Macro(RectangleCenter, double);
  vtkGetVector3Macro(RectangleCenter, double);
  vtkSetVector3Macro(RectangleNormal, double);
  vtkGetVector3Macro(RectangleNormal, double);
  vtkSetClampMacro(RectangleWidth, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(RectangleWidth, double);
  vtkSetClampMacro(RectangleHeight, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(RectangleHeight, double);
  vtkSetClampMacro(RectangleResolutionX, int, 1, 1000);
  vtkGetMacro(RectangleResolutionX, int);
  vtkSetClampMacro(RectangleResolutionY, int, 1, 1000);
  vtkGetMacro(RectangleResolutionY, int);
  ///@}

  ///@{
  /**
   * Snap RectangleCenter to the nearest input-surface vertex. Width, Height and Normal are
   * unchanged: only the quad's position moves.
   */
  vtkSetMacro(RectangleSnapCenter, vtkTypeBool);
  vtkGetMacro(RectangleSnapCenter, vtkTypeBool);
  vtkBooleanMacro(RectangleSnapCenter, vtkTypeBool);
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

  vtkSetClampMacro(MaximumError, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(MaximumError, double);

  vtkSetMacro(ComputeVorticity, vtkTypeBool);
  vtkGetMacro(ComputeVorticity, vtkTypeBool);
  vtkBooleanMacro(ComputeVorticity, vtkTypeBool);
  ///@}

  /**
   * Move points[i] to the nearest vertex of the input surface when enabled[i] is non-zero.
   * The surface is extracted once (polydata inputs are used directly). Returns true if any
   * point moved. Used by the client widget on release and by RequestData for Python users.
   */
  static bool SnapPointsToSurface(
    vtkDataSet* input, double* points, int count, const int* enabled);

  /** Convenience wrapper for a single point. */
  static bool SnapPointToSurface(vtkDataSet* input, const double in[3], double out[3]);

protected:
  vtkSHYXInteractiveStreamTracer();
  ~vtkSHYXInteractiveStreamTracer() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int SeedShape = Line;

  double LinePoint1[3] = { 0.0, 0.0, 0.0 };
  double LinePoint2[3] = { 1.0, 0.0, 0.0 };
  int LineResolution = 20;
  vtkTypeBool LineSnapPoint1 = 1;
  vtkTypeBool LineSnapPoint2 = 1;

  double SphereCenter[3] = { 0.0, 0.0, 0.0 };
  double SphereRadius = 1.0;
  int SphereThetaResolution = 32;
  int SpherePhiResolution = 24;
  int SphereSeedMode = SphereInterior;
  vtkIdType SphereInteriorPointCount = 20;
  int SphereRandomSeed = 1;
  vtkTypeBool SphereSnapCenter = 1;

  double RectangleCenter[3] = { 0.0, 0.0, 0.0 };
  double RectangleNormal[3] = { 0.0, 0.0, 1.0 };
  double RectangleWidth = 2.0;
  double RectangleHeight = 2.0;
  int RectangleResolutionX = 10;
  int RectangleResolutionY = 10;
  vtkTypeBool RectangleSnapCenter = 1;

  int IntegrationDirection = 2; // BOTH
  int IntegratorType = 2;       // RK45
  int IntegrationStepUnit = 2;  // CELL_LENGTH_UNIT
  double InitialIntegrationStep = 0.2;
  double MaximumPropagation = 1.0;
  vtkIdType MaximumNumberOfSteps = 2000;
  double TerminalSpeed = 1.0e-12;
  int InterpolatorType = 0; // point locator
  double MaximumError = 1.0e-6;
  vtkTypeBool ComputeVorticity = 1;

private:
  vtkSHYXInteractiveStreamTracer(const vtkSHYXInteractiveStreamTracer&) = delete;
  void operator=(const vtkSHYXInteractiveStreamTracer&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
