#ifndef pqSHYXSelectionPatchTableWidget_h
#define pqSHYXSelectionPatchTableWidget_h

#include "pqPropertyWidget.h"

#include <QString>
#include <QStringList>
#include <vector>

#include <vtkType.h>

class QLabel;
class QStandardItem;
class QStandardItemModel;
class pqTreeView;
class vtkSMPropertyGroup;

/**
 * Simplified Partitioned-block-names table for vtkSHYXSelectionAppendPatches:
 * Add current selection as a row (geo_N), rename only. The table keeps every row. Apply merges
 * same-named rows into one output patch (first-seen mark 0, 1, 2, ...).
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
  };

  pqSHYXSelectionPatchTableWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXSelectionPatchTableWidget() override;

  bool event(QEvent* e) override;
  void apply() override;
  void reset() override;

Q_SIGNALS:
  void patchesChanged();

private Q_SLOTS:
  void onItemChanged(QStandardItem* item);
  void onAddFromSelection();
  void onRemoveSelected();

private:
  void rebuildFromProperty();
  void rebuildRows(const QList<PatchRow>& rows);
  void writeBackProperty();
  QList<PatchRow> collectRows() const;
  QStringList linesFromProperty(const QString& propertyName) const;
  int nextPartIndex() const;
  void setStatus(const QString& text, bool error);

  QStandardItemModel* Model = nullptr;
  pqTreeView* View = nullptr;
  QLabel* Status = nullptr;
  QString NamesPropertyName;
  QString CellIdsPropertyName;
  bool UpdatingFromProperty = false;
  bool UpdatingFromUI = false;
};

#endif
