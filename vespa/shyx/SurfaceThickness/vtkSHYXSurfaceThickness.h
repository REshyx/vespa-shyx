/**
 * @class   vtkSHYXSurfaceThickness
 * @brief   Per-vertex thickness on a surface mesh (self-proximity or inward ray).
 *
 * Copies the input vtkPolyData and adds point-data arrays:
 * - Thickness: wall-to-wall distance, or -1 if no opposite was found
 * - ThicknessOverEdgeLength: Thickness / mean incident edge length, or -1
 * - ThicknessValid: 1 if Thickness is a real measurement (including 0 for coincident
 *   kissing / crossing), 0 if no opposite was found
 * - LocalEdgeLength: mean incident edge length (always written)
 *
 * Method 0 (default) Self-proximity does not ray-cast: it takes the nearest Euclidean
 * neighbor that is outside a small geodesic ring and, optionally, in the inward
 * half-space. That still works when faces cross. Methods 1–2 shoot inward rays and
 * fail or read ~0 on self-intersections.
 *
 * MaxDistance <= 0 uses 0.05 times the longest AABB side (same scale as the
 * ParaView BoundsDomain scaled_extent on the property).
 *
 * @sa vtkStaticPointLocator vtkStaticCellLocator vtkSMPTools
 */

#ifndef vtkSHYXSurfaceThickness_h
#define vtkSHYXSurfaceThickness_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXSurfaceThicknessModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXSURFACETHICKNESS_EXPORT vtkSHYXSurfaceThickness : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXSurfaceThickness* New();
  vtkTypeMacro(vtkSHYXSurfaceThickness, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum MethodType
  {
    SELF_PROXIMITY = 0,
    RAY_ALONG_NORMAL = 1,
    SHAPE_DIAMETER = 2
  };

  vtkGetMacro(Method, int);
  vtkSetClampMacro(Method, int, 0, 2);

  /**
   * Search / ray length. <= 0 means 0.05 * longest AABB side.
   */
  vtkGetMacro(MaxDistance, double);
  vtkSetMacro(MaxDistance, double);

  /** Reverse the interpreted outward normal (inward = +n instead of -n). */
  vtkGetMacro(FlipNormals, bool);
  vtkSetMacro(FlipNormals, bool);
  vtkBooleanMacro(FlipNormals, bool);

  /**
   * Self-proximity: drop vertices within this many edge hops (same wall).
   * Keep small (default 2) so a coarse tube's opposite wall is not excluded.
   */
  vtkGetMacro(GeodesicRings, int);
  vtkSetClampMacro(GeodesicRings, int, 1, 32);

  /**
   * Self-proximity: only accept neighbors in the inward half-space
   * (angle from inward axis less than 75 degrees). Default on.
   */
  vtkGetMacro(InwardOnly, bool);
  vtkSetMacro(InwardOnly, bool);
  vtkBooleanMacro(InwardOnly, bool);

  /**
   * Ray start offset along inward. <= 0 means 0.25 * that vertex's mean edge length.
   */
  vtkGetMacro(RayOffset, double);
  vtkSetMacro(RayOffset, double);

  /**
   * Ray methods: reject a hit whose cell normal faces the same way as the query
   * (grazing the same wall). Default on.
   */
  vtkGetMacro(RequireOppositeNormal, bool);
  vtkSetMacro(RequireOppositeNormal, bool);
  vtkBooleanMacro(RequireOppositeNormal, bool);

  /** Shape diameter: rays in the cone, including the inward axis. Default 9. */
  vtkGetMacro(NumberOfRays, int);
  vtkSetClampMacro(NumberOfRays, int, 1, 64);

  /** Shape diameter cone half-angle in degrees. Default 30. Ignored when NumberOfRays is 1. */
  vtkGetMacro(ConeAngle, double);
  vtkSetClampMacro(ConeAngle, double, 0.0, 89.0);

protected:
  vtkSHYXSurfaceThickness();
  ~vtkSHYXSurfaceThickness() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  int Method = SELF_PROXIMITY;
  double MaxDistance = 0.0;
  bool FlipNormals = false;
  int GeodesicRings = 2;
  bool InwardOnly = true;
  double RayOffset = 0.0;
  bool RequireOppositeNormal = true;
  int NumberOfRays = 9;
  double ConeAngle = 30.0;

private:
  vtkSHYXSurfaceThickness(const vtkSHYXSurfaceThickness&) = delete;
  void operator=(const vtkSHYXSurfaceThickness&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
