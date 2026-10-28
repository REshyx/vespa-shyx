#ifndef pqSHYXCameraAdjustWidget_h
#define pqSHYXCameraAdjustWidget_h

#include "pqPropertyWidget.h"

#include <QMetaObject>
#include <QString>
#include <QStringList>

class QLabel;
class QModelIndex;
class QStandardItem;
class QStandardItemModel;
class pqTreeView;
class vtkSMPropertyGroup;
class vtkSMProxy;

/**
 * Camera preset table for vtkSHYXCameraAdjust.
 *
 * Add captures the active RenderView camera into a new row, Update overwrites the selected row
 * from the current camera, Delete removes the selected rows, and Apply sets Position /
 * FocalPoint / ViewUp / ViewAngle / ParallelScale / ParallelProjection on the active RenderView
 * camera. Double-clicking a non-Name cell applies that row.
 *
 * Rows are stored in the source properties (CameraNames, CameraPositions, CameraFocalPoints,
 * CameraViewUps, CameraLens) so the presets are saved with the pipeline object and the state.
 */
class pqSHYXCameraAdjustWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  struct CameraRow
  {
    QString Name;
    double  Position[3]        = { 0.0, 0.0, 0.0 };
    double  FocalPoint[3]      = { 0.0, 0.0, -1.0 };
    double  ViewUp[3]          = { 0.0, 1.0, 0.0 };
    double  ViewAngle          = 30.0;
    double  ParallelScale      = 1.0;
    int     ParallelProjection = 0;
  };

  pqSHYXCameraAdjustWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXCameraAdjustWidget() override;

  bool event(QEvent* e) override;
  void apply() override;
  void reset() override;

Q_SIGNALS:
  void camerasChanged();

private Q_SLOTS:
  void onAdd();
  void onUpdateSelected();
  void onRemoveSelected();
  void onApplySelected();
  void onItemChanged(QStandardItem* item);
  void onRowDoubleClicked(const QModelIndex& index);

private:
  void             rebuildFromProperty();
  void             rebuildRows(const QList<CameraRow>& rows);
  QList<CameraRow> collectRows() const;
  void             writeBackProperty();
  QStringList      linesFromProperty(const QString& propertyName) const;
  int              nextCameraIndex() const;
  int              selectedRow() const;
  void             setStatus(const QString& text, bool error);
  bool             captureCamera(CameraRow& row, QString& error) const;
  bool             applyCamera(const CameraRow& row, QString& error) const;

  QStandardItemModel* Model  = nullptr;
  pqTreeView*         View   = nullptr;
  QLabel*             Status = nullptr;
  QString             NamesPropertyName;
  QString             PositionsPropertyName;
  QString             FocalPointsPropertyName;
  QString             ViewUpsPropertyName;
  QString             LensPropertyName;
  bool                UpdatingFromProperty = false;
  bool                UpdatingFromUI       = false;
  bool                Applying             = false;

private:
  Q_DISABLE_COPY(pqSHYXCameraAdjustWidget)
};

#endif
