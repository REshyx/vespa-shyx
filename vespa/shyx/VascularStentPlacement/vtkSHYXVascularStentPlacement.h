/**
 * @class   vtkSHYXVascularStentPlacement
 * @brief   Stent-like radial correction along a walked centerline segment inside the flat-capped strict
 *          zone only; displacement primarily along point normals.
 *
 * Port 0 is the vessel surface (vtkPolyData). Port 1 is a centerline polyline (vtkPolyData with
 * vtkLine / vtkPolyLine cells). The filter walks a stent-length window from AnchorCenterlinePointId,
 * optionally densifies the axis (StentAxisSampleSpacing), then:
 *
 * - **Strict region only**: intersection of perpendicular distance ≤ StentRadius to the axis polyline with
 *   the two half-spaces of a **flat-ended** stent (planes through the first/last axis points along the
 *   first/last segment tangents). Inside this slab, each vertex is moved along its **surface point
 *   normal** so that its distance to the axis becomes StentRadius (numerical solve along the normal).
 *   Vertices within distance R of the axis but outside the flat slab (capsule hemispheres) are unchanged.
 *
 * Output point data includes an int array (AffectMaskArrayName, default StentPlacementAffectMask): 0 at
 * vertices updated in the strict region, -1 elsewhere. Values are set when applying each vertex move,
 * not by post-comparing coordinates.
 *
 * A vtkDoubleArray (GeodesicToZeroRegionArrayName, default StentGeodesicToZeroRegion) stores **weighted**
 * graph distance along mesh edges (each edge weight = Euclidean length after the strict placement pass) from
 * each mask≠0 vertex to the nearest mask==0 vertex; mask==0 vertices get 0; unreachable components get -1.
 * This distance is computed **between** deformation stages: on the strict-deformed geometry, before the
 * optional geodesic-band smooth pass.
 *
 * Strict slab classification uses perpendicular distance to the **stent segment axis** (walked window on
 * the centerline), not the full port-1 polyline. Optional geodesic band smoothing
 * (GeodesicSmoothInfluenceRange x, GeodesicSmoothPowerLambda λ in [0.1, 10]) moves mask==-1 vertices with
 * 0 < g < x by p' = p + S * (R - d) * n_dir only when R > d, where g is **weighted** surface geodesic (same
 * length units as x) to mask-0 computed on the strict-deformed mesh, d is
 * perpendicular distance to the **entire** centerline (port 1), R is StentRadius, S = (1 - g/x)^λ, and n_dir
 * is the surface normal oriented toward increasing radius from that centerline (same sign convention as the strict normal solve). Strength is
 * written to GeodesicSmoothStrengthArrayName (default StentGeodesicSmoothStrength): 1 on strict mask, S when the band move applies, 0 otherwise.
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

    /** If true, use point normals from the input surface when a "Normals" point array exists. */
    vtkGetMacro(PreferInputPointNormals, bool);
    vtkSetMacro(PreferInputPointNormals, bool);
    vtkBooleanMacro(PreferInputPointNormals, bool);

    /** Point-data array name on Centerline; when non-empty, snapped widget updates StentRadius from
     *  tuple at AnchorCenterlinePointId (default: MaximumInscribedSphereRadius, e.g. VMTK). */
    vtkGetStringMacro(CenterlineRadiusArrayName);
    vtkSetStringMacro(CenterlineRadiusArrayName);

    /** Point-data int array on output: 0 = vertex moved in strict region, -1 = untouched. */
    vtkGetStringMacro(AffectMaskArrayName);
    vtkSetStringMacro(AffectMaskArrayName);

    /** vtkDoubleArray on output: weighted geodesic distance to mask==0 region on strict-deformed mesh (see class doc). */
    vtkGetStringMacro(GeodesicToZeroRegionArrayName);
    vtkSetStringMacro(GeodesicToZeroRegionArrayName);

    /** Band threshold x (world length): mask==-1 with weighted geodesic g in (0, x); 0 disables band smooth. */
    vtkGetMacro(GeodesicSmoothInfluenceRange, double);
    vtkSetClampMacro(GeodesicSmoothInfluenceRange, double, 0.0, VTK_DOUBLE_MAX);

    /** Exponent λ in (1 - g/x)^λ for band strength; clamped to [0.1, 10]. */
    vtkGetMacro(GeodesicSmoothPowerLambda, double);
    vtkSetClampMacro(GeodesicSmoothPowerLambda, double, 0.1, 10.0);

    /** vtkDoubleArray: 1 on strict mask; S in band only when vertex moves (R > d); else 0. */
    vtkGetStringMacro(GeodesicSmoothStrengthArrayName);
    vtkSetStringMacro(GeodesicSmoothStrengthArrayName);

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
    bool PreferInputPointNormals = false;
    double StentWidgetCenter[3] = { 0.0, 0.0, 0.0 };
    double StentWidgetAxis[3] = { 0.0, 0.0, 1.0 };
    char* CenterlineRadiusArrayName = nullptr;
    char* AffectMaskArrayName = nullptr;
    char* GeodesicToZeroRegionArrayName = nullptr;
    double GeodesicSmoothInfluenceRange = 5.0;
    double GeodesicSmoothPowerLambda = 1.0;
    char* GeodesicSmoothStrengthArrayName = nullptr;

private:
    vtkSHYXVascularStentPlacement(const vtkSHYXVascularStentPlacement&) = delete;
    void operator=(const vtkSHYXVascularStentPlacement&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
