/**
 * @class   vtkSHYXMinimumOBBFilter
 * @brief   Build an OBB (min-volume or PCA) or AABB for a vtkDataSet as vtkPolyData.
 *
 * BoxType selects the fit:
 * - OBB (min-volume, default): CGAL::oriented_bounding_box (evolutionary approximation of the
 *   minimum-volume oriented box; uses the convex hull). Falls back to AABB if the result is
 *   degenerate or larger than the world AABB.
 * - OBB (PCA): vtkOBBTree::ComputeOBB covariance / eigenvector heuristic (not min-volume).
 * - AABB: axis-aligned box from point bounds (world X/Y/Z).
 *
 * Output is a closed triangle mesh. Field arrays document the fitted result: OBB.Center,
 * OBB.HalfLengths, OBB.Axis0..2, OBB.Volume, and OBB.IsAxisAlignedFallback (1 for AABB / fallback).
 *
 * Position / Rotation / Scale match ParaView Interactive Box (vtkPVTransform). When OBB field
 * data is present, the mesh transform is M(current)*M(baseline)^{-1} so the 3D box widget can
 * initialize to the fitted box and then expand / move / rotate it. XML defaults (Scale=1, zero
 * PRS) are treated as "not yet placed" and leave the raw fitted mesh unchanged until the widget
 * pushes real parameters.
 *
 * @sa
 * vtkOBBTree, CGAL::oriented_bounding_box
 */

#ifndef vtkSHYXMinimumOBBFilter_h
#define vtkSHYXMinimumOBBFilter_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXMinimumOBBFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXMINIMUMOBBFILTER_EXPORT vtkSHYXMinimumOBBFilter : public vtkDataObjectAlgorithm
{
public:
    static vtkSHYXMinimumOBBFilter* New();
    vtkTypeMacro(vtkSHYXMinimumOBBFilter, vtkDataObjectAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /** When true (default), copy input points into a contiguous buffer before OBB (slightly more
     *  memory, safe for all vtkDataSet types). When false, vtkPointSet inputs use GetPoints()
     *  directly without copying. */
    vtkSetMacro(CopyInputPoints, int);
    vtkGetMacro(CopyInputPoints, int);
    vtkBooleanMacro(CopyInputPoints, int);

    enum BoxTypeEnum
    {
        BOX_TYPE_OBB_PCA = 0,
        BOX_TYPE_AABB = 1,
        BOX_TYPE_OBB_MIN_VOLUME = 2
    };

    /** 0 = PCA OBB, 1 = AABB, 2 = CGAL min-volume OBB (default). */
    vtkSetClampMacro(BoxType, int, BOX_TYPE_OBB_PCA, BOX_TYPE_OBB_MIN_VOLUME);
    vtkGetMacro(BoxType, int);

    /** Interactive box Position: world image of ref corner (0,0,0) under vtkPVTransform. */
    vtkGetVector3Macro(Position, double);
    vtkSetVector3Macro(Position, double);
    /** Degrees: Translate, RotateZ, RotateX, RotateY, Scale (same as pqBoxPropertyWidget). */
    vtkGetVector3Macro(Rotation, double);
    vtkSetVector3Macro(Rotation, double);
    /**
     * Per-axis world edge lengths for the unit reference box (Interactive Box Scale). Divided by
     * 2*OBB.HalfLengths before vtkTransform::Scale so values match the box widget.
     */
    vtkGetVector3Macro(Scale, double);
    vtkSetVector3Macro(Scale, double);

    vtkGetVector6Macro(ReferenceBounds, double);
    vtkSetVector6Macro(ReferenceBounds, double);

    vtkSetMacro(UseReferenceBounds, int);
    vtkGetMacro(UseReferenceBounds, int);
    vtkBooleanMacro(UseReferenceBounds, int);

protected:
    vtkSHYXMinimumOBBFilter();
    ~vtkSHYXMinimumOBBFilter() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    int CopyInputPoints = 1;
    int BoxType = BOX_TYPE_OBB_MIN_VOLUME;
    double Position[3] = { 0.0, 0.0, 0.0 };
    double Rotation[3] = { 0.0, 0.0, 0.0 };
    double Scale[3] = { 1.0, 1.0, 1.0 };
    double ReferenceBounds[6] = { 0.0, 1.0, 0.0, 1.0, 0.0, 1.0 };
    int UseReferenceBounds = 1;

    /** Interactive-Box PRS that matches the raw fitted mesh when the OBB field fingerprint changes. */
    double BaselinePosition[3] = { 0.0, 0.0, 0.0 };
    double BaselineRotation[3] = { 0.0, 0.0, 0.0 };
    double BaselineScale[3] = { 1.0, 1.0, 1.0 };
    bool ObbBaselineValid = false;
    unsigned long long ObbFieldFingerprint = 0ULL;
    /**
     * Set when the fitted box (BoxType / input) changes. While set, RequestData ignores stale
     * interactive PRS and outputs the raw fitted mesh until Position/Rotation/Scale match the new
     * baseline (client placeWidget / Reset). Prevents one-Apply lag that warps the new box into
     * the previous pose.
     */
    bool ObbFitJustChanged = false;

private:
    vtkSHYXMinimumOBBFilter(const vtkSHYXMinimumOBBFilter&) = delete;
    void operator=(const vtkSHYXMinimumOBBFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
