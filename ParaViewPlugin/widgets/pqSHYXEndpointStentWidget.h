#ifndef pqSHYXEndpointStentWidget_h
#define pqSHYXEndpointStentWidget_h

#include "pqInteractivePropertyWidget.h"

#include <QPointer>

class pqPipelineSource;
class QCheckBox;
class QComboBox;
class vtkDistanceRepresentation2D;
class vtkSHYXEndpointStentRepresentation;
class vtkPolyData;
class vtkPoints;

/**
 * Two-endpoint distance widget for SHYX Endpoint Stent Placement. Endpoints snap to centerline
 * port 1 (screen-ray pick, same as Enhanced Ruler on a surface). On release, StentLength is set
 * from the centerline path distance and StentDiameter from CenterlineRadiusArrayName at the path
 * midpoint when configured.
 */
class pqSHYXEndpointStentWidget : public pqInteractivePropertyWidget
{
    Q_OBJECT
    typedef pqInteractivePropertyWidget Superclass;

public:
    pqSHYXEndpointStentWidget(
        vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXEndpointStentWidget() override;

    void select() override;

protected Q_SLOTS:
    void placeWidget() override;

private Q_SLOTS:
    void onRulerInteraction();
    void onRulerEndInteraction();
    void onCenterlineDataUpdated();
    void onPipelineSourceUpdated();
    void onShowInteractiveEndpointsToggled(bool visible);
    void onCatalogDiameterChanged(int comboIndex);
    void onCatalogLengthChanged(int comboIndex);

private:
    bool refreshCenterlineClientData();
    bool promoteCheckedEndpointsToUncheckedIfNeeded();
    bool ensureEndpointsSyncedFromCenterline();
    void pushEndpointPositionsToWidget(const double pos1[3], const double pos2[3]);
    vtkPolyData* centerlineClientPoly() const;
    vtkPoints* centerlinePoints() const;
    bool pickCenterlineVertexAtDisplay(
        const int displayPos[2], double worldOut[3], vtkIdType& vertexIdOut) const;
    static vtkIdType vertexIdFromPosition(vtkPoints* pts, const double world[3]);
    static vtkIdType nearestPointId(vtkPoints* pts, const double p[3]);
    static double computeCenterlinePathDistance(vtkPolyData* centerline, vtkIdType startId, vtkIdType endId);
    bool snapEndpointFromHandle(
        vtkDistanceRepresentation2D* rep, bool point1, double worldOut[3], vtkIdType& vertexIdOut) const;
    void snapEndpointsAndUpdateStentParams();
    void syncWidgetFromFilterOnSelect();
    void refreshRulerLabel(bool includePathLength);
    void updateStentLengthAndRadius(vtkIdType id1, vtkIdType id2);
    static vtkIdType midpointVertexOnPath(vtkPolyData* pathPoly, vtkPolyData* centerline);
    void populateCatalogCombos();
    void syncCatalogCombosFromFilter();
    void setCatalogSelectionToCustom();
    void applyCatalogDiameter(int catalogIndex);
    void applyCatalogLength(int catalogIndex);
    void markCatalogPropertiesModified();
    void pushEndpointsToFilter(vtkIdType id1, const double pos1[3], vtkIdType id2, const double pos2[3]);
    vtkSHYXEndpointStentRepresentation* stentRepresentation() const;
    void syncStentPreview();

    QPointer<pqPipelineSource> PipelineSource;
    QComboBox* DiameterCatalogCombo = nullptr;
    QComboBox* LengthCatalogCombo = nullptr;
    QCheckBox* ShowStentPreviewCheck = nullptr;
    bool UpdatingCatalogCombos = false;
    bool ApplyingCatalogChange = false;
};

#endif
