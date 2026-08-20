/**
 * @class   vtkSHYXPartitionedCollectionToOpenFOAM
 * @brief   Write an OpenFOAM case from an IOSS-style volume + side-set PDC.
 *
 * Requires a volume vtkUnstructuredGrid (element_blocks). Side sets become
 * polyMesh patches via volume-point GlobalIds (primary) and element_side
 * (fallback). Node sets are ignored. Output is vtkOpenFOAMReader's
 * vtkMultiBlockDataSet (internalMesh + patches).
 */

#ifndef vtkSHYXPartitionedCollectionToOpenFOAM_h
#define vtkSHYXPartitionedCollectionToOpenFOAM_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXPartitionedCollectionToOpenFOAMModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXPARTITIONEDCOLLECTIONTOOPENFOAM_EXPORT vtkSHYXPartitionedCollectionToOpenFOAM
  : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXPartitionedCollectionToOpenFOAM* New();
  vtkTypeMacro(vtkSHYXPartitionedCollectionToOpenFOAM, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Root of the OpenFOAM case (writes constant/polyMesh and case.foam). */
  vtkSetStringMacro(CaseDirectory);
  vtkGetStringMacro(CaseDirectory);

  /**
   * When non-zero, leftover volume boundary faces (not claimed by any side)
   * are written as DefaultFacesName (type wall). Default 0: fail instead.
   */
  vtkSetMacro(AllowDefaultFaces, int);
  vtkGetMacro(AllowDefaultFaces, int);
  vtkBooleanMacro(AllowDefaultFaces, int);

  vtkSetStringMacro(DefaultFacesName);
  vtkGetStringMacro(DefaultFacesName);

protected:
  vtkSHYXPartitionedCollectionToOpenFOAM();
  ~vtkSHYXPartitionedCollectionToOpenFOAM() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestDataObject(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  char* CaseDirectory = nullptr;
  char* DefaultFacesName = nullptr;
  int AllowDefaultFaces = 0;

private:
  vtkSHYXPartitionedCollectionToOpenFOAM(const vtkSHYXPartitionedCollectionToOpenFOAM&) = delete;
  void operator=(const vtkSHYXPartitionedCollectionToOpenFOAM&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
