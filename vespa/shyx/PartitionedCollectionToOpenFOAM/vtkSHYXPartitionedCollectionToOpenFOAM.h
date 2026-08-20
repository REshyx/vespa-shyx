/**
 * @class   vtkSHYXPartitionedCollectionToOpenFOAM
 * @brief   Write an OpenFOAM case from an IOSS-style volume + side-set PDC.
 *
 * Requires a volume vtkUnstructuredGrid (element_blocks). Side sets become
 * polyMesh patches via volume-point GlobalIds (primary) and element_side
 * (fallback). Node sets are ignored. Output is vtkOpenFOAMReader's
 * vtkMultiBlockDataSet (internalMesh + patches). Also writes system/ (simpleFoam
 * skeleton) and 0/p, 0/U (zeroGradient on every PDC patch name) plus volume
 * PointData/CellData as 0/<array> volFields (boundary values from owner cells).
 */

#ifndef vtkSHYXPartitionedCollectionToOpenFOAM_h
#define vtkSHYXPartitionedCollectionToOpenFOAM_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXPartitionedCollectionToOpenFOAMModule.h"

#include <vtkDataArraySelection.h>
#include <vtkSmartPointer.h>

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
   * Directory actually written this Apply. Empty Case Directory creates
   * %TEMP%/shyx-pdc-of-<id>-<mtime>/case (same pattern as SnappyHexMesh).
   */
  vtkGetStringMacro(CaseFoamPath);

  /**
   * When non-zero, leftover volume boundary faces (not claimed by any side)
   * are written as DefaultFacesName (type wall). Default 0: fail instead.
   */
  vtkSetMacro(AllowDefaultFaces, int);
  vtkGetMacro(AllowDefaultFaces, int);
  vtkBooleanMacro(AllowDefaultFaces, int);

  vtkSetStringMacro(DefaultFacesName);
  vtkGetStringMacro(DefaultFacesName);

  /** Checked volume PointData/CellData names are written as 0/shyx_<name>. */
  vtkDataArraySelection* GetVolumeArraySelection();

  vtkMTimeType GetMTime() override;

protected:
  vtkSHYXPartitionedCollectionToOpenFOAM();
  ~vtkSHYXPartitionedCollectionToOpenFOAM() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestDataObject(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestInformation(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  /** Updates CaseFoamPath without calling Modified() (avoids update loops). */
  void SetCaseFoamPathNoModified(const char* path);

  char* CaseDirectory = nullptr;
  char* CaseFoamPath = nullptr;
  char* DefaultFacesName = nullptr;
  int AllowDefaultFaces = 0;
  vtkSmartPointer<vtkDataArraySelection> VolumeArraySelection;

private:
  vtkSHYXPartitionedCollectionToOpenFOAM(const vtkSHYXPartitionedCollectionToOpenFOAM&) = delete;
  void operator=(const vtkSHYXPartitionedCollectionToOpenFOAM&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
