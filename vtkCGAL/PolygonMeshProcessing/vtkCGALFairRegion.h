/**
 * @class   vtkCGALFairRegion
 * @brief   remesh using the CGAL isotropic remeshing
 *
 * vtkCGALFairRegion is a filter allowing fair blind-holes on
 * a triangulated polydata using the CGAL `fair` method.
 */

#ifndef vtkCGALfairRegion_h
#define vtkCGALfairRegion_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALFairRegion : public vtkPolyDataAlgorithm
{
public:
  static vtkCGALFairRegion* New();
  vtkTypeMacro(vtkCGALFairRegion, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Choose if the result mesh should have the
   * point / cell data attributes of the input.
   * If so, a vtkProbeFilter is called in order
   * to interpolate values to the new mesh.
   * Default is true
   **/
   vtkGetMacro(UpdateAttributes, bool);
   vtkSetMacro(UpdateAttributes, bool);
   vtkBooleanMacro(UpdateAttributes, bool)
  //@}

protected:
  vtkCGALFairRegion();
  ~vtkCGALFairRegion() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  // Fields

  bool   UpdateAttributes = true;

private:
  vtkCGALFairRegion(const vtkCGALFairRegion&) = delete;
  void operator=(const vtkCGALFairRegion&) = delete;
};

#endif
