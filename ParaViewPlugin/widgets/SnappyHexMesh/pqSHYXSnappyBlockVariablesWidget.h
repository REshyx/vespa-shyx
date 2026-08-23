#ifndef pqSHYXSnappyBlockVariablesWidget_h
#define pqSHYXSnappyBlockVariablesWidget_h

#include "pqPropertyWidget.h"

#include <QList>
#include <QMap>
#include <QModelIndex>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <vector>

class QStandardItem;
class QStandardItemModel;
class QTreeView;
class pqDataRepresentation;
class pqView;
class vtkDataAssembly;
class vtkEventQtSlotConnect;
class vtkSMPropertyGroup;
class vtkSMStringVectorProperty;

/**
 * Per-block custom variables for vtkSHYXSnappyHexMesh after a successful mesh.
 *
 * Rows come from the OpenFOAM MultiBlock output (internalMesh then patches).
 * Add Variable / Delete Variable columns. Values are stored in BlockNames +
 * BoundaryVariables. Finite cells are written to 0/shyx_BoundaryVariableN.
 * A leading eye toggles that block in the active view (Hide Block).
 */
class pqSHYXSnappyBlockVariablesWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  struct BlockRow
  {
    QString Type;
    QString Name;
    QStringList Variables;
    QString SelectorPath;
    int DataSetIndex = -1;
  };

  pqSHYXSnappyBlockVariablesWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXSnappyBlockVariablesWidget() override;

  bool event(QEvent* e) override;
  void apply() override;
  void reset() override;
  void setView(pqView* view) override;

Q_SIGNALS:
  void blockVariablesChanged();

private Q_SLOTS:
  void onItemChanged(QStandardItem* item);
  void onRefreshClicked();
  void onAddVariableClicked();
  void onDeleteVariableClicked();
  void onOutputDataUpdated();
  void onViewClicked(const QModelIndex& index);
  void onHeaderSectionClicked(int logicalIndex);
  void onBlockVisibilityModified();
  void onActiveViewOrRepresentationChanged();

private:
  void rebuildFromProperty();
  void rebuildRows(const QList<BlockRow>& rows);
  void writeBackProperty();
  void setVariableColumnCount(int count);
  QList<QString> currentNamesFromProperty() const;
  QList<QStringList> currentBoundaryVariablesFromProperty() const;
  QMap<QString, QStringList> variablesByName() const;
  QList<BlockRow> collectCurrentOutputNames() const;

  void connectBlockVisibilityObserver();
  void disconnectBlockVisibilityObserver();
  void updateEyeIcons();
  void toggleRowVisibility(int row);
  void toggleAllVisibility();
  Qt::CheckState headerVisibilityState() const;
  void setBlocksVisible(const QList<int>& rows, bool visible);
  QString selectorForRow(int row) const;
  pqDataRepresentation* currentRepresentation() const;
  vtkSMStringVectorProperty* visibilityProperty(pqDataRepresentation* repr) const;
  vtkDataAssembly* activeAssembly(pqDataRepresentation* repr) const;

  QStandardItemModel* Model = nullptr;
  QTreeView* View = nullptr;
  QString NamesPropertyName;
  QString BoundaryVariablesPropertyName;
  int VariableColumnCount = 1;
  bool UpdatingFromProperty = false;
  bool UpdatingFromUI = false;
  bool UpdatingBlockVisibility = false;
  QPointer<pqDataRepresentation> ObservedRepresentation;
  vtkEventQtSlotConnect* BlockVisibilityVTKConnect = nullptr;
  std::vector<QMetaObject::Connection> RepresentationConnections;
};

#endif
