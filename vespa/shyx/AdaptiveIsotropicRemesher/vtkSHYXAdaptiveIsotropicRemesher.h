/**
 * @class   vtkSHYXAdaptiveIsotropicRemesher
 * @brief   Curvature-aware isotropic remesh with min/max edge length, tolerance, and configurable
 *          relaxation steps per iteration.
 *
 * Uses CGAL Polygon_mesh_processing isotropic remeshing with **Vespa
 * FeatureAwareAdaptiveSizingField** always (custom interpolated_corrected_curvatures math with
 * per-vertex caps on dual named CGAL property maps — not CGAL `Adaptive_sizing_field`). After
 * `PrepareIccVertexNormalsForAdaptiveSizing` (CGAL area-weighted `v:vespa_icc_normal`, and dual
 * per-vertex normals when the Feature mask applies), curvature-driven targets mirror CGAL's ICC
 * sizing formula inside min/max bounds, with optional **AdaptiveSizingNeighborMaxRatio** smoothing
 * of adjacent vertex targets (**> 1** reduces sharp spatial sizing jumps). When FeatureSizingStandAlone is OFF, values written to the
 * feature map reuse the global tolerance/min/max so `v:vespa_size_feature` equals
 * `v:vespa_size_global` everywhere.
 *
 * Remesh region: either an optional vtkSelection on port 1 (copied/active selection) or a scalar
 * value-range on the input polydata (point- or cell-centered array; same name resolution as the
 * feature mask). The rest of the surface is unchanged when the region is non-empty.
 * Edges on the boundary between remeshed and untouched faces are also marked as
 * CGAL feature/constrained edges for isotropic_remeshing (together with angle-based sharp edges
 * and optional FeatureMask filtering).
 * With an empty remesh region (no selection, invalid scalar-range setup, or no cells in range),
 * the whole surface is remeshed.
 *
 * Output port 0: remeshed vtkPolyData (point array **VespaIccNonMaskVertexNormal**: ICC non-mask-side
 * vertex normal from `v:vespa_icc_n_nonmask`, or `v:vespa_icc_normal` when no mask dual exists; written
 * after a final `PrepareIcc` on the output using the evaluated feature mask when enabled).
 * Output port 1: **Lines** only (sharp feature edges). Uses the **input** mask region (port 2) when
 * the feature mask is valid and non-empty; otherwise the full remeshed surface (with optional mask
 * filtering on the output). No vertex cells. Empty when Detect feature edges is OFF.
 * Output port 2: input geometry, faces passing the feature mask threshold (for remesh constraints).
 * Output port 3: **Sizing / ICC preview immediately before the final remesh sub-step** —
 * NumberOfIterations==1: same topology as the CGAL input (converted to VTK); NumberOfIterations>1:
 * the mesh after NumberOfIterations-1 single-iteration CGAL passes. `RemeshRecomputeCurvatureEachIteration`
 * matches the main path when enabled (default): curvature and `v:vespa_size_*` are refreshed immediately before
 * this snapshot (and thus before the last iteration), as in the CGAL loop. With NumberOfIterations>1 and
 * Feature mask, port 3 probes mask arrays from the input onto the preview mesh (`UpdateAttributes` ON)
 * and builds the same dual ICC normals as on the final output when evaluation succeeds.
 * (`v:vespa_icc_normal` is not exported.) Arrays: VespaAdaptiveSizeGlobal / VespaAdaptiveSizeFeature
 * from `v:vespa_size_*` (after optional neighbor ratio limiting; AdaptiveSizingNeighborMaxRatio),
 * VespaIccPrincipalCurvatureMin/Max and uncapped sizing from a second ICC pass
 * after preview `PrepareIcc`; feature uncapped uses the mirrored tolerance when FeatureSizingStandAlone is OFF.
 * **VespaIccNonMaskVertexNormal** (3-tuple point vectors) matches `v:vespa_icc_n_nonmask`, or the area blend
 * `v:vespa_icc_normal` when the dual bundle is absent.
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
   * How the remesh face subset is chosen. 0 (default): vtkSelection on port 1 when connected and
   * non-empty. 1: cells on the input whose chosen array's tuple magnitude lies in
   * [RemeshRangeMin, RemeshRangeMax] (RemeshRange* properties); port 1 is ignored for this purpose.
   */
  vtkGetMacro(RemeshRegionMode, int);
  vtkSetClampMacro(RemeshRegionMode, int, 0, 1);
  //@}

  vtkSetStringMacro(RemeshRangeArrayName);
  vtkGetStringMacro(RemeshRangeArrayName);

  //@{
  /**
   * Inclusive scalar range for RemeshRegionMode == 1 (tuple magnitude). Cells outside the
   * interval are not remeshed; requires RemeshRangeMin <= RemeshRangeMax.
   */
  vtkGetMacro(RemeshRangeMin, double);
  vtkSetMacro(RemeshRangeMin, double);
  //@}

  //@{
  vtkGetMacro(RemeshRangeMax, double);
  vtkSetMacro(RemeshRangeMax, double);
  //@}

  //@{
  /**
   * When the chosen remesh-range array is point-centered: if true, every corner of a cell must
   * fall in [RemeshRangeMin, RemeshRangeMax]; if false, one in-range corner suffices. Ignored for
   * cell-centered arrays. Default false.
   */
  vtkGetMacro(RemeshRangeAllScalars, bool);
  vtkSetMacro(RemeshRangeAllScalars, bool);
  vtkBooleanMacro(RemeshRangeAllScalars, bool);
  //@}

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
   * Tolerance passed to Vespa ICC adaptive sizing (global / non-feature edges via
   * `FeatureAwareAdaptiveSizingField`); together with interpolated corrected curvature and Min/Max
   * edge length. Smaller values generally yield finer meshes within the bounds. Must be strictly
   * positive.
   */
  vtkGetMacro(AdaptiveTolerance, double);
  vtkSetMacro(AdaptiveTolerance, double);
  //@}

  //@{
  /**
   * When true, constrained/feature edges dispatch to **`v:vespa_size_feature`** built from
   * FeatureMinEdgeLength / FeatureMaxEdgeLength / FeatureAdaptiveTolerance while other edges use the
   * global sizing map (**`v:vespa_size_global`** from MinEdgeLength / MaxEdgeLength /
   * AdaptiveTolerance). When false, the constructor receives the global tolerances/length bounds for
   * both maps **so vertex targets are identical** on every vertex (dual maps still exist; CGAL
   * remesh only picks one map per edge by feature flag).
   *
   * RemeshProtectConstraints / collapse / relaxation / flip / split options are unchanged.
   * With RemeshProtectConstraints=true, differing feature sizing has little effect on splits/collapses
   * because constrained edges do not subdivide/compress (only tangential relaxation sees `at()`).
   * Default false.
   */
  vtkGetMacro(FeatureSizingStandAlone, bool);
  vtkSetMacro(FeatureSizingStandAlone, bool);
  vtkBooleanMacro(FeatureSizingStandAlone, bool);
  //@}

  //@{
  /**
   * Minimum allowed edge length mapped into **`v:vespa_size_feature`** when FeatureSizingStandAlone
   * is true (must be strictly positive in that mode). When standalone is false this property is ignored
   * and the global MinEdgeLength is reused for the feature map.
   */
  vtkGetMacro(FeatureMinEdgeLength, double);
  vtkSetMacro(FeatureMinEdgeLength, double);
  //@}

  //@{
  /**
   * Maximum allowed edge length for the feature sizing map when FeatureSizingStandAlone is true
   * (must be greater than FeatureMinEdgeLength). Ignored when standalone is false; globals are reused.
   */
  vtkGetMacro(FeatureMaxEdgeLength, double);
  vtkSetMacro(FeatureMaxEdgeLength, double);
  //@}

  //@{
  /**
   * Tolerance for the feature sizing map when FeatureSizingStandAlone is true (strictly positive).
   * When standalone is false AdaptiveTolerance is reused for both maps.
   */
  vtkGetMacro(FeatureAdaptiveTolerance, double);
  vtkSetMacro(FeatureAdaptiveTolerance, double);
  //@}

  //@{
  /**
   * Spatial cap on ICC sizing jumps: after per-vertex targets are clamped to min/max edge length,
   * when **> 1**, relax both `v:vespa_size_global` and `v:vespa_size_feature` so across any mesh edge
   * the ratio of larger to smaller endpoint target does not exceed this value (iterative symmetric
   * lowering of the larger target only). Typical values ~1.2–2 soften abrupt coarsening next to refined
   * regions. **<= 1** disables — behavior matches the raw ICC sizing field only. Default **1.6**;
   * set **0** to disable.
   */
  vtkGetMacro(AdaptiveSizingNeighborMaxRatio, double);
  vtkSetClampMacro(AdaptiveSizingNeighborMaxRatio, double, 0.0, 1.0e6);
  //@}

  //@{
  /**
   * When true (default), CGAL isotropic remeshing performs **one CGAL iteration per pass** and calls
   * FeatureAwareAdaptiveSizingField::recompute_curvature before each subsequent pass so ICC is
   * re-evaluated on the updated mesh (**full surface** ICC domain; see vtkSHYXFeatureAwareAdaptiveSizingField).
   * When false, ICC runs in the sizing-field constructor immediately before multi-iteration remesh,
   * and splits only interpolate neighbor targets (faster when the iteration count is large).
   */
  vtkGetMacro(RemeshRecomputeCurvatureEachIteration, bool);
  vtkSetMacro(RemeshRecomputeCurvatureEachIteration, bool);
  vtkBooleanMacro(RemeshRecomputeCurvatureEachIteration, bool);
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
   * is still curvature-driven Vespa ICC on the remeshed patch. Default true.
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
   * isotropic_remeshing; ProtectAngle, SharpFeatureSideFilter, FeatureMaskEnabled, and related
   * properties take effect.
   * When false, those sources do NOT write feature-edge constraints; remesh sees only
   * vtkSelection-boundary constraints (when a selection input is connected).
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
  bool   FeatureSizingStandAlone   = false;
  double FeatureMinEdgeLength      = 0.0;
  double FeatureMaxEdgeLength      = 0.0;
  double FeatureAdaptiveTolerance  = 0.01;
  double AdaptiveSizingNeighborMaxRatio = 1.6;
  bool   RemeshRecomputeCurvatureEachIteration = true;
  double ProtectAngle        = 70.0;
  int    NumberOfIterations  = 3;
  int    NumberOfRelaxationSteps = 3;
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

  int    RemeshRegionMode        = 0;
  char*  RemeshRangeArrayName    = nullptr;
  double RemeshRangeMin          = 0.0;
  double RemeshRangeMax          = 1.0;
  bool   RemeshRangeAllScalars   = false;

private:
  vtkSHYXAdaptiveIsotropicRemesher(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
  void operator=(const vtkSHYXAdaptiveIsotropicRemesher&) = delete;
};

#endif
