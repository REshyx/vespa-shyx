#ifndef pqSHYXSelectionPatchTableWidget_h
#define pqSHYXSelectionPatchTableWidget_h

#include "pqPropertyWidget.h"

#include <QMetaObject>
#include <QModelIndex>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include <vtkType.h>

class QCheckBox;
class QLabel;
class QMenu;
class QStandardItem;
class QStandardItemModel;
class QToolButton;
class pqDataRepresentation;
class pqTreeView;
class pqView;
class vtkDataAssembly;
class vtkEventQtSlotConnect;
class vtkObject;
class vtkSMPropertyGroup;
class vtkSMProxy;
class vtkSMStringVectorProperty;

struct pqSHYXSelectionPatchShapeHost;

/**
 * Patch table for vtkSHYXSelectionAppendPatches: Add from selection, Celldata, pipeline, or
 * parametric box/sphere. Add from Celldata splits an Input cell-data array into one row per
 * unique value. The table keeps every row. Apply merges same-named rows into one output patch
 * (first-seen mark 0, 1, 2, ...). A leading eye toggles that output patch in the active view via
 * BlockSelectors / BlockVisibilities (same as Hide Block); same-named rows share one block.
 * Apply on Add (default) runs Apply after each Add or Remove so port 0 (added) and port 1
 * (Input minus selection cells) refresh. Box/sphere 3D widgets appear only when Show
 * Interactable widget is checked, that table row is selected, and the filter is visible in the
 * view.
 */
class pqSHYXSelectionPatchTableWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  struct PatchRow
  {
    QString Name;
    QString CellIds;
    QString Kind;
    QString Params;
  };

  pqSHYXSelectionPatchTableWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXSelectionPatchTableWidget() override;

  bool event(QEvent* e) override;
  void apply() override;
  void reset() override;
  void select() override;
  void deselect() override;
  void setView(pqView* view) override;

Q_SIGNALS:
  void patchesChanged();

private Q_SLOTS:
  void onItemChanged(QStandardItem* item);
  void onAddFromSelection();
  void onAddFromCellData(const QString& arrayName);
  void onRemoveSelected();
  void onAddBox();
  void onAddSphere();
  void onPopulatePipelineMenu();
  void onPopulateCellDataMenu();
  void onActiveViewChanged();
  void onShowInteractableToggled(bool on);
  void onTableSelectionChanged();
  void onViewClicked(const QModelIndex& index);
  void onHeaderSectionClicked(int logicalIndex);
  void onBlockVisibilityModified();
  void onActiveViewOrRepresentationChanged();

private:
  void rebuildFromProperty();
  void rebuildRows(const QList<PatchRow>& rows);
  void writeBackProperty();
  QList<PatchRow> collectRows() const;
  QStringList linesFromProperty(const QString& propertyName) const;
  int nextPartIndex() const;
  void setStatus(const QString& text, bool error);
  void applyOutputsIfChecked();
  void appendRow(const PatchRow& row, const QString& okStatus, bool errorStatus = false);
  void appendRows(const QList<PatchRow>& extra, const QString& okStatus, bool errorStatus = false);

  QString normalizedKind(const QString& kind) const;
  QString kindLabel(const QString& kind) const;
  QString infoText(const PatchRow& row) const;
  bool computeShapePlacement(double center[3], double& radius) const;
  void addShapeRow(const QString& kind);
  void addPipelineSource(vtkSMProxy* producer, unsigned int port, const QString& label);
  void removeCustomPatchConnections(const QList<int>& pipelineIndicesDescending);
  bool alreadyHasPipeline(vtkSMProxy* producer, unsigned int port) const;
  QList<int> selectedShapeRowIndices() const;
  bool shapeWidgetsWanted() const;
  bool isSourceVisibleInView(pqView* view) const;
  void connectViewVisibility(pqView* view);
  void disconnectViewVisibilityLinks();

  void connectBlockVisibilityObserver();
  void disconnectBlockVisibilityObserver();
  void updateEyeIcons();
  void toggleRowVisibility(int row);
  void toggleAllVisibility();
  Qt::CheckState headerVisibilityState() const;
  void setBlocksVisible(const QList<int>& rows, bool visible);
  QString outputNameForRow(int row) const;
  QString selectorForRow(int row) const;
  pqDataRepresentation* currentRepresentation() const;
  vtkSMStringVectorProperty* visibilityProperty(pqDataRepresentation* repr) const;
  vtkDataAssembly* activeAssembly(pqDataRepresentation* repr) const;

  void syncShapeWidgets();
  void destroyShapeWidgets();
  void setShapeWidgetsEnabled(bool on);
  void onShapeWidgetEvent(vtkObject* caller, unsigned long eid);
  static void ProcessShapeEvents(
    vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);

  QStandardItemModel* Model = nullptr;
  pqTreeView* View = nullptr;
  QLabel* Status = nullptr;
  QCheckBox* ApplyOnAdd = nullptr;
  QCheckBox* ShowInteractable = nullptr;
  QMenu* PipelineMenu = nullptr;
  QMenu* CellDataMenu = nullptr;
  QString NamesPropertyName;
  QString CellIdsPropertyName;
  QString KindsPropertyName;
  QString ParamsPropertyName;
  bool UpdatingFromProperty = false;
  bool UpdatingFromUI = false;
  bool Applying = false;
  bool ShapeWidgetsVisible = false;
  bool InteractingShape = false;
  std::unique_ptr<pqSHYXSelectionPatchShapeHost> ShapeHost;
  std::vector<QMetaObject::Connection> ViewVisibilityConnections;
  std::vector<QMetaObject::Connection> RepresentationConnections;
  bool UpdatingBlockVisibility = false;
  QPointer<pqDataRepresentation> ObservedRepresentation;
  vtkEventQtSlotConnect* BlockVisibilityVTKConnect = nullptr;
};

#endif
