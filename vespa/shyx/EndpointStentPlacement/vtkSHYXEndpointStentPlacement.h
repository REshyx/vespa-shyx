/**
 * @class   vtkSHYXEndpointStentPlacement
 * @brief   Stent-like radial correction along a centerline segment between two endpoint vertices.
 *
 * Port 0 is the vessel surface (vtkPolyData). Port 1 is a centerline polyline (vtkPolyData with
 * vtkLine / vtkPolyLine cells). The stent axis is the shortest path along the centerline graph
 * between Point1VertexId and Point2VertexId (same method as vtkSHYXEnhancedRuler). StentLength is
 * the path arc length; StentRadius may be taken from CenterlineRadiusArrayName at the path midpoint.
 *
 * Deformation logic matches vtkSHYXVascularStentPlacement: strict flat-capped slab, optional geodesic
 * band smoothing, and the same output point-data arrays.
 */

#ifndef vtkSHYXEndpointStentPlacement_h
#define vtkSHYXEndpointStentPlacement_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXEndpointStentPlacementModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXENDPOINTSTENTPLACEMENT_EXPORT vtkSHYXEndpointStentPlacement : public vtkPolyDataAlgorithm
{
public:
    static vtkSHYXEndpointStentPlacement* New();
    vtkTypeMacro(vtkSHYXEndpointStentPlacement, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /** Centerline input (port 1). */
    void SetCenterlineConnection(vtkAlgorithmOutput* algOutput);

    vtkSetVector3Macro(Point1, double);
    vtkGetVector3Macro(Point1, double);
    vtkSetVector3Macro(Point2, double);
    vtkGetVector3Macro(Point2, double);

    vtkGetMacro(Point1VertexId, vtkIdType);
    vtkSetMacro(Point1VertexId, vtkIdType);
    vtkGetMacro(Point2VertexId, vtkIdType);
    vtkSetMacro(Point2VertexId, vtkIdType);

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

    /** Point-data array name on Centerline; when non-empty, StentRadius is updated from the path
     *  midpoint vertex (default: MaximumInscribedSphereRadius, e.g. VMTK). */
    vtkGetStringMacro(CenterlineRadiusArrayName);
    vtkSetStringMacro(CenterlineRadiusArrayName);

    vtkGetStringMacro(AffectMaskArrayName);
    vtkSetStringMacro(AffectMaskArrayName);

    vtkGetStringMacro(GeodesicToZeroRegionArrayName);
    vtkSetStringMacro(GeodesicToZeroRegionArrayName);

    vtkGetMacro(GeodesicSmoothInfluenceRange, double);
    vtkSetClampMacro(GeodesicSmoothInfluenceRange, double, 0.0, VTK_DOUBLE_MAX);

    vtkGetMacro(GeodesicSmoothPowerLambda, double);
    vtkSetClampMacro(GeodesicSmoothPowerLambda, double, 0.1, 10.0);

    vtkGetStringMacro(GeodesicSmoothStrengthArrayName);
    vtkSetStringMacro(GeodesicSmoothStrengthArrayName);

    vtkGetStringMacro(DisplacementArrayName);
    vtkSetStringMacro(DisplacementArrayName);

    /**
     * Coronary stent catalog. Index 0 = custom measured values from endpoints / drag.
     * Index &gt; 0 selects a catalog diameter (2.25–4.0) or length (13–38).
     */
    vtkGetMacro(StentCatalogDiameterIndex, int);
    vtkSetClampMacro(StentCatalogDiameterIndex, int, 0, 6);
    vtkGetMacro(StentCatalogLengthIndex, int);
    vtkSetClampMacro(StentCatalogLengthIndex, int, 0, 11);

    /**
     * Iteratively move both endpoints along the centerline graph: expand in steps until path length
     * first exceeds \p targetLength, or contract in steps until it first falls below. Vertex ids and
     * positions are returned; optional \p achievedLengthOut is the resulting path length.
     */
    static bool ComputeSymmetricEndpointsForLength(vtkPolyData* centerline, vtkIdType id1, vtkIdType id2,
        double targetLength, vtkIdType& outId1, double outPos1[3], vtkIdType& outId2, double outPos2[3],
        double* achievedLengthOut = nullptr);

protected:
    vtkSHYXEndpointStentPlacement();
    ~vtkSHYXEndpointStentPlacement() override;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    double Point1[3] = { 0.0, 0.0, 0.0 };
    double Point2[3] = { 1.0, 0.0, 0.0 };
    vtkIdType Point1VertexId = 0;
    /** Sentinel -1: on first execute with centerline, initialize to the last centerline point id. */
    vtkIdType Point2VertexId = -1;
    double StentLength = 10.0;
    double StentRadius = 1.0;
    double StentAxisSampleSpacing = 0.0;
    bool PreferInputPointNormals = false;
    char* CenterlineRadiusArrayName = nullptr;
    char* AffectMaskArrayName = nullptr;
    char* GeodesicToZeroRegionArrayName = nullptr;
    double GeodesicSmoothInfluenceRange = 5.0;
    double GeodesicSmoothPowerLambda = 1.0;
    char* GeodesicSmoothStrengthArrayName = nullptr;
    char* DisplacementArrayName = nullptr;
    int StentCatalogDiameterIndex = 0;
    int StentCatalogLengthIndex = 0;

private:
    vtkSHYXEndpointStentPlacement(const vtkSHYXEndpointStentPlacement&) = delete;
    void operator=(const vtkSHYXEndpointStentPlacement&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
