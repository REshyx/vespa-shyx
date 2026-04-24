/**
 * @class   vtkSHYXSelectionFillAlphaReunionFilter
 * @brief   Split mesh by selection; hole-fill the rest; hole-fill + Alpha Wrapping the selection;
 *          then CGAL boolean union the two parts.
 *
 * @sa vtkSHYXHoleFillFilter, vtkCGALAlphaWrapping, vtkSHYXBooleanOperationFilter
 */

#ifndef vtkSHYXSelectionFillAlphaReunionFilter_h
#define vtkSHYXSelectionFillAlphaReunionFilter_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXSelectionFillAlphaReunionFilterModule.h"

class VTKSHYXSELECTIONFILLALPHAREUNIONFILTER_EXPORT vtkSHYXSelectionFillAlphaReunionFilter
  : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXSelectionFillAlphaReunionFilter* New();
  vtkTypeMacro(vtkSHYXSelectionFillAlphaReunionFilter, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Port 1: optional vtkSelection (same pattern as other SHYX selection filters). */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * When port 1 has no usable selection: name of a cell data array on port 0. A cell is selected
   * if the first component is &gt; 0.5 (float) or != 0 (integral), same as vtkSHYXDeleteSelectedCellsFilter.
   */
  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

  /** Passthrough to vtkSHYXHoleFillFilter (both branches). */
  vtkGetMacro(FairingContinuity, int);
  vtkSetClampMacro(FairingContinuity, int, 0, 2);

  /** Parameters for CGAL Alpha Wrapping on the selected part (see vtkCGALAlphaWrapping). */
  vtkGetMacro(AbsoluteThresholds, bool);
  vtkSetMacro(AbsoluteThresholds, bool);
  vtkBooleanMacro(AbsoluteThresholds, bool);

  vtkGetMacro(Alpha, double);
  vtkSetMacro(Alpha, double);

  vtkGetMacro(Offset, double);
  vtkSetMacro(Offset, double);

  /** If true, only hole-fill the selected part (skip Alpha Wrapping). */
  vtkGetMacro(SkipAlphaWrapping, bool);
  vtkSetMacro(SkipAlphaWrapping, bool);
  vtkBooleanMacro(SkipAlphaWrapping, bool);

  /** Passthrough to vtkSHYXBooleanOperationFilter (union). */
  vtkGetMacro(ThrowOnSelfIntersection, bool);
  vtkSetMacro(ThrowOnSelfIntersection, bool);
  vtkBooleanMacro(ThrowOnSelfIntersection, bool);

  vtkGetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkSetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkBooleanMacro(OrientToBoundVolumeWhenNeeded, bool);

protected:
  vtkSHYXSelectionFillAlphaReunionFilter();
  ~vtkSHYXSelectionFillAlphaReunionFilter() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  int FairingContinuity = 1;
  bool AbsoluteThresholds = false;
  double Alpha = 1.0;
  double Offset = 0.1;
  bool SkipAlphaWrapping = false;
  bool ThrowOnSelfIntersection = false;
  bool OrientToBoundVolumeWhenNeeded = true;
  char* SelectionCellArrayName = nullptr;

private:
  vtkSHYXSelectionFillAlphaReunionFilter(const vtkSHYXSelectionFillAlphaReunionFilter&) = delete;
  void operator=(const vtkSHYXSelectionFillAlphaReunionFilter&) = delete;
};

#endif
