#ifndef pqSHYXPolygonSelectionController_h
#define pqSHYXPolygonSelectionController_h

#include <QObject>
#include <QPointer>

#include "vtkSmartPointer.h"
#include "vtkVector.h"

#include <vector>

class pqRenderView;
class pqViewFrame;
class QAction;
class QEvent;
class QWidget;
class vtkActor2D;
class vtkCallbackCommand;
class vtkCoordinate;
class vtkObject;
class vtkPolyData;
class vtkPolyDataMapper2D;

/**
 * Click-vertex polygon selection.
 *
 * Independent of the rectangle rubber band (Select Cells On) and of ParaView's
 * freehand lasso (Select Cells With Polygon). Left-click adds a vertex,
 * right-click removes the last vertex, double-click closes and selects.
 * Right-click the title-bar button to toggle Select through (cells along the
 * view, including ones hidden behind the front surface). Off: visible surface
 * cells only. The mode stays on so another polygon can be drawn. Middle-drag
 * and the wheel keep the normal camera pan / zoom.
 */
class pqSHYXPolygonSelectionController : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXPolygonSelectionController(
    pqRenderView* view, pqViewFrame* frame, QAction* toggleAction, QObject* parent = nullptr);
  ~pqSHYXPolygonSelectionController() override;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
  void onToggled(bool checked);
  void onOtherSelectionToggled(bool checked);

private:
  Q_DISABLE_COPY(pqSHYXPolygonSelectionController)

  static void ProcessEvents(vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);

  void handleInteractorEvent(unsigned long eid);
  bool enablePolygon();
  void disablePolygon();
  void releaseOtherSelectionModes();
  bool otherSelectionModeChecked() const;
  void addPoint(int x, int y);
  void undoLastPoint();
  void closePolygon();
  void selectThroughCells(const std::vector<vtkVector2i>& polygon, int modifier);
  void installToggleActionContextMenu();
  void updateToggleActionTooltip();
  void clearDraft();
  void onEscape();
  void rebuildPreview();
  void renderView();
  int currentSelectionModifier() const;
  bool nearLastPoint(int x, int y) const;

  QPointer<pqRenderView> View;
  QPointer<pqViewFrame> Frame;
  QPointer<QAction> ToggleAction;
  QPointer<QWidget> FilteredWidget;

  vtkSmartPointer<vtkPolyData> Poly;
  vtkSmartPointer<vtkPolyDataMapper2D> Mapper;
  vtkSmartPointer<vtkActor2D> Actor;
  vtkSmartPointer<vtkCoordinate> DisplayCoordinate;
  vtkSmartPointer<vtkCallbackCommand> Observer;

  std::vector<vtkVector2i> Points;
  int Cursor[2] = { 0, 0 };
  bool CursorValid = false;
  bool Enabled = false;
  bool SelectThrough = false;
};

#endif
