/**
 * @class   vtkSHYXExtractSelectedCellsFilter
 * @brief   Keep cells included in a vtkSelection (opposite of vtkSHYXDeleteSelectedCellsFilter).
 *
 * Unlike ParaView Extract Selection (vtkExtractSelection), the output is not always
 * vtkUnstructuredGrid. Port 0 is any vtkDataSet; port 1 is optional vtkSelection.
 * If the input is vtkPolyData, or every selected cell is a vtkPolyData cell type
 * (vertex / line / polygon / strip), the output is vtkPolyData. Otherwise the output
 * is vtkUnstructuredGrid. Same selection resolution as SHYX Delete / Extrude
 * (vtkExtractSelection: vtkOriginalCellIds, or vtkOriginalPointIds with incident cells).
 * InvertSelection (off by default) keeps the complement instead. Empty Selection may use
 * SelectionCellArrayName on port 0 (scalar &gt; 0.5 or integral non-zero). Empty selection
 * (after invert) yields an empty output of the decided type.
 */

#ifndef vtkSHYXExtractSelectedCellsFilter_h
#define vtkSHYXExtractSelectedCellsFilter_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXExtractSelectedCellsFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXEXTRACTSELECTEDCELLSFILTER_EXPORT vtkSHYXExtractSelectedCellsFilter
  : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXExtractSelectedCellsFilter* New();
  vtkTypeMacro(vtkSHYXExtractSelectedCellsFilter, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Selection input (port 1), same pattern as vtkSHYXDeleteSelectedCellsFilter. */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * When port 1 has no usable selection: name of a cell data array on port 0. A cell is kept
   * if the first component is &gt; 0.5 (float) or != 0 (integral).
   */
  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

  /** When on, keep unselected cells (complement). Off by default. */
  vtkSetMacro(InvertSelection, int);
  vtkGetMacro(InvertSelection, int);
  vtkBooleanMacro(InvertSelection, int);

protected:
  vtkSHYXExtractSelectedCellsFilter();
  ~vtkSHYXExtractSelectedCellsFilter() override;

  int RequestDataObject(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  char* SelectionCellArrayName = nullptr;
  int InvertSelection = 0;

private:
  vtkSHYXExtractSelectedCellsFilter(const vtkSHYXExtractSelectedCellsFilter&) = delete;
  void operator=(const vtkSHYXExtractSelectedCellsFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
