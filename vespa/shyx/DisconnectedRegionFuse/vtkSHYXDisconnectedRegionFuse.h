/**
 * @class   vtkSHYXDisconnectedRegionFuse
 * @brief   Fuse nearby vertices across disconnected pieces of vtkPolyData.
 *
 * vtkSHYXDisconnectedRegionFuse accepts one or more vtkPolyData inputs on port 0
 * (repeatable). When FuseWithinInput is on (default), connected components (from
 * verts, lines, polys, and strips) are fuse regions. When it is off, each input
 * connection is one region and disconnected pieces inside that connection are not
 * merged. Each pair of regions is joined only at its closest vertices, and only
 * if that gap is <= FuseThreshold. FusePositionMode is Average (midpoint of the
 * welded vertices), Snap small to large (keep the larger region's vertex;
 * region size is its point count), or Construct primitives (keep all points and
 * add a line or two triangles at the closest gap). ComplianceWeight blends Euclidean
 * distance with a geometric prior: lines prefer tip-to-tip along the outward
 * tangent; surfaces prefer connections in the tangent plane. 0 is distance only.
 *
 * FuseVerts / FuseLines / FusePolys choose which cell arrays are remapped onto
 * the fused points and written to the output. Degenerate cells after fusion are
 * dropped (verts with no points, lines with fewer than 2 points, polys with
 * fewer than 3). Triangle strips are used only for connectivity, not output.
 * Cell array SHYXFuseMark: 0 unchanged, 1 original cell incident to a fused
 * vertex, 2 newly constructed primitive.
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

    enum FusePositionModeType
    {
        FUSE_POSITION_AVERAGE = 0,
        FUSE_POSITION_SNAP_SMALL_TO_LARGE = 1,
        FUSE_POSITION_CONSTRUCT_PRIMITIVES = 2
    };

    vtkGetMacro(FusePositionMode, int);
    vtkSetClampMacro(FusePositionMode, int, 0, 2);

    vtkSetClampMacro(ComplianceWeight, double, 0.0, 1.0);
    vtkGetMacro(ComplianceWeight, double);

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
    int FusePositionMode = FUSE_POSITION_AVERAGE;
    double ComplianceWeight = 0.0;
    bool FuseVerts = true;
    bool FuseLines = true;
    bool FusePolys = true;

private:
    vtkSHYXDisconnectedRegionFuse(const vtkSHYXDisconnectedRegionFuse&) = delete;
    void operator=(const vtkSHYXDisconnectedRegionFuse&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
