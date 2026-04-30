/**
 * @class   vtkSHYXAdaptiveIsotropicRemesher
 * @brief   Curvature-aware isotropic remesh with min/max edge length, tolerance, and configurable
 *          relaxation steps per iteration.
 *
 * Uses CGAL::Polygon_mesh_processing::Adaptive_sizing_field (CGAL 6.0+) with
 * isotropic_remeshing. Finer triangles tend to appear in higher-curvature regions;
 * edge lengths are clamped to [MinEdgeLength, MaxEdgeLength]. Feature edges are
 * detected and protected like vtkCGALIsotropicRemesher.
 *
 * Optional Selection (port 1) or SelectionCellArrayName on the input restricts
 * CGAL isotropic remeshing to those faces; the rest of the surface is unchanged.
 * With no selection, the whole surface is remeshed (previous behavior).
 *
 * Optional post-remesh CGAL smooth_shape uses the same feature angle: sharp edges
 * are re-detected on the remeshed surface and incident vertices are constrained.
 *
 * Output port 0: remeshed (and optionally shape-smoothed) vtkPolyData.
 * Output port 1: detected sharp features on the final mesh as vtkPolyData with
 * VTK_LINE cells (feature edges) and VTK_VERTEX cells (endpoints on feature edges),
 * using the same Protection Angle and optional signed-side filter (see SharpFeatureSideFilter).
 *
 * Requires CGAL 6.0 or newer.
 */

#ifndef vtkSHYXAdaptiveIsotropicRemesher_h
#define vtkSHYXAdaptiveIsotropicRemesher_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXAdaptiveIsotropicRemesherModule.h"

class vtkAlgorithmOutput;
class vtkInformation;

class VTKSHYXADAPTIVEISOTROPICREMESHER_EXPORT vtkSHYXAdaptiveIsotropicRemesher : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXAdaptiveIsotropicRemesher* New();
  vtkTypeMacro(vtkSHYXAdaptiveIsotropicRemesher, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** vtkSelection input (port 1), same pattern as SHYX Delete Selected Cells. */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * When port 1 has no usable selection: name of a cell data array on port 0.
   * A face is included in the remesh patch if the first component is > 0.5 (float)
   * or non-zero (integral types).
   */
  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

  //@{
  /**
   * Minimum allowed edge length after remeshing. Must be strictly positive.
   * ParaView exposes BoundsDomain (scaled_extent 0.001) on this property for suggested values.
   */
  vtkGetMacro(MinEdgeLength, double);
  vtkSetMacro(MinEdgeLength, double);
  //@}

  //@{
  /**
   * Maximum allowed edge length after remeshing. Must be greater than MinEdgeLength.
   * ParaView exposes BoundsDomain (scaled_extent 0.05) on this property for suggested values.
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
   * Default 70.
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

  //@{
  /**
   * Relaxation steps per CGAL isotropic remeshing iteration (vertex relocation
   * smoothing strength). Larger values tend to produce a smoother mesh; 0 disables
   * relaxation. Default 3.
   */
  vtkGetMacro(NumberOfRelaxationSteps, int);
  vtkSetMacro(NumberOfRelaxationSteps, int);
  //@}

  //@{
  /**
   * Post-remesh CGAL smooth_shape iteration count. 0 disables shape smoothing.
   * Default 0.
   */
  vtkGetMacro(ShapeSmoothingIterations, int);
  vtkSetMacro(ShapeSmoothingIterations, int);
  //@}

  //@{
  /**
   * CGAL smooth_shape time step (smoothing speed). Used only when
   * ShapeSmoothingIterations > 0. Typical range about 1e-6 to 1. Default 1e-4.
   */
  vtkGetMacro(ShapeSmoothingTimeStep, double);
  vtkSetMacro(ShapeSmoothingTimeStep, double);
  //@}

  //@{
  /**
   * After detect_sharp_edges, optionally remove interior sharp edges on one side of the
   * signed dihedral (CGAL::approximate_dihedral_angle). 0 = none; 1 = exclude concave
   * (signed angle &lt; 0); 2 = exclude convex (signed angle &gt; 0). Boundary edges are
   * unchanged. Sign depends on mesh face orientation; swap 1/2 if results look inverted.
   */
  vtkGetMacro(SharpFeatureSideFilter, int);
  vtkSetClampMacro(SharpFeatureSideFilter, int, 0, 2);
  //@}

protected:
  vtkSHYXAdaptiveIsotropicRemesher();
  ~vtkSHYXAdaptiveIsotropicRemesher() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  char* SelectionCellArrayName = nullptr;

  double MinEdgeLength       = 0.0;
  double MaxEdgeLength       = 0.0;
  double AdaptiveTolerance   = 0.01;
  double ProtectAngle        = 70.0;
  int    NumberOfIterations  = 3;
  int    NumberOfRelaxationSteps = 3;
  int    ShapeSmoothingIterations = 0;
  double ShapeSmoothingTimeStep   = 1e-4;
  int    SharpFeatureSideFilter   = 0;

private:
  vtkSHYXAdaptiveIsotropicRemesher(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
  void operator=(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
};

#endif
