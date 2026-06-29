#ifndef pqSHYXPartitionedBlockNamesWidget_h
#define pqSHYXPartitionedBlockNamesWidget_h

#include "pqPropertyWidget.h"

#include <QList>
#include <QPair>
#include <QString>

class QStandardItem;
class QStandardItemModel;
class QTreeView;
class vtkSMPropertyGroup;

/**
 * Editable block-name table for vtkSHYXDataSetToPartitionedCollection.
 *
 * The widget stores names in a hidden newline-separated StringVectorProperty. The
 * filter applies those names to both vtkCompositeDataSet::NAME() metadata and
 * the IOSS vtkDataAssembly labels during RequestData.
 */
class pqSHYXPartitionedBlockNamesWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
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

private:
  void rebuildFromProperty();
  void rebuildRows(const QList<QPair<QString, QString>>& typedNames);
  void writeBackProperty();
  QList<QString> currentNamesFromProperty() const;
  QList<QPair<QString, QString>> collectCurrentOutputNames() const;

  QStandardItemModel* Model = nullptr;
  QTreeView* View = nullptr;
  QString PropertyName;
  bool UpdatingFromProperty = false;
  bool UpdatingFromUI = false;
};

#endif
