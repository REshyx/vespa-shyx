/**
 * @class   vtkSHYXAdaptiveIsotropicRemesher
 * @brief   Curvature-aware isotropic remesh with min/max edge length and tolerance.
 *
 * Uses CGAL::Polygon_mesh_processing::Adaptive_sizing_field (CGAL 6.0+) with
 * isotropic_remeshing. Finer triangles tend to appear in higher-curvature regions;
 * edge lengths are clamped to [MinEdgeLength, MaxEdgeLength]. Feature edges are
 * detected and protected like vtkCGALIsotropicRemesher.
 *
 * Requires CGAL 6.0 or newer.
 */

#ifndef vtkSHYXAdaptiveIsotropicRemesher_h
#define vtkSHYXAdaptiveIsotropicRemesher_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXAdaptiveIsotropicRemesherModule.h"

class VTKSHYXADAPTIVEISOTROPICREMESHER_EXPORT vtkSHYXAdaptiveIsotropicRemesher : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXAdaptiveIsotropicRemesher* New();
  vtkTypeMacro(vtkSHYXAdaptiveIsotropicRemesher, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Minimum allowed edge length after remeshing.
   * If <= 0, a default of 0.5% of the input axis-aligned bounding box longest side
   * (vtkBoundingBox::GetMaxLength()) is used, matching ParaView BoundsDomain scaled_extent
   * with scale 0.005 on this property in the Server Manager XML.
   */
  vtkGetMacro(MinEdgeLength, double);
  vtkSetMacro(MinEdgeLength, double);
  //@}

  //@{
  /**
   * Maximum allowed edge length after remeshing.
   * If <= 0, a default of 5% of that same longest-side length is used.
   */
  vtkGetMacro(MaxEdgeLength, double);
  vtkSetMacro(MaxEdgeLength, double);
  //@}

  //@{
  /**
   * Error tolerance passed to CGAL Adaptive_sizing_field (together with
   * discrete curvature). Smaller values generally yield finer meshes within
   * the min/max bounds. Must be strictly positive.
   */
  vtkGetMacro(AdaptiveTolerance, double);
  vtkSetMacro(AdaptiveTolerance, double);
  //@}

  //@{
  /**
   * Feature edge angle threshold (degrees); protected during remeshing.
   * Default 45.
   */
  vtkGetMacro(ProtectAngle, double);
  vtkSetMacro(ProtectAngle, double);
  //@}

  //@{
  /**
   * Number of CGAL isotropic remeshing iterations. Default 3.
   */
  vtkGetMacro(NumberOfIterations, int);
  vtkSetMacro(NumberOfIterations, int);
  //@}

protected:
  vtkSHYXAdaptiveIsotropicRemesher() = default;
  ~vtkSHYXAdaptiveIsotropicRemesher() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double MinEdgeLength       = -1.0;
  double MaxEdgeLength       = -1.0;
  double AdaptiveTolerance   = 0.001;
  double ProtectAngle        = 45.0;
  int    NumberOfIterations  = 3;

private:
  vtkSHYXAdaptiveIsotropicRemesher(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
  void operator=(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
};

#endif
