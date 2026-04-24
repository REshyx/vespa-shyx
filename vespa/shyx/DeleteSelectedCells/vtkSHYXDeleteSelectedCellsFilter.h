/**
 * @class   vtkSHYXDeleteSelectedCellsFilter
 * @brief   Remove cells from vtkPolyData that are included in an incoming vtkSelection.
 *
 * Port 0 is the mesh. Port 1 is optional vtkSelection (ParaView Selection port). Selected cells are
 * dropped from the output; remaining connectivity is compacted and unused points removed.
 * If Selection is empty, optional SelectionCellArrayName on port 0 selects cells (same rule as
 * SHYX Selection Extrude: scalar &gt; 0.5 or integral non-zero).
 */

#ifndef vtkSHYXDeleteSelectedCellsFilter_h
#define vtkSHYXDeleteSelectedCellsFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXDeleteSelectedCellsFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXDELETESELECTEDCELLSFILTER_EXPORT vtkSHYXDeleteSelectedCellsFilter : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXDeleteSelectedCellsFilter* New();
  vtkTypeMacro(vtkSHYXDeleteSelectedCellsFilter, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Selection input (port 1), same pattern as vtkSHYXSelectionExtrudeFilter. */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * When port 1 has no usable selection: name of a cell data array on port 0. A cell is removed
   * if the first component is &gt; 0.5 (float) or != 0 (integral).
   */
  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

protected:
  vtkSHYXDeleteSelectedCellsFilter();
  ~vtkSHYXDeleteSelectedCellsFilter() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  char* SelectionCellArrayName = nullptr;

private:
  vtkSHYXDeleteSelectedCellsFilter(const vtkSHYXDeleteSelectedCellsFilter&) = delete;
  void operator=(const vtkSHYXDeleteSelectedCellsFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
