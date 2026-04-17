/**
 * @class   vtkSHYXDisconnectedRegionFuse
 * @brief   Fuse nearby vertices across multiple input surface meshes (one mesh per region).
 *
 * vtkSHYXDisconnectedRegionFuse accepts multiple vtkPolyData inputs on port 0 (repeatable).
 * Each input is treated as one region: vertices from different inputs may be fused when
 * within FuseThreshold, while vertices from the same input are never merged (even if that
 * mesh has multiple disconnected pieces). A vertex may merge with multiple vertices from
 * other inputs.
 *
 * @sa
 * vtkCleanPolyData
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
