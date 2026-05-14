/**
 * @class   vtkSHYXVascularStentPlacement
 * @brief   Stent-like radial correction along a walked centerline segment: flat-capped strict zone plus
 *          smooth cap-side blending, displacement primarily along point normals.
 *
 * Port 0 is the vessel surface (vtkPolyData). Port 1 is a centerline polyline (vtkPolyData with
 * vtkLine / vtkPolyLine cells). The filter walks a stent-length window from AnchorCenterlinePointId,
 * optionally densifies the axis (StentAxisSampleSpacing), then:
 *
 * - **Strict region**: intersection of perpendicular distance ≤ StentRadius to the axis polyline with
 *   the two half-spaces of a **flat-ended** stent (planes through the first/last axis points along the
 *   first/last segment tangents). Inside this slab, each vertex is moved along its **surface point
 *   normal** so that its distance to the axis becomes StentRadius (numerical solve along the normal).
 *
 * - **Cap-side smooth bands**: vertices still within distance R of the axis but lying **outside** the
 *   flat slab on the start and/or end (hemisphere-side of the capsule) are partially moved toward the
 *   same normal-based stent target; the axial influence width is CapSideSmoothInfluenceRange (smoothstep).
 */

#ifndef vtkSHYXVascularStentPlacement_h
#define vtkSHYXVascularStentPlacement_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXVascularStentPlacementModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXVASCULARSTENTPLACEMENT_EXPORT vtkSHYXVascularStentPlacement : public vtkPolyDataAlgorithm
{
public:
    static vtkSHYXVascularStentPlacement* New();
    vtkTypeMacro(vtkSHYXVascularStentPlacement, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /** Centerline input (port 1). */
    void SetCenterlineConnection(vtkAlgorithmOutput* algOutput);

    vtkGetMacro(AnchorCenterlinePointId, vtkIdType);
    vtkSetClampMacro(AnchorCenterlinePointId, vtkIdType, 0, VTK_ID_MAX);

    vtkGetMacro(StentLength, double);
    vtkSetClampMacro(StentLength, double, 0.0, VTK_DOUBLE_MAX);

    vtkGetMacro(StentRadius, double);
    vtkSetClampMacro(StentRadius, double, 0.0, VTK_DOUBLE_MAX);

    /** Maximum arc length between consecutive samples on the stent axis polyline; 0 disables densification. */
    vtkGetMacro(StentAxisSampleSpacing, double);
    vtkSetClampMacro(StentAxisSampleSpacing, double, 0.0, VTK_DOUBLE_MAX);

    /**
     * Axial half-width (world units) on each flat-cap side over which cap-side points (still within
     * distance R of the axis but outside the flat slab) blend from no motion to full stent alignment
     * (smoothstep). Zero disables cap-side smoothing (only the strict flat-capped cylinder is updated).
     */
    vtkGetMacro(CapSideSmoothInfluenceRange, double);
    vtkSetClampMacro(CapSideSmoothInfluenceRange, double, 0.0, VTK_DOUBLE_MAX);

    /** If true, use point normals from the input surface when a "Normals" point array exists. */
    vtkGetMacro(PreferInputPointNormals, bool);
    vtkSetMacro(PreferInputPointNormals, bool);
    vtkBooleanMacro(PreferInputPointNormals, bool);

    /** Point-data array name on Centerline; when non-empty, snapped widget updates StentRadius from
     *  tuple at AnchorCenterlinePointId (default: MaximumInscribedSphereRadius, e.g. VMTK). */
    vtkGetStringMacro(CenterlineRadiusArrayName);
    vtkSetStringMacro(CenterlineRadiusArrayName);

    /** Linked to the ImplicitCylinder 3D widget center (ParaView UI). */
    vtkGetVector3Macro(StentWidgetCenter, double);
    vtkSetVector3Macro(StentWidgetCenter, double);
    /** Linked to the ImplicitCylinder 3D widget axis (unit vector recommended). */
    vtkGetVector3Macro(StentWidgetAxis, double);
    vtkSetVector3Macro(StentWidgetAxis, double);

protected:
    vtkSHYXVascularStentPlacement();
    ~vtkSHYXVascularStentPlacement() override;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    vtkIdType AnchorCenterlinePointId = 0;
    double StentLength = 10.0;
    double StentRadius = 1.0;
    double StentAxisSampleSpacing = 0.0;
    double CapSideSmoothInfluenceRange = 1.0;
    bool PreferInputPointNormals = false;
    double StentWidgetCenter[3] = { 0.0, 0.0, 0.0 };
    double StentWidgetAxis[3] = { 0.0, 0.0, 1.0 };
    char* CenterlineRadiusArrayName = nullptr;

private:
    vtkSHYXVascularStentPlacement(const vtkSHYXVascularStentPlacement&) = delete;
    void operator=(const vtkSHYXVascularStentPlacement&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
