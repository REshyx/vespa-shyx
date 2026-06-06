/**
 * @class   vtkSHYXEnhancedRuler
 * @brief   Surface ruler with vertex snapping and geodesic path distance.
 *
 * Port 0 input is a vtkDataSet (typically a surface vtkPolyData). Endpoints snap to the nearest
 * input point id when SnapToInputVertices is on. Output port 0 is the shortest path polyline:
 * Dijkstra on triangle mesh edges when polygons are present, otherwise along VTK_LINE /
 * VTK_POLY_LINE segments. GeodesicDistance is updated on each execution (-1 when unreachable).
 */

#ifndef vtkSHYXEnhancedRuler_h
#define vtkSHYXEnhancedRuler_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXEnhancedRulerModule.h"

VTK_ABI_NAMESPACE_BEGIN

class vtkPolyData;

class VTKSHYXENHANCEDRULER_EXPORT vtkSHYXEnhancedRuler : public vtkDataObjectAlgorithm
{
public:
    static vtkSHYXEnhancedRuler* New();
    vtkTypeMacro(vtkSHYXEnhancedRuler, vtkDataObjectAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    vtkSetVector3Macro(Point1, double);
    vtkGetVector3Macro(Point1, double);
    vtkSetVector3Macro(Point2, double);
    vtkGetVector3Macro(Point2, double);

    vtkGetMacro(Point1VertexId, vtkIdType);
    vtkSetMacro(Point1VertexId, vtkIdType);
    vtkGetMacro(Point2VertexId, vtkIdType);
    vtkSetMacro(Point2VertexId, vtkIdType);

    vtkGetMacro(SnapToInputVertices, bool);
    vtkSetMacro(SnapToInputVertices, bool);
    vtkBooleanMacro(SnapToInputVertices, bool);

    vtkGetMacro(GeodesicDistance, double);

    /**
     * Shortest path distance between two input point ids. Uses triangle-surface Dijkstra when
     * polygons are present; otherwise walks VTK_LINE / VTK_POLY_LINE connectivity. Optionally
     * fills \p pathOut with the path polyline. Returns -1 when unreachable.
     */
    static double ComputePathDistance(
        vtkPolyData* topology, vtkIdType startId, vtkIdType endId, vtkPolyData* pathOut = nullptr);

protected:
    vtkSHYXEnhancedRuler();
    ~vtkSHYXEnhancedRuler() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    double Point1[3] = { 0.0, 0.0, 0.0 };
    double Point2[3] = { 1.0, 0.0, 0.0 };
    vtkIdType Point1VertexId = 0;
    /** Sentinel -1: on first execute with input, initialize to the last input point id. */
    vtkIdType Point2VertexId = -1;
    bool SnapToInputVertices = true;

    double GeodesicDistance = -1.0;

private:
    vtkSHYXEnhancedRuler(const vtkSHYXEnhancedRuler&) = delete;
    void operator=(const vtkSHYXEnhancedRuler&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
