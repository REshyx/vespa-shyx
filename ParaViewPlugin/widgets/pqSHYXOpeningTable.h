#ifndef pqSHYXOpeningTable_h
#define pqSHYXOpeningTable_h

#include "pqPropertyWidget.h"

#include <QString>

class QStandardItem;
class QStandardItemModel;
class QTreeView;
class vtkSMPropertyGroup;

/**
 * 单一表格（Inlet / Remove / Seed point）合并 vtkSHYXVmtkOpeningCenterlines 的两个
 * vtkDataArraySelection（InletStatus / ExcludedStatus）。
 *
 * Server-side 与 XML 完全不变；本 widget 只是把两个 pqArraySelectionWidget 合并为一张 3 列表。
 * 通过 pqArrayListDomain + dynamic Qt property 与 SM 双向绑定，与 pqArraySelectionWidget 同源协议。
 */
class pqSHYXOpeningTable : public pqPropertyWidget
{
    Q_OBJECT
    typedef pqPropertyWidget Superclass;

public:
    pqSHYXOpeningTable(vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXOpeningTable() override;

    bool event(QEvent* e) override;

Q_SIGNALS:
    void inletChanged();
    void excludedChanged();

private Q_SLOTS:
    void onItemChanged(QStandardItem* item);

private:
    void rebuildFromDynamicProperty(const QString& dynPropName);
    void writeBackProperty(const QString& dynPropName);
    void updateRowAppearance(int row);

    QStandardItemModel* Model = nullptr;
    QTreeView* View = nullptr;

    QString InletPropName;
    QString ExcludedPropName;

    bool UpdatingFromDynamicProperty = false;
    bool UpdatingFromUI = false;
};

#endif
