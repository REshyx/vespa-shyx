/**
 * @class   vtkSHYXRepairDegeneracies
 * @brief   CGAL PMP geometric repair: degenerate faces/edges and optional almost-degenerate (cap/needle) removal.
 *
 * Uses @c CGAL::Polygon_mesh_processing::remove_degenerate_faces() and optionally
 * @c CGAL::Polygon_mesh_processing::remove_almost_degenerate_faces() from
 * @c CGAL/Polygon_mesh_processing/repair_degeneracies.h . Input must be a pure triangle mesh.
 *
 * - Output port 0: repaired vtkPolyData (same as before).
 * - Output port 1: vtkPolyData of VTK_TRIANGLE cells marking regions CGAL targets: exact degenerate
 *   faces on the input (cell data @c SHYX_RepairRegion = 1), and if @c RemoveAlmostDegenerate is on,
 *   needle/cap faces on the mesh after @c remove_degenerate_faces and before @c remove_almost_degenerate_faces
 *   (2 = needle, 3 = cap, 4 = both).
 *
 * @sa https://doc.cgal.org/latest/Polygon_mesh_processing/group__PMP__geometric__repair__grp.html
 */

#ifndef vtkSHYXRepairDegeneracies_h
#define vtkSHYXRepairDegeneracies_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXRepairDegeneraciesModule.h"

class VTKSHYXREPAIRDEGENERACIES_EXPORT vtkSHYXRepairDegeneracies : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXRepairDegeneracies* New();
  vtkTypeMacro(vtkSHYXRepairDegeneracies, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /** If on (default), run remove_almost_degenerate_faces after remove_degenerate_faces. */
  vtkGetMacro(RemoveAlmostDegenerate, bool);
  vtkSetMacro(RemoveAlmostDegenerate, bool);
  vtkBooleanMacro(RemoveAlmostDegenerate, bool);
  //@}

  //@{
  /** Cosine bound for cap detection; CGAL default corresponds to 160 degrees (-0.939692621). */
  vtkGetMacro(CapThreshold, double);
  vtkSetMacro(CapThreshold, double);
  //@}

  //@{
  /** Longest/shortest edge ratio above which a triangle is a needle; CGAL default 4. */
  vtkGetMacro(NeedleThreshold, double);
  vtkSetMacro(NeedleThreshold, double);
  //@}

  //@{
  /** If non-zero, skip collapsing edges longer than this length. CGAL default 0 (disabled). */
  vtkGetMacro(CollapseLengthThreshold, double);
  vtkSetMacro(CollapseLengthThreshold, double);
  //@}

  //@{
  /** If non-zero, skip flips when triangle height exceeds this threshold. CGAL default 0 (disabled). */
  vtkGetMacro(FlipTriangleHeightThreshold, double);
  vtkSetMacro(FlipTriangleHeightThreshold, double);
  //@}

  //@{
  /** Passed to remove_degenerate_edges inside remove_degenerate_faces; CGAL default is true. */
  vtkGetMacro(PreserveGenus, bool);
  vtkSetMacro(PreserveGenus, bool);
  vtkBooleanMacro(PreserveGenus, bool);
  //@}

protected:
  vtkSHYXRepairDegeneracies();
  ~vtkSHYXRepairDegeneracies() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  bool RemoveAlmostDegenerate = true;
  double CapThreshold                 = -0.939692621; // cos(160°), CGAL default
  double NeedleThreshold              = 4.0;
  double CollapseLengthThreshold      = 0.0;
  double FlipTriangleHeightThreshold  = 0.0;
  bool PreserveGenus                  = true;

private:
  vtkSHYXRepairDegeneracies(const vtkSHYXRepairDegeneracies&) = delete;
  void operator=(const vtkSHYXRepairDegeneracies&) = delete;
};

#endif
