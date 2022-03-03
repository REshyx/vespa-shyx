/**
 * @class   vtkCGALRegionFairing
 * @brief   fair region using the CGAL fair method
 *
 * vtkCGALRegionFairing is a filter allowing fair blind-holes on
 * a triangulated polydata using the CGAL `fair` method.
 */

#ifndef vtkCGALRegionFairing_h
#define vtkCGALRegionFairing_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALRegionFairing : public vtkPolyDataAlgorithm
{
public:
  static vtkCGALRegionFairing* New();
  vtkTypeMacro(vtkCGALRegionFairing, vtkPolyDataAlgorithm);
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

    protected : vtkCGALRegionFairing();
  ~vtkCGALRegionFairing() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  // Fields

  bool UpdateAttributes = true;

private:
  vtkCGALRegionFairing(const vtkCGALRegionFairing&) = delete;
  void operator=(const vtkCGALRegionFairing&) = delete;
};

#endif
