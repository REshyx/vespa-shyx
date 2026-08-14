/**
 * @class   vtkVESPAAttributeTransfer
 * @brief   VTK-only point/cell attribute mapping after remesh or deformation.
 *
 * Used by CGAL and non-CGAL filters. Not automatic: call Interpolate or Copy from RequestData.
 *
 * Interpolate: probe input point data onto output points (vtkProbeFilter); map cell data from the
 * input cell nearest each output cell centroid (vtkStaticCellLocator). Copy: shallow-copy point
 * and cell arrays (same connectivity).
 */

#ifndef vtkVESPAAttributeTransfer_h
#define vtkVESPAAttributeTransfer_h

#include "vtkVESPACoreModule.h"
#include "vtkObject.h"

class vtkPolyData;

class VTKVESPACORE_EXPORT vtkVESPAAttributeTransfer : public vtkObject
{
public:
  static vtkVESPAAttributeTransfer* New();
  vtkTypeMacro(vtkVESPAAttributeTransfer, vtkObject);

  /**
   * Probe input point arrays onto \a output points. Map input cell arrays onto output cells by
   * nearest input cell to each output centroid. Safe when output is vertices-only (cell pass no-ops).
   */
  static bool Interpolate(vtkPolyData* input, vtkPolyData* output);

  /** Shallow-copy point and cell data from \a input onto \a output (topology-preserving filters). */
  static bool Copy(vtkPolyData* input, vtkPolyData* output);

protected:
  vtkVESPAAttributeTransfer()           = default;
  ~vtkVESPAAttributeTransfer() override = default;

private:
  vtkVESPAAttributeTransfer(const vtkVESPAAttributeTransfer&) = delete;
  void operator=(const vtkVESPAAttributeTransfer&)            = delete;
};

#endif
