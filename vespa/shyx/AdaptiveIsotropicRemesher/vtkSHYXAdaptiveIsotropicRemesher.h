/**
 * @class   vtkSHYXAdaptiveIsotropicRemesher
 * @brief   Curvature-aware isotropic remesh with min/max edge length, tolerance, and configurable
 *          relaxation steps per iteration.
 *
 * Uses CGAL::Polygon_mesh_processing::Adaptive_sizing_field (CGAL 6.0+) as the sizing function
 * for isotropic_remeshing on faces (curvature-driven target edge length within min/max bounds).
 *
 * Optional vtkSelection on port 1 restricts CGAL isotropic remeshing to the selected faces;
 * the rest of the surface is unchanged.
 * Edges on the boundary between selected and unselected faces are also marked as
 * CGAL feature/constrained edges for isotropic_remeshing and for smooth_shape (together
 * with angle-based sharp edges and optional FeatureMask filtering).
 * With no selection, the whole surface is remeshed (previous behavior).
 *
 * Optional post-remesh CGAL smooth_shape re-detects sharp edges on the remeshed surface.
 * When Feature mask is enabled, the same cell/point array and threshold are evaluated on the
 * remeshed surface (after attribute interpolation) to clip sharp features and to mark mask-region
 * boundary edges. smooth_shape anchor positions from the mask use sharp endpoints on the
 * **remeshed** threshold patch (not the input port 2 patch). Selection boundary anchors still use
 * input topology.
 *
 * Output port 0: remeshed (and optionally shape-smoothed) vtkPolyData.
 * Output port 1: **Lines** (sharp feature edges) use the **input** mask region (port 2) when the
 * feature mask is valid and non-empty; otherwise the full remeshed surface (with optional mask
 * filtering on the output). **Vertices** (feature points) are added only if **ShapeSmoothingIterations
 * &gt; 0**: with feature mask, from the remeshed threshold patch (port 3); without mask, from the
 * full remeshed surface. If there is no smooth step, port 1 has no vertex cells.
 * Output port 2: input geometry, faces passing the feature mask threshold (for remesh constraints).
 * Output port 3: same as port 0 after remesh/smooth, faces passing the mask threshold (remeshed
 * mask patch for visualization and for smooth anchor geometry when Feature Mask is on).
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

  //@{
  /**
   * CGAL protect_constraints for isotropic_remeshing. When true, edges marked constrained in
   * edge_is_constrained_map (here: sharp features) are neither split nor collapsed. When false,
   * constrained edges may be split or collapsed per CGAL (subject to collapse_constraints); sizing
   * is still Adaptive_sizing_field on the remeshed patch. Default true.
   */
  vtkGetMacro(RemeshProtectConstraints, bool);
  vtkSetMacro(RemeshProtectConstraints, bool);
  vtkBooleanMacro(RemeshProtectConstraints, bool);
  //@}

  //@{
  /**
   * CGAL collapse_constraints: allow collapsing constrained edges when RemeshProtectConstraints
   * is false. Ignored by CGAL when RemeshProtectConstraints is true. Default true (CGAL default).
   */
  vtkGetMacro(RemeshCollapseConstraints, bool);
  vtkSetMacro(RemeshCollapseConstraints, bool);
  vtkBooleanMacro(RemeshCollapseConstraints, bool);
  //@}

  //@{
  /**
   * CGAL relax_constraints: move endpoints of constrained and boundary edges along constrained
   * polylines during tangential relaxation. Default false (CGAL default).
   */
  vtkGetMacro(RemeshRelaxConstraints, bool);
  vtkSetMacro(RemeshRelaxConstraints, bool);
  vtkBooleanMacro(RemeshRelaxConstraints, bool);
  //@}

  //@{
  /** CGAL do_split for isotropic_remeshing. Default true. */
  vtkGetMacro(RemeshDoSplit, bool);
  vtkSetMacro(RemeshDoSplit, bool);
  vtkBooleanMacro(RemeshDoSplit, bool);
  //@}

  //@{
  /** CGAL do_collapse for isotropic_remeshing. Default true. */
  vtkGetMacro(RemeshDoCollapse, bool);
  vtkSetMacro(RemeshDoCollapse, bool);
  vtkBooleanMacro(RemeshDoCollapse, bool);
  //@}

  //@{
  /** CGAL do_flip for isotropic_remeshing. Default true. */
  vtkGetMacro(RemeshDoFlip, bool);
  vtkSetMacro(RemeshDoFlip, bool);
  vtkBooleanMacro(RemeshDoFlip, bool);
  //@}

  //@{
  /**
   * Master switch for sharp-edge / feature-mask constraints.
   * When true (default), CGAL detect_sharp_edges (with ProtectAngle / SharpFeatureSideFilter)
   * and feature-mask region/boundary contributions are added to edge_is_constrained_map for
   * isotropic_remeshing and to vertex_is_constrained_map for smooth_shape; ProtectAngle,
   * SharpFeatureSideFilter, FeatureMaskEnabled, and related properties take effect.
   * When false, those sources do NOT write feature-edge constraints; remesh and smooth_shape
   * see only vtkSelection-boundary constraints (when a selection input is connected).
   * vtkSelection behavior is independent of this toggle.
   */
  vtkGetMacro(DetectFeatureEdges, bool);
  vtkSetMacro(DetectFeatureEdges, bool);
  vtkBooleanMacro(DetectFeatureEdges, bool);
  //@}

  //@{
  /**
   * When true, feature extraction on port 2 and constraints use FeatureMaskArrayName:
   * tuple magnitude must be strictly greater than FeatureMaskThreshold. The array may live on
   * points or cells (same name resolves to cell data if a valid per-cell array exists, otherwise
   * point data). For point-centered arrays, FeatureMaskAllScalars controls whether every corner
   * of a cell must pass (true) or any corner suffices (false). Default false (no mask).
   * Has no effect when DetectFeatureEdges is false.
   */
  vtkGetMacro(FeatureMaskEnabled, bool);
  vtkSetMacro(FeatureMaskEnabled, bool);
  vtkBooleanMacro(FeatureMaskEnabled, bool);
  //@}

  vtkSetStringMacro(FeatureMaskArrayName);
  vtkGetStringMacro(FeatureMaskArrayName);

  //@{
  /**
   * Cells pass the mask when tuple magnitude is strictly greater than this value.
   * Default 0 (common case: mark values &gt; 0).
   */
  vtkGetMacro(FeatureMaskThreshold, double);
  vtkSetMacro(FeatureMaskThreshold, double);
  //@}

  //@{
  /**
   * When the chosen mask array is point-centered: if true, every corner point of a cell must pass
   * the magnitude test; if false, one passing corner is enough. Ignored for cell-centered arrays.
   * Default false.
   */
  vtkGetMacro(FeatureMaskAllScalars, bool);
  vtkSetMacro(FeatureMaskAllScalars, bool);
  vtkBooleanMacro(FeatureMaskAllScalars, bool);
  //@}

protected:
  vtkSHYXAdaptiveIsotropicRemesher();
  ~vtkSHYXAdaptiveIsotropicRemesher() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  double MinEdgeLength       = 0.0;
  double MaxEdgeLength       = 0.0;
  double AdaptiveTolerance   = 0.01;
  double ProtectAngle        = 70.0;
  int    NumberOfIterations  = 3;
  int    NumberOfRelaxationSteps = 3;
  int    ShapeSmoothingIterations = 0;
  double ShapeSmoothingTimeStep   = 1e-4;
  int    SharpFeatureSideFilter   = 0;
  bool   RemeshProtectConstraints   = true;
  bool   RemeshCollapseConstraints  = true;
  bool   RemeshRelaxConstraints     = false;
  bool   RemeshDoSplit              = true;
  bool   RemeshDoCollapse           = true;
  bool   RemeshDoFlip               = true;

  bool   DetectFeatureEdges    = true;
  bool   FeatureMaskEnabled    = false;
  char*  FeatureMaskArrayName = nullptr;
  double FeatureMaskThreshold  = 0.0;
  bool   FeatureMaskAllScalars   = false;

private:
  vtkSHYXAdaptiveIsotropicRemesher(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
  void operator=(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
};

#endif
