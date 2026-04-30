/**
 * @class   vtkSHYXEdgeCollapse
 * @brief   Triangulated surface mesh simplification via CGAL Surface_mesh_simplification::edge_collapse.
 *
 * Wraps CGAL::Surface_mesh_simplification::edge_collapse with Edge_count_ratio_stop_predicate:
 * simplification stops when (current edge count) / (initial edge count) drops below EdgeCountRatio.
 * Input must be a pure triangle mesh (use vtkTriangleFilter upstream if needed).
 *
 * @sa CGAL::Surface_mesh_simplification::edge_collapse
 */

#ifndef vtkSHYXEdgeCollapse_h
#define vtkSHYXEdgeCollapse_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXEdgeCollapseModule.h"

class VTKSHYXEDGECOLLAPSE_EXPORT vtkSHYXEdgeCollapse : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXEdgeCollapse* New();
  vtkTypeMacro(vtkSHYXEdgeCollapse, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Stop when the fraction of remaining undirected edges (relative to the initial count) falls
   * strictly below this value. CGAL Edge_count_ratio_stop_predicate; typical range (0, 1],
   * default 0.5 (stop when current/initial edge ratio drops below 0.5).
   */
  vtkGetMacro(EdgeCountRatio, double);
  vtkSetMacro(EdgeCountRatio, double);
  //@}

  //@{
  /**
   * Cost / placement strategy passed to CGAL::Surface_mesh_simplification::edge_collapse:
   * 0 = Lindstrom-Turk (default, memoryless),
   * 1 = Garland-Heckbert plane quadrics (QEM; cost+placement from GarlandHeckbert_plane_policies),
   * 2 = Edge_length_cost with Midpoint_placement (fast, less accurate).
   */
  vtkGetMacro(CostStrategy, int);
  vtkSetClampMacro(CostStrategy, int, 0, 2);
  //@}

protected:
  vtkSHYXEdgeCollapse() = default;
  ~vtkSHYXEdgeCollapse() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  /** Default 0.5 — Edge_count_ratio_stop_predicate threshold. */
  double EdgeCountRatio = 0.5;

  /** See SetCostStrategy; 0 = LindstromTurk. */
  int CostStrategy = 0;

private:
  vtkSHYXEdgeCollapse(const vtkSHYXEdgeCollapse&) = delete;
  void operator=(const vtkSHYXEdgeCollapse&) = delete;
};

#endif
