#ifndef pqSHYXEnhancedRulerWidget_h
#define pqSHYXEnhancedRulerWidget_h

#include "pqInteractivePropertyWidget.h"

#include <QPointer>

class pqPipelineSource;
class vtkDistanceRepresentation2D;
class vtkPolyData;
class vtkPoints;

/**
 * Interactive distance widget for SHYX Enhanced Ruler. While dragging, the 3D axis label shows only
 * the Euclidean distance (geodesic is computed on release). On interaction end, each
 * endpoint is snapped using the same screen-ray mesh-point pick as ParaView's Select Points
 * (vtkSMRenderViewProxy::ConvertDisplayToPointOnSurface with snapOnMeshPoint).
 */
class pqSHYXEnhancedRulerWidget : public pqInteractivePropertyWidget
{
    Q_OBJECT
    typedef pqInteractivePropertyWidget Superclass;

public:
    pqSHYXEnhancedRulerWidget(
        vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXEnhancedRulerWidget() override;

    void select() override;

protected Q_SLOTS:
    void placeWidget() override;

private Q_SLOTS:
    void onRulerInteraction();
    void onRulerEndInteraction();
    void onInputDataUpdated();
    void onPipelineSourceUpdated();

private:
    bool initializeEndpointsIfNeeded();
    vtkPolyData* inputSurfacePoly() const;
    vtkPoints* inputPoints() const;
    bool pickMeshVertexAtDisplay(const int displayPos[2], double worldOut[3], vtkIdType& vertexIdOut) const;
    static vtkIdType vertexIdFromPosition(vtkPoints* pts, const double world[3]);
    static vtkIdType nearestPointId(vtkPoints* pts, const double p[3]);
    static double computeGeodesicDistance(vtkPolyData* surface, vtkIdType startId, vtkIdType endId);
    bool snapEndpointFromHandle(
        vtkDistanceRepresentation2D* rep, bool point1, double worldOut[3], vtkIdType& vertexIdOut) const;
    void snapEndpointsAndUpdateLabel();
    void syncWidgetFromFilterOnSelect();
    void refreshRulerLabel(bool includeGeodesic);
    void syncGeodesicDistancePanel();

    QPointer<pqPipelineSource> PipelineSource;
};

#endif
