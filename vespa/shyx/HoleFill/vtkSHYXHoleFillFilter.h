/**
 * @class   vtkSHYXHoleFillFilter
 * @brief   Fill boundary holes on a triangle vtkPolyData using CGAL PMP (SHYX menu proxy).
 *
 * Delegates to vtkCGALPatchFilling: optional vtkSelection removes a patch first; otherwise every
 * boundary cycle is filled with CGAL::Polygon_mesh_processing::triangulate_refine_and_fair_hole().
 *
 * @sa vtkCGALPatchFilling
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
   * vtkCGALPatchFilling.
   */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  vtkGetMacro(FairingContinuity, int);
  vtkSetClampMacro(FairingContinuity, int, 0, 2);

protected:
  vtkSHYXHoleFillFilter();
  ~vtkSHYXHoleFillFilter() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  int FairingContinuity = 1;

private:
  vtkSHYXHoleFillFilter(const vtkSHYXHoleFillFilter&) = delete;
  void operator=(const vtkSHYXHoleFillFilter&) = delete;
};

#endif
