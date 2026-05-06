/**
 * @class   vtkSHYXDeleteSelectedCellsFilter
 * @brief   Remove cells from vtkDataSet that are included in an incoming vtkSelection (or optional cell array mask).
 *
 * Port 0 is any vtkDataSet; output has the same concrete type as input. Port 1 is optional
 * vtkSelection (ParaView Selection port). Selected cells are dropped from vtkPolyData (unused
 * points removed) and from vtkUnstructuredGrid (vtkExtractCells). For vtkStructuredGrid and
 * vtkExplicitStructuredGrid, selected cells are blanked (BlankCell). For vtkImageData,
 * vtkRectilinearGrid, and other vtkDataSet types, selected cells are marked hidden via the
 * cell ghost array (vtkDataSetAttributes::HIDDENCELL). If Selection is empty, optional
 * SelectionCellArrayName on port 0 selects cells (same rule as SHYX Selection Extrude:
 * scalar &gt; 0.5 or integral non-zero).
 */

#ifndef vtkSHYXDeleteSelectedCellsFilter_h
#define vtkSHYXDeleteSelectedCellsFilter_h

#include "vtkDataSetAlgorithm.h"
#include "vtkSHYXDeleteSelectedCellsFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXDELETESELECTEDCELLSFILTER_EXPORT vtkSHYXDeleteSelectedCellsFilter : public vtkDataSetAlgorithm
{
public:
  static vtkSHYXDeleteSelectedCellsFilter* New();
  vtkTypeMacro(vtkSHYXDeleteSelectedCellsFilter, vtkDataSetAlgorithm);
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
