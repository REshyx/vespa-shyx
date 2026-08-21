#ifndef pqSHYXPartitionedBlockNamesWidget_h
#define pqSHYXPartitionedBlockNamesWidget_h

#include "pqPropertyWidget.h"

#include <QList>
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
 * Editable block-name table for vtkSHYXDataSetToPartitionedCollection.
 *
 * The widget stores names in a hidden newline-separated StringVectorProperty. Side-set rows are
 * editable; paired node-set rows mirror the side name with a "node_" prefix. The filter applies
 * those names to both vtkCompositeDataSet::NAME() metadata and the IOSS vtkDataAssembly labels
 * during RequestData.
 *
 * A leading eye icon toggles that block in the active view via the representation's
 * BlockSelectors / BlockVisibilities (the same property as ParaView's Hide Block). The header
 * eye shows or hides every listed block. Side and node rows stay linked: toggling either eye
 * shows or hides the pair together.
 */
class pqSHYXPartitionedBlockNamesWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  struct BlockRow
  {
    QString Type;
    QString Name;
    bool WriteNormal = false;
    QStringList Variables;
    QString SelectorPath;
    int DataSetIndex = -1;
  };

  pqSHYXPartitionedBlockNamesWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXPartitionedBlockNamesWidget() override;

  bool event(QEvent* e) override;
  void apply() override;
  void reset() override;
  void setView(pqView* view) override;

Q_SIGNALS:
  void blockNamesChanged();

private Q_SLOTS:
  void onItemChanged(QStandardItem* item);
  void onRefreshClicked();
  void onAddVariableClicked();
  void onDeleteVariableClicked();
  void onViewClicked(const QModelIndex& index);
  void onHeaderSectionClicked(int logicalIndex);
  void onBlockVisibilityModified();
  void onActiveViewOrRepresentationChanged();

private:
  void rebuildFromProperty();
  void rebuildRows(const QList<BlockRow>& rows);
  void writeBackProperty();
  void syncNodeRowsFromSideRows();
  void setVariableColumnCount(int count);
  QList<QString> currentNamesFromProperty() const;
  QList<QStringList> currentBoundaryVariablesFromProperty() const;
  QList<int> currentBoundaryWriteNormalsFromProperty() const;
  QList<BlockRow> collectCurrentOutputNames() const;

  void connectBlockVisibilityObserver();
  void disconnectBlockVisibilityObserver();
  void updateEyeIcons();
  void toggleRowVisibility(int row);
  void toggleAllVisibility();
  bool allRowsVisible() const;
  void setBlocksVisible(const QList<int>& rows, bool visible);
  int pairedRow(int row) const;
  QString selectorForRow(int row) const;
  pqDataRepresentation* currentRepresentation() const;
  vtkSMStringVectorProperty* visibilityProperty(pqDataRepresentation* repr) const;
  vtkDataAssembly* activeAssembly(pqDataRepresentation* repr) const;

  QStandardItemModel* Model = nullptr;
  QTreeView* View = nullptr;
  QString NamesPropertyName;
  QString BoundaryVariablesPropertyName;
  QString BoundaryWriteNormalsPropertyName;
  int VariableColumnCount = 1;
  bool UpdatingFromProperty = false;
  bool UpdatingFromUI = false;
  bool UpdatingBlockVisibility = false;
  QPointer<pqDataRepresentation> ObservedRepresentation;
  vtkEventQtSlotConnect* BlockVisibilityVTKConnect = nullptr;
  std::vector<QMetaObject::Connection> RepresentationConnections;
};

#endif
