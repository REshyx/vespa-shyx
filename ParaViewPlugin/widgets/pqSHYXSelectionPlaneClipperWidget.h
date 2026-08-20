#ifndef pqSHYXSelectionPlaneClipperWidget_h
#define pqSHYXSelectionPlaneClipperWidget_h

#include "pqPropertyGroupWidget.h"

#include "vtkAlgorithm.h"
#include "vtkPolyData.h"
#include "vtkSmartPointer.h"
#include "vtkType.h"
#include "vtkWeakPointer.h"

#include <QPointer>
#include <QShowEvent>
#include <vector>

class QCheckBox;
class QLabel;
class pqOutputPort;
class pqPipelineSource;
class pqRenderView;
class pqSelectionInputWidget;
class pqView;
class vtkDataSet;
class vtkSMNewWidgetRepresentationProxy;
class vtkSMProxy;

/**
 * Single implicit-plane widget for vtkSHYXSelectionPlaneClipper. Copy Active Selection stores the
 * view selection in the Selection text box (the source of truth) and remembers which pipeline
 * node it came from. Show Interactive Cut Plane computes the world-space plane from that copied
 * selection and displays the widget; it does not clip. Apply performs the clip. View selection
 * is not cleared.
 */
class pqSHYXSelectionPlaneClipperWidget : public pqPropertyGroupWidget
{
  Q_OBJECT
  typedef pqPropertyGroupWidget Superclass;

public:
  pqSHYXSelectionPlaneClipperWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXSelectionPlaneClipperWidget() override;

  void select() override;
  void deselect() override;
  void setView(pqView* view) override;

protected:
  void showEvent(QShowEvent* event) override;

private Q_SLOTS:
  void onUseInteractiveToggled(bool on);
  void onCopiedSelectionChanged();
  void onPipelineDataUpdated();
  void onPlaneInteraction();
  void onPlaneEndInteraction();

private:
  void tearDownPlaneWidgets();
  void rebuildPlaneWidgetsIfNeeded();
  void rememberCopiedGeometryProducerFromView();
  void rememberCopiedGeometryProducerFromInput();
  bool computePlaneFromCopiedSelection();
  bool writePackedFromOriginNormal(const double origin[3], const double normal[3]);
  void fillPlanePlaceBounds(double bounds[6]) const;
  /// Selection from the Copy widget / unchecked SM value (Copy does not Apply).
  vtkSMProxy* copiedSelectionProxy() const;
  vtkDataSet* datasetFromCopiedGeometryPort() const;
  bool hasCopiedSelection() const;
  void connectCopiedSelectionWidget();
  void rememberCopiedSelectionIdentity();
  void attachPlaneWidgetsToView();
  void detachPlaneWidgetsFromView();
  void updatePlaneWidgetsVisibility();
  /// Move pick / rotation center to the visible plane origin; camera pose is unchanged.
  void alignPickCenterToVisiblePlane();
  void placePlaneBounds(vtkSMNewWidgetRepresentationProxy* wdg, const double bounds[6]);
  void syncWidgetsFromFilterState();
  void pushPackedFromWidgetsToFilter();
  /// Copy ClipPlaneHint into InteractiveCutPacked when packed is already set or a widget exists,
  /// so a selection-driven recompute is not overwritten by a stale SM packed string on the next Apply.
  void writeClipHintToInteractivePackedIfLocked();
  int  planeHintCountFromOutput(vtkAlgorithm* alg, vtkPolyData* out) const;
  void stylePlaneWidget(vtkSMNewWidgetRepresentationProxy* wdg) const;
  void disconnectViewVisibilityLinks();

  QCheckBox*                 UseInteractiveCheckbox = nullptr;
  QLabel*                    InfoLabel            = nullptr;
  QPointer<pqPipelineSource> PipelineSource;
  vtkSmartPointer<vtkSMNewWidgetRepresentationProxy> PlaneWidget;
  unsigned long              PlaneEndInteractionTag = 0;
  unsigned long              PlaneInteractionTag   = 0;
  std::vector<QMetaObject::Connection> ViewVisibilityConnections;
  QPointer<pqRenderView>     LastPlaneHostRenderView;
  QPointer<pqOutputPort>     LastCopiedGeometryPort;
  vtkWeakPointer<vtkSMProxy> LastCopiedSelectionProxy;
  vtkMTimeType               LastCopiedSelectionMTime = 0;
  unsigned long              SelectionModifiedTag = 0;
  QPointer<pqSelectionInputWidget> CopiedSelectionWidget;
  bool                       RecomputeFromSelectionGuard = false;
  bool                       SuppressSelectionRecompute = false;
  bool                       CopiedSelectionWidgetConnected = false;
};

#endif
