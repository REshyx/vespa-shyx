/**
 * @class   vtkSHYXMeshChecker
 * @brief   Diagnose polygon soup / surface mesh: non-manifold edges, boundary cycles, self-intersections;
 *          and optionally output a repaired surface.
 *
 * - Output port 0: geometry after combinatorial soup repair (orient + repair_polygon_soup) and, when
 *   applicable, CGAL self-intersection repair on triangle meshes using the same PMP entry points as
 *   vtkSHYXAutorefineSelfIntersectionFilter (CGAL 6+: autorefine; CGAL 5: experimental
 *   autorefine_and_remove_self_intersections with optional preserve_genus).
 * - Output port 1: diagnostic vtkPolyData mixing VTK_LINE, VTK_POLY_LINE, and VTK_TRIANGLE cells. Each
 *   cell has cell data array SHYX_CheckReason: 1 = soup edge with >2 incident faces, 2 = boundary
 *   (hole) cycle, 3 = triangle participating in a self-intersection (CGAL PMP::self_intersections).
 */

#ifndef vtkSHYXMeshChecker_h
#define vtkSHYXMeshChecker_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXMeshCheckerModule.h"

class VTKSHYXMESHCHECKER_EXPORT vtkSHYXMeshChecker : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXMeshChecker* New();
  vtkTypeMacro(vtkSHYXMeshChecker, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkGetMacro(CheckSoupEdges, bool);
  vtkSetMacro(CheckSoupEdges, bool);
  vtkBooleanMacro(CheckSoupEdges, bool);

  vtkGetMacro(CheckBoundary, bool);
  vtkSetMacro(CheckBoundary, bool);
  vtkBooleanMacro(CheckBoundary, bool);

  vtkGetMacro(CheckSelfIntersection, bool);
  vtkSetMacro(CheckSelfIntersection, bool);
  vtkBooleanMacro(CheckSelfIntersection, bool);

  //@{
  /**
   * If on (default), call CGAL orient_polygon_soup() before the combinatorial mesh test so face
   * winding is unified when possible.
   */
  vtkGetMacro(CheckOrient, bool);
  vtkSetMacro(CheckOrient, bool);
  vtkBooleanMacro(CheckOrient, bool);
  //@}

  //@{
  /**
   * If on (default), call CGAL repair_polygon_soup() after optional orient (splits non-manifold
   * vertices / repairs soup so polygon_soup_to_polygon_mesh is more likely to succeed).
   */
  vtkGetMacro(AttemptOrientRepair, bool);
  vtkSetMacro(AttemptOrientRepair, bool);
  vtkBooleanMacro(AttemptOrientRepair, bool);
  //@}

  //@{
  /**
   * If on, and the mesh is a pure triangle surface built from the soup, run CGAL autorefine for
   * self-intersections when OnlyIfSelfIntersecting is off or when CGAL reports intersections (see
   * vtkSHYXAutorefineSelfIntersectionFilter). Default: off.
   */
  vtkGetMacro(AttemptRepairSelfIntersections, bool);
  vtkSetMacro(AttemptRepairSelfIntersections, bool);
  vtkBooleanMacro(AttemptRepairSelfIntersections, bool);
  //@}

  //@{
  /**
   * When on (default), skip autorefine unless CGAL detects self-intersections (does_self_intersect or
   * non-empty self_intersections face pairs on the triangle mesh).
   */
  vtkGetMacro(OnlyIfSelfIntersecting, bool);
  vtkSetMacro(OnlyIfSelfIntersecting, bool);
  vtkBooleanMacro(OnlyIfSelfIntersecting, bool);
  //@}

  //@{
  /**
   * Before CGAL 6: passed as preserve_genus to experimental autorefine. Ignored on CGAL 6+.
   */
  vtkGetMacro(PreserveGenus, bool);
  vtkSetMacro(PreserveGenus, bool);
  vtkBooleanMacro(PreserveGenus, bool);
  //@}

  //@{
  /**
   * If on (default), emit vtkWarningMacro lines prefixed with [SHYXMeshChecker] for each diagnostic
   * step (soup stats, CGAL mesh build, boundary, self-intersection, final cell counts). Turn off to
   * reduce log noise once the pipeline is verified.
   */
  vtkGetMacro(LogSteps, bool);
  vtkSetMacro(LogSteps, bool);
  vtkBooleanMacro(LogSteps, bool);
  //@}

protected:
  vtkSHYXMeshChecker();
  ~vtkSHYXMeshChecker() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  bool CheckSoupEdges         = true;
  bool CheckBoundary          = true;
  bool CheckSelfIntersection  = true;
  bool CheckOrient            = true;
  bool AttemptOrientRepair    = true;
  bool AttemptRepairSelfIntersections = false;
  bool OnlyIfSelfIntersecting         = true;
  bool PreserveGenus                  = false;
  bool LogSteps                       = true;

private:
  vtkSHYXMeshChecker(const vtkSHYXMeshChecker&) = delete;
  void operator=(const vtkSHYXMeshChecker&)     = delete;
};

#endif
