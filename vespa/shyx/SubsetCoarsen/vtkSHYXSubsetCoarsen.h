/**
 * @class   vtkSHYXSubsetCoarsen
 * @brief   Coarsen a triangle surface without inserting vertices or moving survivors.
 *
 * CGAL Surface_mesh_simplification::edge_collapse with endpoint (subset) placement:
 * each collapse keeps one original endpoint and deletes the other. No Steiner points,
 * no midpoint/QEM relocation, no tangential relaxation. Output vertices are a subset
 * of the input vertices at the same coordinates (VTK↔CGAL round-trip notwithstanding).
 *
 * Stop predicate is Edge_count_ratio_stop_predicate (same meaning as vtkSHYXEdgeCollapse).
 * CostStrategy selects how the surviving endpoint is ranked: plane QEM (geometric error)
 * or post-collapse minimum angle (element quality). Placement is always an original
 * endpoint, unless a constrained vertex must be kept.
 *
 * @sa vtkSHYXEdgeCollapse vtkSHYXAdaptiveIsotropicRemesher
 *     CGAL::Surface_mesh_simplification::edge_collapse
 */

#ifndef vtkSHYXSubsetCoarsen_h
#define vtkSHYXSubsetCoarsen_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXSubsetCoarsenModule.h"

class VTKSHYXSUBSETCOARSEN_EXPORT vtkSHYXSubsetCoarsen : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXSubsetCoarsen* New();
  vtkTypeMacro(vtkSHYXSubsetCoarsen, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Stop when (current undirected edges) / (initial undirected edges) falls strictly
   * below this value. CGAL Edge_count_ratio_stop_predicate; typical range (0, 1],
   * default 0.5. Constraints / the link condition may stop earlier at a higher ratio.
   */
  vtkGetMacro(EdgeCountRatio, double);
  vtkSetMacro(EdgeCountRatio, double);
  //@}

  //@{
  /**
   * 0 = plane QEM at the two endpoints (default; geometric error).
   * 1 = minimum angle among triangles remaining after the collapse (element quality).
   * In both cases the surviving vertex is an original endpoint.
   */
  vtkGetMacro(CostStrategy, int);
  vtkSetClampMacro(CostStrategy, int, 0, 1);
  //@}

  //@{
  /**
   * When true (default), every border edge is constrained and is not collapsed, so the
   * boundary polyline is locked. Mixed interior–border edges still collapse onto the
   * border vertex. Turn off to allow deleting border vertices (still a subset of the
   * original vertex set).
   */
  vtkGetMacro(PreserveBoundary, bool);
  vtkSetMacro(PreserveBoundary, bool);
  vtkBooleanMacro(PreserveBoundary, bool);
  //@}

  //@{
  /**
   * When true, CGAL detect_sharp_edges (ProtectAngle) marks feature edges as constrained
   * so they are not collapsed. Default false.
   */
  vtkGetMacro(DetectFeatureEdges, bool);
  vtkSetMacro(DetectFeatureEdges, bool);
  vtkBooleanMacro(DetectFeatureEdges, bool);
  //@}

  //@{
  /**
   * Dihedral angle in degrees for detect_sharp_edges. Used only when DetectFeatureEdges
   * is on. Default 70.
   */
  vtkGetMacro(ProtectAngle, double);
  vtkSetMacro(ProtectAngle, double);
  //@}

  //@{
  /**
   * CGAL Bounded_normal_change_placement: refuse a collapse that would reverse a
   * neighboring triangle's orientation. Default true.
   */
  vtkGetMacro(PreventNormalFlip, bool);
  vtkSetMacro(PreventNormalFlip, bool);
  vtkBooleanMacro(PreventNormalFlip, bool);
  //@}

protected:
  vtkSHYXSubsetCoarsen() = default;
  ~vtkSHYXSubsetCoarsen() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double EdgeCountRatio = 0.5;
  int CostStrategy = 0;
  bool PreserveBoundary = true;
  bool DetectFeatureEdges = false;
  double ProtectAngle = 70.0;
  bool PreventNormalFlip = true;

private:
  vtkSHYXSubsetCoarsen(const vtkSHYXSubsetCoarsen&) = delete;
  void operator=(const vtkSHYXSubsetCoarsen&) = delete;
};

#endif
