/**
 * @class   vtkSHYXRadiusNeighborCount
 * @brief   For each point, count how many input points lie within a fixed radius (parallel).
 *
 * Uses vtkStaticPointLocator and vtkSMPTools. The output is the input vtkPolyData shallow
 * copied with a new point-data array \c NeighborsInRadius (vtkIdTypeArray). The closed
 * ball [0, Radius] includes the query point itself.
 *
 * @sa vtkStaticPointLocator vtkSMPTools
 */

#ifndef vtkSHYXRadiusNeighborCount_h
#define vtkSHYXRadiusNeighborCount_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXRadiusNeighborCountModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXRADIUSNEIGHBORCOUNT_EXPORT vtkSHYXRadiusNeighborCount : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXRadiusNeighborCount* New();
  vtkTypeMacro(vtkSHYXRadiusNeighborCount, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(Radius, double);
  vtkGetMacro(Radius, double);

protected:
  vtkSHYXRadiusNeighborCount();
  ~vtkSHYXRadiusNeighborCount() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

private:
  vtkSHYXRadiusNeighborCount(const vtkSHYXRadiusNeighborCount&) = delete;
  void operator=(const vtkSHYXRadiusNeighborCount&) = delete;

  double Radius = 1.0;
};

VTK_ABI_NAMESPACE_END
#endif
