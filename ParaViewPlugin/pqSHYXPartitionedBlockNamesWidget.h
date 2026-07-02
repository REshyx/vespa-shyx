#ifndef pqSHYXPartitionedBlockNamesWidget_h
#define pqSHYXPartitionedBlockNamesWidget_h

#include "pqPropertyWidget.h"

#include <QList>
#include <QString>
#include <QStringList>

class QStandardItem;
class QStandardItemModel;
class QTreeView;
class vtkSMPropertyGroup;

/**
 * Editable block-name table for vtkSHYXDataSetToPartitionedCollection.
 *
 * The widget stores names in a hidden newline-separated StringVectorProperty. Side-set rows are
 * editable; paired node-set rows mirror the side name with a "node_" prefix. The filter applies
 * those names to both vtkCompositeDataSet::NAME() metadata and the IOSS vtkDataAssembly labels
 * during RequestData.
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
  };

  pqSHYXPartitionedBlockNamesWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXPartitionedBlockNamesWidget() override;

  bool event(QEvent* e) override;
  void apply() override;
  void reset() override;

Q_SIGNALS:
  void blockNamesChanged();

private Q_SLOTS:
  void onItemChanged(QStandardItem* item);
  void onRefreshClicked();
  void onAddVariableClicked();
  void onDeleteVariableClicked();

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

  QStandardItemModel* Model = nullptr;
  QTreeView* View = nullptr;
  QString NamesPropertyName;
  QString BoundaryVariablesPropertyName;
  QString BoundaryWriteNormalsPropertyName;
  int VariableColumnCount = 1;
  bool UpdatingFromProperty = false;
  bool UpdatingFromUI = false;
};

#endif
