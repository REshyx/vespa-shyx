/**
 * @class   vtkSHYXSelectionAppendPatches
 * @brief   Extract named, marked patches from a parent mesh into a vtkPartitionedDataSetCollection.
 *
 * Each stored row is an ExtractSelection. The table keeps duplicate names as separate rows. Apply
 * merges rows that share a name into one output patch (union of cell ids) and keeps the mark of the
 * first occurrence of that name. Unique names are marked 0, 1, 2, ... in table order. Unused parent
 * cells are not appended; patches do not share points or GlobalIds. Apply rebuilds the collection
 * from PatchNames / PatchCellIds. The optional Selection port is used only when those lists are
 * empty (single geo_0).
 *
 * Output port 0 is the added patches (vtkPartitionedDataSetCollection). Output port 1 is the Input
 * minus the union of all added cell ids (same concrete type as Input when possible).
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
   * Newline-separated patch names (table order). Empty lines become geo_N. Duplicate names are
   * merged into one output patch and reuse the earlier mark. Unique names are marked 0, 1, 2, ...
   * in first-seen order. Names need not end with _N.
   */
  vtkSetStringMacro(PatchNames);
  vtkGetStringMacro(PatchNames);

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
  int RequestDataObject(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  char* PatchNames = nullptr;
  char* PatchCellIds = nullptr;
  char* MarkArrayName = nullptr;

private:
  vtkSHYXSelectionAppendPatches(const vtkSHYXSelectionAppendPatches&) = delete;
  void operator=(const vtkSHYXSelectionAppendPatches&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
