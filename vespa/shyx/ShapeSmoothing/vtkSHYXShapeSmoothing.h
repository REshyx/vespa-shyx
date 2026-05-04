/**
 * @class   vtkSHYXShapeSmoothing
 * @brief   Switchable triangle-mesh smoothing on vtkPolyData using CGAL Polygon Mesh Processing.
 *
 * Three algorithms are exposed and selectable via SmoothingMethod:
 *
 * - SHAPE_MCF (`CGAL::Polygon_mesh_processing::smooth_shape`): mean curvature flow. Vertices move
 *   along surface normals proportionally to mean curvature for `NumberOfIterations` iterations
 *   with time-step `ShapeTimeStep`. Volume preservation is approximated by `ShapeDoScale` which
 *   rescales the smoothed mesh after each iteration; CGAL only applies rescaling when the mesh is
 *   closed and at most one vertex is constrained (otherwise it is silently skipped).
 *
 * - ANGLE_AND_AREA (`CGAL::Polygon_mesh_processing::angle_and_area_smoothing`): equalises angle
 *   and/or area distributions (area-based smoothing requires Ceres; angle-based is always
 *   available). `DoProject` reprojects vertices on the input surface after each iteration which
 *   keeps the overall shape (and therefore volume) close to the input, even when constraints are
 *   sparse. `UseSafetyConstraints` rejects vertex moves that would worsen the smallest angle or
 *   create self-intersections; `UseDelaunayFlips` finishes each area-smoothing pass with
 *   Delaunay-based edge flips.
 *
 * - FAIR (`CGAL::Polygon_mesh_processing::fair`): bi-Laplacian fairing of a vertex range with
 *   tangential continuity `FairingContinuity` (0=C0, 1=C1, 2=C2). Only the vertices given to
 *   `fair()` move; everything else acts as boundary condition. With `FairScope = MaskRegionOnly`
 *   only the (mask-region, non-constrained) vertices move, so the rest of the mesh is preserved
 *   exactly.
 *
 * Constrained vertices come from
 *  - sharp edges via `pmp::detect_sharp_edges(ProtectAngle)` filtered by `SharpFeatureSideFilter`
 *    (signed dihedral angle, like vtkSHYXAdaptiveIsotropicRemesher),
 *  - optional Feature Mask: tuple-magnitude threshold on a point/cell array; mask-region boundary
 *    edges are added to the feature edge set, sharp edges leaving the mask region are dropped,
 *    and sharp endpoints inside the mask threshold patch are also added as anchor positions
 *    (snapped onto the working mesh within `AnchorTolerance` of the bounding-box length).
 *
 * Output port 0: smoothed vtkPolyData. Output port 1: a diagnostic vtkPolyData containing the
 * sharp/feature edges (lines) and constrained feature points (vertex cells) that were used.
 *
 * Requires CGAL 5.4+ (smooth_shape and fair). angle_and_area_smoothing area path additionally
 * requires Ceres at build time (CGAL_PMP_USE_CERES_SOLVER).
 */

#ifndef vtkSHYXShapeSmoothing_h
#define vtkSHYXShapeSmoothing_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXShapeSmoothingModule.h"

class VTKSHYXSHAPESMOOTHING_EXPORT vtkSHYXShapeSmoothing : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXShapeSmoothing* New();
  vtkTypeMacro(vtkSHYXShapeSmoothing, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum SmoothingMethodType
  {
    SHAPE_MCF      = 0, ///< CGAL::Polygon_mesh_processing::smooth_shape (mean curvature flow)
    ANGLE_AND_AREA = 1, ///< CGAL::Polygon_mesh_processing::angle_and_area_smoothing
    FAIR           = 2  ///< CGAL::Polygon_mesh_processing::fair (bi-Laplacian)
  };

  enum FairScopeType
  {
    FAIR_ALL_NON_CONSTRAINED = 0, ///< fair every non-constrained vertex of the mesh
    FAIR_MASK_REGION_ONLY    = 1  ///< fair only mask-region vertices (requires Feature Mask ON)
  };

  //@{
  /** Selected smoothing algorithm. Default SHAPE_MCF. */
  vtkGetMacro(SmoothingMethod, int);
  vtkSetClampMacro(SmoothingMethod, int, 0, 2);
  //@}

  //@{
  /**
   * Iterations for SHAPE_MCF and ANGLE_AND_AREA; ignored by FAIR (a single linear solve).
   * Default 1.
   */
  vtkGetMacro(NumberOfIterations, int);
  vtkSetMacro(NumberOfIterations, int);
  //@}

  //@{
  /**
   * Feature edge angle threshold (degrees) for `pmp::detect_sharp_edges`. Endpoints of detected
   * sharp edges become constrained vertices for the smoother. Default 70.
   */
  vtkGetMacro(ProtectAngle, double);
  vtkSetMacro(ProtectAngle, double);
  //@}

  //@{
  /**
   * Optional dihedral-side filter applied after `detect_sharp_edges` (same semantics as
   * vtkSHYXAdaptiveIsotropicRemesher::SharpFeatureSideFilter). 0=none, 1=exclude concave
   * (signed angle &lt; 0), 2=exclude convex (signed angle &gt; 0). Default 0.
   */
  vtkGetMacro(SharpFeatureSideFilter, int);
  vtkSetClampMacro(SharpFeatureSideFilter, int, 0, 2);
  //@}

  //@{
  /**
   * Relative tolerance (fraction of the input AABB longest side) used when snapping anchor
   * positions (collected from the mask threshold patch) onto the working mesh's vertices.
   * Default 1e-4.
   */
  vtkGetMacro(AnchorTolerance, double);
  vtkSetMacro(AnchorTolerance, double);
  //@}

  //@{
  /**
   * Master switch for sharp-edge / feature-mask constraints (mirrors
   * vtkSHYXAdaptiveIsotropicRemesher::DetectFeatureEdges). When true (default), CGAL
   * detect_sharp_edges (ProtectAngle / SharpFeatureSideFilter) and feature-mask
   * region/boundary contributions populate the constrained edge map and constrained
   * vertex map used by the selected smoother. When false, none of those sources contribute;
   * featureEdges and vertexConstrained stay empty, so SHAPE_MCF and ANGLE_AND_AREA run with
   * no constraints, FAIR triggers the "every vertex would be moved" error unless the mesh
   * has another natural boundary.
   */
  vtkGetMacro(DetectFeatureEdges, bool);
  vtkSetMacro(DetectFeatureEdges, bool);
  vtkBooleanMacro(DetectFeatureEdges, bool);
  //@}

  //@{
  /**
   * Enable threshold-based Feature Mask. When ON, FeatureMaskArrayName / FeatureMaskThreshold /
   * FeatureMaskAllScalars define the active region (cells passing the magnitude test). Sharp
   * edges leaving the mask are cleared, mask-region boundary edges are added as feature edges,
   * and sharp endpoints inside the mask patch contribute anchor positions. Default OFF.
   * Has no effect when DetectFeatureEdges is false.
   */
  vtkGetMacro(FeatureMaskEnabled, bool);
  vtkSetMacro(FeatureMaskEnabled, bool);
  vtkBooleanMacro(FeatureMaskEnabled, bool);
  //@}

  vtkSetStringMacro(FeatureMaskArrayName);
  vtkGetStringMacro(FeatureMaskArrayName);

  //@{
  /** Cells pass the mask when tuple magnitude is strictly greater than this. Default 0. */
  vtkGetMacro(FeatureMaskThreshold, double);
  vtkSetMacro(FeatureMaskThreshold, double);
  //@}

  //@{
  /**
   * For point-centered mask arrays: if true, every corner of a cell must pass; if false, any
   * corner is enough (vtkThreshold-style). Ignored for cell-centered arrays. Default false.
   */
  vtkGetMacro(FeatureMaskAllScalars, bool);
  vtkSetMacro(FeatureMaskAllScalars, bool);
  vtkBooleanMacro(FeatureMaskAllScalars, bool);
  //@}

  // ---- SHAPE_MCF (smooth_shape) -------------------------------------------------------------

  //@{
  /**
   * `time` parameter passed to `smooth_shape` (smoothing speed). Larger values converge faster
   * but distort details more. Typical range about 1e-6 to 1. Default 1e-4.
   */
  vtkGetMacro(ShapeTimeStep, double);
  vtkSetMacro(ShapeTimeStep, double);
  //@}

  //@{
  /**
   * CGAL `do_scale` named parameter for `smooth_shape`. When ON (default), CGAL rescales the
   * smoothed mesh to compensate for the volume shrinkage of mean curvature flow. CGAL only
   * applies the rescaling on closed meshes with at most one constrained vertex; otherwise it is
   * silently skipped.
   */
  vtkGetMacro(ShapeDoScale, bool);
  vtkSetMacro(ShapeDoScale, bool);
  vtkBooleanMacro(ShapeDoScale, bool);
  //@}

  // ---- ANGLE_AND_AREA -----------------------------------------------------------------------

  //@{
  /** CGAL `use_angle_smoothing` named parameter. Default true. */
  vtkGetMacro(UseAngleSmoothing, bool);
  vtkSetMacro(UseAngleSmoothing, bool);
  vtkBooleanMacro(UseAngleSmoothing, bool);
  //@}

  //@{
  /**
   * CGAL `use_area_smoothing` named parameter. Default true. Requires Ceres at build time
   * (CGAL_PMP_USE_CERES_SOLVER). Without Ceres CGAL prints a warning and disables it.
   */
  vtkGetMacro(UseAreaSmoothing, bool);
  vtkSetMacro(UseAreaSmoothing, bool);
  vtkBooleanMacro(UseAreaSmoothing, bool);
  //@}

  //@{
  /**
   * CGAL `use_safety_constraints`: reject moves that decrease the smallest angle around a vertex
   * or create self-intersections. Default true (matches CGAL header default).
   */
  vtkGetMacro(UseSafetyConstraints, bool);
  vtkSetMacro(UseSafetyConstraints, bool);
  vtkBooleanMacro(UseSafetyConstraints, bool);
  //@}

  //@{
  /** CGAL `use_Delaunay_flips`: finish area smoothing with Delaunay edge flips. Default true. */
  vtkGetMacro(UseDelaunayFlips, bool);
  vtkSetMacro(UseDelaunayFlips, bool);
  vtkBooleanMacro(UseDelaunayFlips, bool);
  //@}

  //@{
  /**
   * CGAL `do_project`: after each iteration, reproject vertices onto an AABB tree built from the
   * input triangles. This is the main shape/volume-preservation knob for angle_and_area_smoothing.
   * Default true.
   */
  vtkGetMacro(DoProject, bool);
  vtkSetMacro(DoProject, bool);
  vtkBooleanMacro(DoProject, bool);
  //@}

  // ---- FAIR ----------------------------------------------------------------------------------

  //@{
  /**
   * CGAL `fairing_continuity` named parameter (0=C0, 1=C1, 2=C2). Higher continuity needs more
   * fixed boundary vertices. Default 1.
   */
  vtkGetMacro(FairingContinuity, int);
  vtkSetClampMacro(FairingContinuity, int, 0, 2);
  //@}

  //@{
  /**
   * Vertex range passed to `fair()`:
   *  - FAIR_ALL_NON_CONSTRAINED (0): every vertex of the mesh that is not constrained by sharp
   *    edges or mask-boundary edges is faired.
   *  - FAIR_MASK_REGION_ONLY (1): only vertices fully inside the Feature Mask region (whose
   *    incident faces all pass the mask) and not constrained are faired.
   * Default FAIR_ALL_NON_CONSTRAINED.
   */
  vtkGetMacro(FairScope, int);
  vtkSetClampMacro(FairScope, int, 0, 1);
  //@}

protected:
  vtkSHYXShapeSmoothing();
  ~vtkSHYXShapeSmoothing() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  int    SmoothingMethod        = SHAPE_MCF;
  int    NumberOfIterations     = 1;
  double ProtectAngle           = 70.0;
  int    SharpFeatureSideFilter = 0;
  double AnchorTolerance        = 1e-4;

  bool   DetectFeatureEdges    = true;
  bool   FeatureMaskEnabled    = false;
  char*  FeatureMaskArrayName  = nullptr;
  double FeatureMaskThreshold  = 0.0;
  bool   FeatureMaskAllScalars = false;

  double ShapeTimeStep = 1e-4;
  bool   ShapeDoScale  = true;

  bool   UseAngleSmoothing    = true;
  bool   UseAreaSmoothing     = true;
  bool   UseSafetyConstraints = true;
  bool   UseDelaunayFlips     = true;
  bool   DoProject            = true;

  int    FairingContinuity = 1;
  int    FairScope         = FAIR_ALL_NON_CONSTRAINED;

private:
  vtkSHYXShapeSmoothing(const vtkSHYXShapeSmoothing&) = delete;
  void operator=(const vtkSHYXShapeSmoothing&)        = delete;
};

#endif
