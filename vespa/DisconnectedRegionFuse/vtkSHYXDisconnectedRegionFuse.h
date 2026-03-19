/**
 * @class   vtkSHYXDisconnectedRegionFuse
 * @brief   Merge disconnected regions of a surface mesh by fusing nearby vertices across regions.
 *
 * vtkSHYXDisconnectedRegionFuse takes a surface mesh with multiple disconnected regions and
 * merges them by finding vertex pairs across different regions that are within a distance
 * threshold, then fusing those vertices. Vertices within the same connected region are
 * never merged. A vertex in one region may merge with multiple vertices from different
 * other regions.
 *
 * @sa
 * vtkPolyDataConnectivityFilter, vtkCleanPolyData
 */

#ifndef vtkSHYXDisconnectedRegionFuse_h
#define vtkSHYXDisconnectedRegionFuse_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXDisconnectedRegionFuseModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXDISCONNECTEDREGIONFUSE_EXPORT vtkSHYXDisconnectedRegionFuse : public vtkPolyDataAlgorithm
{
public:
    static vtkSHYXDisconnectedRegionFuse* New();
    vtkTypeMacro(vtkSHYXDisconnectedRegionFuse, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    vtkSetMacro(FuseThreshold, double);
    vtkGetMacro(FuseThreshold, double);

protected:
    vtkSHYXDisconnectedRegionFuse();
    ~vtkSHYXDisconnectedRegionFuse() override = default;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    double FuseThreshold = 0.01;

private:
    vtkSHYXDisconnectedRegionFuse(const vtkSHYXDisconnectedRegionFuse&) = delete;
    void operator=(const vtkSHYXDisconnectedRegionFuse&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
