/**
 * @class   vtkSHYXSelectionAppendPatches
 * @brief   Extract named, marked patches from a parent mesh into a vtkPartitionedDataSetCollection.
 *
 * Each stored row is an independent ExtractSelection: overlaps between patches are allowed, unused
 * parent cells are not appended, and patches do not share points or GlobalIds. Apply rebuilds the
 * collection from PatchNames / PatchMarks / PatchCellIds. The optional Selection port is used only
 * when those lists are empty (single Part_0).
 */

#ifndef vtkSHYXSelectionAppendPatches_h
#define vtkSHYXSelectionAppendPatches_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXSelectionAppendPatchesModule.h"

VTK_ABI_NAMESPACE_BEGIN
class vtkAlgorithmOutput;

class VTKSHYXSELECTIONAPPENDPATCHES_EXPORT vtkSHYXSelectionAppendPatches : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXSelectionAppendPatches* New();
  vtkTypeMacro(vtkSHYXSelectionAppendPatches, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Selection input (port 1), same pattern as SHYX Selection Extrude. */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * Newline-separated patch names (Partitioned block names panel order). Empty lines become
   * Part_N. Duplicate names are allowed.
   */
  vtkSetStringMacro(PatchNames);
  vtkGetStringMacro(PatchNames);

  /**
   * Newline-separated mark values aligned with PatchNames. Written as a constant cell (and field)
   * array named by MarkArrayName on every cell of that patch.
   */
  vtkSetStringMacro(PatchMarks);
  vtkGetStringMacro(PatchMarks);

  /**
   * Newline-separated cell-id lists aligned with PatchNames. Each line is comma-separated ids
   * and/or inclusive ranges (e.g. 0-10,15). Ids are indices on the current Input, not GlobalIds.
   */
  vtkSetStringMacro(PatchCellIds);
  vtkGetStringMacro(PatchCellIds);

  /** Cell/field array name for the per-patch constant mark (default PatchMark). */
  vtkSetStringMacro(MarkArrayName);
  vtkGetStringMacro(MarkArrayName);

protected:
  vtkSHYXSelectionAppendPatches();
  ~vtkSHYXSelectionAppendPatches() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  char* PatchNames = nullptr;
  char* PatchMarks = nullptr;
  char* PatchCellIds = nullptr;
  char* MarkArrayName = nullptr;

private:
  vtkSHYXSelectionAppendPatches(const vtkSHYXSelectionAppendPatches&) = delete;
  void operator=(const vtkSHYXSelectionAppendPatches&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
