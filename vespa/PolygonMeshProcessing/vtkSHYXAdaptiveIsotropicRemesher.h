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

#include "vtkCGALPMPModule.h"

class VTKCGALPMP_EXPORT vtkSHYXAdaptiveIsotropicRemesher : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXAdaptiveIsotropicRemesher* New();
  vtkTypeMacro(vtkSHYXAdaptiveIsotropicRemesher, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Minimum allowed edge length after remeshing.
   * If <= 0, a default of 0.5% of the input bounding-box diagonal is used.
   */
  vtkGetMacro(MinEdgeLength, double);
  vtkSetMacro(MinEdgeLength, double);
  //@}

  //@{
  /**
   * Maximum allowed edge length after remeshing.
   * If <= 0, a default of 5% of the input bounding-box diagonal is used.
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

  /**
   * Set MinEdgeLength and MaxEdgeLength from the current input polydata using the same rule as
   * automatic defaults: 0.5% and 5% of the input bounding-box diagonal (vtkPolyData::GetLength()).
   * Intended for ParaView (command_button); updates upstream if needed so bounds are available.
   */
  virtual void RefreshSuggestedEdgeLengthsFromBounds();

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
