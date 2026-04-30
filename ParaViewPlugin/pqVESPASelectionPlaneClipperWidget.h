#ifndef pqVESPASelectionPlaneClipperWidget_h
#define pqVESPASelectionPlaneClipperWidget_h

#include "pqPropertyGroupWidget.h"

#include "vtkPolyData.h"
#include "vtkSmartPointer.h"

#include <QPointer>
#include <QShowEvent>
#include <vector>

class QCheckBox;
class QLabel;
class pqPipelineSource;
class pqRenderView;
class pqView;
class vtkSMNewWidgetRepresentationProxy;

/**
 * Single implicit-plane widget for vtkSHYXSelectionPlaneClipper. Plane hint is read from port 0
 * field data array SHYX_SelectionPlaneClipper_PlanePacked (six components per tuple).
 */
class pqVESPASelectionPlaneClipperWidget : public pqPropertyGroupWidget
{
  Q_OBJECT
  typedef pqPropertyGroupWidget Superclass;

public:
  pqVESPASelectionPlaneClipperWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqVESPASelectionPlaneClipperWidget() override;

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

private:
  void tearDownPlaneWidgets();
  void rebuildPlaneWidgetsIfNeeded();
  void attachPlaneWidgetsToView();
  void detachPlaneWidgetsFromView();
  void updatePlaneWidgetsVisibility();
  void placePlaneBounds(vtkSMNewWidgetRepresentationProxy* wdg, const double bounds[6]);
  void syncWidgetsFromFilterState();
  void pushPackedFromWidgetsToFilter();
  int  planeHintCountFromOutput(vtkPolyData* out) const;
  bool readPlaneHandlesFromOutput(vtkPolyData* out, double origin[3], double dirHandle[3]) const;
  void stylePlaneWidget(vtkSMNewWidgetRepresentationProxy* wdg) const;
  void disconnectViewVisibilityLinks();
  bool isOutputPort0VisibleInView(pqView* view) const;

  QCheckBox*                 UseInteractiveCheckbox = nullptr;
  QLabel*                    InfoLabel            = nullptr;
  QPointer<pqPipelineSource> PipelineSource;
  vtkSmartPointer<vtkSMNewWidgetRepresentationProxy> PlaneWidget;
  unsigned long              PlaneEndInteractionTag = 0;
  unsigned long              PlaneInteractionTag   = 0;
  std::vector<QMetaObject::Connection> ViewVisibilityConnections;
  QPointer<pqRenderView>     LastPlaneHostRenderView;
};

#endif
