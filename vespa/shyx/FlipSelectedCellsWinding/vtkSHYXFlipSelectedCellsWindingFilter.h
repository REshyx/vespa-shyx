/**
 * @class   vtkSHYXFlipSelectedCellsWindingFilter
 * @brief   Reverse the order of point ids for each selected cell in vtkPolyData (flip winding / face normal).
 *
 * Port 0 is the mesh. Port 1 is optional vtkSelection (ParaView Selection port). vtkPolyData::ReverseCell
 * is applied to each selected cell. Optional SelectionCellArrayName on port 0 marks cells when
 * Selection resolves to nothing (scalar &gt; 0.5 or integral non-zero). If there is still no cell
 * to flip (no selection, empty mask, etc.), every cell is reversed.
 */

#ifndef vtkSHYXFlipSelectedCellsWindingFilter_h
#define vtkSHYXFlipSelectedCellsWindingFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXFlipSelectedCellsWindingFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXFLIPSELECTEDCELLSWINDINGFILTER_EXPORT vtkSHYXFlipSelectedCellsWindingFilter
  : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXFlipSelectedCellsWindingFilter* New();
  vtkTypeMacro(vtkSHYXFlipSelectedCellsWindingFilter, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Selection input (port 1). */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * Optional cell data array on port 0. When the Selection port yields no cells, cells with
   * first component &gt; 0.5 (float) or != 0 (integral) are flipped. If still none, all cells flip.
   */
  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

protected:
  vtkSHYXFlipSelectedCellsWindingFilter();
  ~vtkSHYXFlipSelectedCellsWindingFilter() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  char* SelectionCellArrayName = nullptr;

private:
  vtkSHYXFlipSelectedCellsWindingFilter(const vtkSHYXFlipSelectedCellsWindingFilter&) = delete;
  void operator=(const vtkSHYXFlipSelectedCellsWindingFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
