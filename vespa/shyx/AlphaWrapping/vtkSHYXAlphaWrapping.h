/**
 * @class   vtkSHYXAlphaWrapping
 * @brief   CGAL alpha wrap with Alpha/Offset as mesh-scale absolute lengths.
 *
 * Same backend as vtkCGALAlphaWrapping (point cloud or triangle soup -> watertight
 * 2-manifold that strictly encloses the input). Unlike VESPA Alpha Wrapping, Alpha
 * and Offset are always in input length units. A non-positive value is replaced by
 * a fraction of the longest AABB side (Alpha 5%, Offset 3%), matching the ParaView
 * BoundsDomain scaled_extent suggestions on those properties.
 *
 * @sa vtkCGALAlphaWrapping
 */

#ifndef vtkSHYXAlphaWrapping_h
#define vtkSHYXAlphaWrapping_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXAlphaWrappingModule.h"

class VTKSHYXALPHAWRAPPING_EXPORT vtkSHYXAlphaWrapping : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXAlphaWrapping* New();
  vtkTypeMacro(vtkSHYXAlphaWrapping, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Maximum circumradius of faces on the wrap (absolute length).
   * <= 0 means 0.05 times the longest AABB side of the input.
   */
  vtkGetMacro(Alpha, double);
  vtkSetMacro(Alpha, double);
  //@}

  //@{
  /**
   * Dilatation of the wrap relative to the input (absolute length, must stay > 0
   * after auto-resolve). <= 0 means 0.03 times the longest AABB side.
   */
  vtkGetMacro(Offset, double);
  vtkSetMacro(Offset, double);
  //@}

protected:
  vtkSHYXAlphaWrapping() = default;
  ~vtkSHYXAlphaWrapping() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double Alpha = 0.0;
  double Offset = 0.0;

private:
  vtkSHYXAlphaWrapping(const vtkSHYXAlphaWrapping&) = delete;
  void operator=(const vtkSHYXAlphaWrapping&) = delete;
};

#endif
