#ifndef pqVESPAEndClipperPlaneHandlesWidget_h
#define pqVESPAEndClipperPlaneHandlesWidget_h

#include "pqPropertyGroupWidget.h"

#include <QMetaObject>
#include <QModelIndex>
#include <QPointer>
#include <QShowEvent>
#include <QVector>
#include <vector>

#include "vtkPolyData.h"
#include "vtkSmartPointer.h"

class QCheckBox;
class QLabel;
class pqArraySelectionWidget;
class pqPipelineSource;
class pqRenderView;
class pqView;
class vtkSMNewWidgetRepresentationProxy;

/**
 * One DisplaySized implicit-plane widget per vessel endpoint (port 1 preview).
 * Origin = cut position; normal follows the implicit-plane handles (direction
 * is encoded as origin + arm * normal when pushing packed state to the filter).
 *
 * Visibility matches the port-1 representation eye icon in the active view (same idea as
 * pqVESPAOBBInteractiveBoxWidget and the boolean subtract preview).
 */
class pqVESPAEndClipperPlaneHandlesWidget : public pqPropertyGroupWidget
{
    Q_OBJECT
    typedef pqPropertyGroupWidget Superclass;

public:
    pqVESPAEndClipperPlaneHandlesWidget(
        vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqVESPAEndClipperPlaneHandlesWidget() override;

    void select() override;
    void deselect() override;
    void setView(pqView* view) override;

protected:
    void showEvent(QShowEvent* event) override;

private Q_SLOTS:
    void onUseInteractiveToggled(bool on);
    void onPipelineDataUpdated();
    void onPlaneInteraction();
    void onPlaneEndInteraction();
    void onEndpointListCurrentChanged(const QModelIndex& current, const QModelIndex& previous);

private:
    void tearDownPlaneWidgets();
    void rebuildPlaneWidgetsIfNeeded();
    void attachPlaneWidgetsToView();
    void detachPlaneWidgetsFromView();
    void updatePlaneWidgetsVisibility();
    void placePlaneBounds(vtkSMNewWidgetRepresentationProxy* wdg, const double bounds[6]);
    void syncWidgetsFromFilterState();
    void pushPackedFromWidgetsToFilter();
    int  endpointCountFromClipViz(vtkPolyData* viz) const;
    bool readEndpointHandlesFromViz(
        vtkPolyData* viz, int idx, double origin[3], double dirHandle[3]) const;
    void stylePlaneWidget(vtkSMNewWidgetRepresentationProxy* wdg) const;
    void tryConnectEndpointListSelection();
    void disconnectEndpointListSelection();
    void disconnectViewVisibilityLinks();
    /** Port 1 clip preview is shown in \a view; true if there is no port-1 display in that view yet. */
    bool isClipPreviewPort1VisibleInView(pqView* view) const;
    static int ParseEndpointIndexFromArrayRowLabel(const QString& label);

    QCheckBox*                  UseInteractiveCheckbox = nullptr;
    QLabel*                     InfoLabel            = nullptr;
    QPointer<pqPipelineSource>  PipelineSource;
    std::vector<vtkSmartPointer<vtkSMNewWidgetRepresentationProxy>> PlaneWidgets;
    std::vector<unsigned long> PlaneEndInteractionTags;
    std::vector<unsigned long> PlaneInteractionTags;
    QVector<QMetaObject::Connection> EndpointListConnections;
    std::vector<QMetaObject::Connection> ViewVisibilityConnections;
    QPointer<pqArraySelectionWidget> EndpointArrayListWidget;
    int                              ActiveRowEndpointIndex = -1;
    /// Render view that currently hosts HiddenRepresentations for PlaneWidgets (not always == view()).
    QPointer<pqRenderView> LastPlaneHostRenderView;
};

#endif
