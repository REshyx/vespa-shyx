/**
 * @class   vtkSHYXDisconnectedRegionFuse
 * @brief   Fuse nearby vertices across disconnected pieces of vtkPolyData.
 *
 * vtkSHYXDisconnectedRegionFuse accepts one or more vtkPolyData inputs on port 0
 * (repeatable). When FuseWithinInput is on (default), connected components (from
 * verts, lines, polys, and strips) are fuse regions. When it is off, each input
 * connection is one region and disconnected pieces inside that connection are not
 * merged. Vertices from different regions may merge when within FuseThreshold;
 * vertices in the same region are never merged directly.
 *
 * FuseVerts / FuseLines / FusePolys choose which cell arrays are remapped onto
 * the fused points and written to the output. Degenerate cells after fusion are
 * dropped (verts with no points, lines with fewer than 2 points, polys with
 * fewer than 3). Triangle strips are used only for connectivity, not output.
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

    vtkSetMacro(FuseWithinInput, bool);
    vtkGetMacro(FuseWithinInput, bool);
    vtkBooleanMacro(FuseWithinInput, bool);

    vtkSetMacro(FuseVerts, bool);
    vtkGetMacro(FuseVerts, bool);
    vtkBooleanMacro(FuseVerts, bool);

    vtkSetMacro(FuseLines, bool);
    vtkGetMacro(FuseLines, bool);
    vtkBooleanMacro(FuseLines, bool);

    vtkSetMacro(FusePolys, bool);
    vtkGetMacro(FusePolys, bool);
    vtkBooleanMacro(FusePolys, bool);

protected:
    vtkSHYXDisconnectedRegionFuse();
    ~vtkSHYXDisconnectedRegionFuse() override = default;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    double FuseThreshold = 0.01;
    bool FuseWithinInput = true;
    bool FuseVerts = true;
    bool FuseLines = true;
    bool FusePolys = true;

private:
    vtkSHYXDisconnectedRegionFuse(const vtkSHYXDisconnectedRegionFuse&) = delete;
    void operator=(const vtkSHYXDisconnectedRegionFuse&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
