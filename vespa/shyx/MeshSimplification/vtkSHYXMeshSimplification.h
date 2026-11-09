/**
 * @class   vtkSHYXMeshSimplification
 * @brief   Triangle mesh simplification via one CGAL edge_collapse pass.
 *
 * Algorithm selects the cost and placement:
 * - Lindstrom-Turk (relocates; volume / boundary / shape weights),
 * - Garland-Heckbert plane, triangle, or probabilistic-triangle quadrics,
 * - edge-length cost with midpoint placement,
 * - endpoint subset (survivor is an original endpoint; plane QEM or min-angle ranking).
 *
 * Stop criterion, boundary edges, and sharp edges apply to every algorithm.
 * A vertex incident to a constrained edge is not moved. If both endpoints are,
 * that collapse is refused. Constrained edges themselves are not collapsed.
 *
 * Input must be a pure triangle mesh (use vtkTriangleFilter upstream if needed).
 *
 * @sa vtkSHYXEdgeCollapse vtkSHYXSubsetCoarsen
 *     CGAL::Surface_mesh_simplification::edge_collapse
 */

#ifndef vtkSHYXMeshSimplification_h
#define vtkSHYXMeshSimplification_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXMeshSimplificationModule.h"

class VTKSHYXMESHSIMPLIFICATION_EXPORT vtkSHYXMeshSimplification : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXMeshSimplification* New();
  vtkTypeMacro(vtkSHYXMeshSimplification, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum AlgorithmType
  {
    LINDSTROM_TURK = 0,
    GARLAND_HECKBERT_PLANE = 1,
    GARLAND_HECKBERT_TRIANGLE = 2,
    GARLAND_HECKBERT_PROB_TRIANGLE = 3,
    EDGE_LENGTH_MIDPOINT = 4,
    ENDPOINT_SUBSET = 5
  };

  enum StopCriterionType
  {
    STOP_EDGE_COUNT_RATIO = 0,
    STOP_EDGE_COUNT = 1,
    STOP_EDGE_LENGTH = 2
  };

  enum EndpointRankingType
  {
    ENDPOINT_PLANE_QEM = 0,
    ENDPOINT_MIN_ANGLE = 1
  };

  //@{
  /** Cost / placement. Default Lindstrom-Turk. */
  vtkGetMacro(Algorithm, int);
  vtkSetClampMacro(Algorithm, int, 0, 5);
  //@}

  //@{
  /**
   * 0 = edge-count ratio, 1 = remaining undirected edge count, 2 = edge-length threshold.
   * Default edge-count ratio.
   */
  vtkGetMacro(StopCriterion, int);
  vtkSetClampMacro(StopCriterion, int, 0, 2);
  //@}

  //@{
  /**
   * Stop when (current undirected edges) / (initial undirected edges) falls strictly below this
   * value. Used when StopCriterion is STOP_EDGE_COUNT_RATIO. Default 0.5.
   */
  vtkGetMacro(EdgeCountRatio, double);
  vtkSetMacro(EdgeCountRatio, double);
  //@}

  //@{
  /**
   * Stop when the undirected edge count falls strictly below this value.
   * Used when StopCriterion is STOP_EDGE_COUNT. Default 1000.
   */
  vtkGetMacro(EdgeCount, int);
  vtkSetMacro(EdgeCount, int);
  //@}

  //@{
  /**
   * Absolute length. The collapse sequence stops when the next edge is strictly longer than this.
   * Used when StopCriterion is STOP_EDGE_LENGTH. Default 1.
   */
  vtkGetMacro(EdgeLength, double);
  vtkSetMacro(EdgeLength, double);
  //@}

  //@{
  /** When true (default), border edges are constrained and are not collapsed. */
  vtkGetMacro(PreserveBoundary, bool);
  vtkSetMacro(PreserveBoundary, bool);
  vtkBooleanMacro(PreserveBoundary, bool);
  //@}

  //@{
  /** When true, detect_sharp_edges(ProtectAngle) marks feature edges as constrained. Default false. */
  vtkGetMacro(DetectFeatureEdges, bool);
  vtkSetMacro(DetectFeatureEdges, bool);
  vtkBooleanMacro(DetectFeatureEdges, bool);
  //@}

  //@{
  /** Dihedral angle in degrees for detect_sharp_edges. Default 70. */
  vtkGetMacro(ProtectAngle, double);
  vtkSetMacro(ProtectAngle, double);
  //@}

  //@{
  /**
   * Wrap placement in Bounded_normal_change_placement. When true (default), a collapse that would
   * reverse a neighboring triangle is refused.
   */
  vtkGetMacro(PreventNormalFlip, bool);
  vtkSetMacro(PreventNormalFlip, bool);
  vtkBooleanMacro(PreventNormalFlip, bool);
  //@}

  //@{
  /** Lindstrom-Turk volume weight. CGAL default 0.5. Used only by LINDSTROM_TURK. */
  vtkGetMacro(VolumeWeight, double);
  vtkSetMacro(VolumeWeight, double);
  //@}

  //@{
  /** Lindstrom-Turk boundary weight. CGAL default 0.5. Used only by LINDSTROM_TURK. */
  vtkGetMacro(BoundaryWeight, double);
  vtkSetMacro(BoundaryWeight, double);
  //@}

  //@{
  /** Lindstrom-Turk shape weight. CGAL default 0. Used only by LINDSTROM_TURK. */
  vtkGetMacro(ShapeWeight, double);
  vtkSetMacro(ShapeWeight, double);
  //@}

  //@{
  /**
   * Garland-Heckbert discontinuity multiplier passed to the quadric policy constructor.
   * CGAL default 100. Used by the three quadric algorithms.
   */
  vtkGetMacro(DiscontinuityMultiplier, double);
  vtkSetMacro(DiscontinuityMultiplier, double);
  //@}

  //@{
  /**
   * Endpoint-subset ranking. 0 = plane QEM at the two endpoints, 1 = post-collapse minimum angle.
   * Placement is always an original endpoint. Default plane QEM.
   */
  vtkGetMacro(EndpointRanking, int);
  vtkSetClampMacro(EndpointRanking, int, 0, 1);
  //@}

protected:
  vtkSHYXMeshSimplification() = default;
  ~vtkSHYXMeshSimplification() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int Algorithm = LINDSTROM_TURK;
  int StopCriterion = STOP_EDGE_COUNT_RATIO;
  double EdgeCountRatio = 0.5;
  int EdgeCount = 1000;
  double EdgeLength = 1.0;
  bool PreserveBoundary = true;
  bool DetectFeatureEdges = false;
  double ProtectAngle = 70.0;
  bool PreventNormalFlip = true;
  double VolumeWeight = 0.5;
  double BoundaryWeight = 0.5;
  double ShapeWeight = 0.0;
  double DiscontinuityMultiplier = 100.0;
  int EndpointRanking = ENDPOINT_PLANE_QEM;

private:
  vtkSHYXMeshSimplification(const vtkSHYXMeshSimplification&) = delete;
  void operator=(const vtkSHYXMeshSimplification&) = delete;
};

#endif
