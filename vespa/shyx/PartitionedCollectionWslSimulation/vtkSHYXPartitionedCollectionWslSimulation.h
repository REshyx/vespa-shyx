/**
 * @class   vtkSHYXPartitionedCollectionWslSimulation
 * @brief   Export vtkPartitionedDataSetCollection to Exodus on WSL, run run.sh, read result.exo.
 *
 * Intended downstream of vtkSHYXDataSetToPartitionedCollection. Writes the input collection
 * as an Exodus file under WorkDirectory (default WSL UNC path), optionally executes a WSL
 * shell script (default /home/shyx/Test/run.sh), then loads ResultExoFileName from the same
 * work directory into the output vtkPartitionedDataSetCollection.
 */

#ifndef vtkSHYXPartitionedCollectionWslSimulation_h
#define vtkSHYXPartitionedCollectionWslSimulation_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXPartitionedCollectionWslSimulationModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXPARTITIONEDCOLLECTIONWSLSIMULATION_EXPORT vtkSHYXPartitionedCollectionWslSimulation
  : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXPartitionedCollectionWslSimulation* New();
  vtkTypeMacro(vtkSHYXPartitionedCollectionWslSimulation, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Windows UNC work directory, e.g. \\\\wsl.localhost\\Ubuntu-24.04\\home\\shyx\\Test */
  vtkSetStringMacro(WorkDirectory);
  vtkGetStringMacro(WorkDirectory);

  /** WSL distribution name passed to `wsl -d` on Windows. */
  vtkSetStringMacro(WslDistribution);
  vtkGetStringMacro(WslDistribution);

  /** Absolute path to run.sh inside WSL, e.g. /home/shyx/Test/run.sh. */
  vtkSetStringMacro(RunScriptWslPath);
  vtkGetStringMacro(RunScriptWslPath);

  vtkSetStringMacro(InputExoFileName);
  vtkGetStringMacro(InputExoFileName);

  vtkSetStringMacro(ResultExoFileName);
  vtkGetStringMacro(ResultExoFileName);

  /** Windows path to option file (ParaView file picker); copied into WSL work dir on Apply. */
  vtkSetStringMacro(OptionFilePath);
  vtkGetStringMacro(OptionFilePath);

  /** Target file name in WSL work directory (default opt). */
  vtkSetStringMacro(OptionFileWslName);
  vtkGetStringMacro(OptionFileWslName);

  /** When non-zero (default), invoke RunScriptWslPath via WSL after writing input Exodus. */
  vtkSetMacro(RunScript, int);
  vtkGetMacro(RunScript, int);
  vtkBooleanMacro(RunScript, int);

protected:
  vtkSHYXPartitionedCollectionWslSimulation();
  ~vtkSHYXPartitionedCollectionWslSimulation() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  char* WorkDirectory = nullptr;
  char* WslDistribution = nullptr;
  char* RunScriptWslPath = nullptr;
  char* InputExoFileName = nullptr;
  char* ResultExoFileName = nullptr;
  char* OptionFilePath = nullptr;
  char* OptionFileWslName = nullptr;
  int RunScript = 1;

private:
  vtkSHYXPartitionedCollectionWslSimulation(
    const vtkSHYXPartitionedCollectionWslSimulation&) = delete;
  void operator=(const vtkSHYXPartitionedCollectionWslSimulation&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
