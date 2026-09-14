/**
 * @class   vtkSHYXHoleFillFilter
 * @brief   Fill boundary holes on a triangle surface using CGAL PMP (SHYX menu proxy).
 *
 * Accepts any vtkDataSet on port 0; non-PolyData inputs are converted to a surface mesh with
 * vtkGeometryFilter before filling. Optional vtkSelection removes a patch first.
 *
 * By default (RepairPolygonSoup), the filter matches SHYX Mesh Checker: orient_polygon_soup +
 * repair_polygon_soup + polygon_soup_to_polygon_mesh, then
 * CGAL::Polygon_mesh_processing::triangulate_refine_and_fair_hole() on every boundary cycle.
 * Duplicate/near-duplicate vertices on the hole rim otherwise make vtkCGALHelper::toCGAL
 * (Euler add_face) produce a mesh that extract_boundary_cycles cannot fill (silent no-op).
 * Turn RepairPolygonSoup off to skip soup cleanup and delegate entirely to vtkCGALPatchFilling.
 *
 * @sa vtkCGALPatchFilling, vtkSHYXMeshChecker
 */

#ifndef vtkSHYXHoleFillFilter_h
#define vtkSHYXHoleFillFilter_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXHoleFillFilterModule.h"

class VTKSHYXHOLEFILLFILTER_EXPORT vtkSHYXHoleFillFilter : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXHoleFillFilter* New();
  vtkTypeMacro(vtkSHYXHoleFillFilter, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  void SetUpdateAttributes(bool update) override;

  /**
   * Optional selection input (port 1): triangles/points removed before hole filling, same as
   * vtkCGALPatchFilling. Applied before soup repair so original ids remain valid.
   */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  vtkGetMacro(FairingContinuity, int);
  vtkSetClampMacro(FairingContinuity, int, 0, 2);

  ///@{
  /**
   * If on (default), run CGAL orient_polygon_soup + repair_polygon_soup and build a Surface_mesh
   * the same way as vtkSHYXMeshChecker port 0, then fill holes on that mesh. Needed for soups
   * with duplicate vertices that look like a hole but are not a fillable boundary cycle.
   */
  vtkGetMacro(RepairPolygonSoup, bool);
  vtkSetMacro(RepairPolygonSoup, bool);
  vtkBooleanMacro(RepairPolygonSoup, bool);
  ///@}

protected:
  vtkSHYXHoleFillFilter();
  ~vtkSHYXHoleFillFilter() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  int FairingContinuity = 1;
  bool RepairPolygonSoup = true;

private:
  vtkSHYXHoleFillFilter(const vtkSHYXHoleFillFilter&) = delete;
  void operator=(const vtkSHYXHoleFillFilter&) = delete;
};

#endif
