/**
 * @class   vtkCGALMeshChecker
 * @brief   Check various properties of a mesh, like manifoldness
 *
 * vtkCGALMeshChecker is a filter allowing to perform diagnosis on a mesh
 * to check manifoldness, auto-intersection, watertightness...
 */

#ifndef vtkCGALMeshChecker_h
#define vtkCGALMeshChecker_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALMeshChecker : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALMeshChecker* New();
  vtkTypeMacro(vtkCGALMeshChecker, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  vtkCGALMeshChecker() = default;
  ~vtkCGALMeshChecker() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  // fields
  bool CheckWatertight = true;
  bool RepairWatertight = false;
  bool CheckIntersect = true;

private:
  vtkCGALMeshChecker(const vtkCGALMeshChecker&) = delete;
  void operator=(const vtkCGALMeshChecker&)          = delete;
};

#endif
