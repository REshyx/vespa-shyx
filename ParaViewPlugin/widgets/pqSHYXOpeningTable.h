#ifndef pqSHYXOpeningTable_h
#define pqSHYXOpeningTable_h

#include "pqPropertyWidget.h"

#include <QList>
#include <QString>
#include <QStringList>

class QAbstractItemDelegate;
class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class vtkSMPropertyGroup;

/**
 * 单一表格（Inlet / Remove / Seed point / Extra_n）合并 vtkSHYXVmtkOpeningCenterlines 的
 * vtkDataArraySelection（InletStatus / ExcludedStatus）以及 ExtraColumnCount / ExtraRoles。
 * 行按 SeedPoint 的 SurfacePointId 升序排列。
 *
 * Inlet / Remove 列表头带三态勾选。Add Extra 追加 Extra_1…Extra_n 列（下拉 —— / in / out），
 * 每列是一组独立的 Extra 多对多 Voronoi 连接。Remove Extra 删掉最后一列 Extra_n。
 *
 * 通过 pqArrayListDomain + dynamic Qt property 与 SM 双向绑定 Inlet/Remove；
 * Extra 用 Q_PROPERTY extraColumnCount / extraRoles。
 */
class pqSHYXOpeningTable : public pqPropertyWidget
{
    Q_OBJECT
    Q_PROPERTY(int extraColumnCount READ extraColumnCount WRITE setExtraColumnCount NOTIFY extraColumnCountChanged)
    Q_PROPERTY(QStringList extraRoles READ extraRoles WRITE setExtraRoles NOTIFY extraRolesChanged)
    typedef pqPropertyWidget Superclass;

public:
    pqSHYXOpeningTable(vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXOpeningTable() override;

    bool event(QEvent* e) override;

    int extraColumnCount() const;
    void setExtraColumnCount(int count);

    QStringList extraRoles() const;
    void setExtraRoles(const QStringList& roles);

Q_SIGNALS:
    void inletChanged();
    void excludedChanged();
    void extraColumnCountChanged();
    void extraRolesChanged();

private Q_SLOTS:
    void onItemChanged(QStandardItem* item);
    void onAddExtra();
    void onRemoveExtra();

private:
    void rebuildFromDynamicProperty(const QString& dynPropName);
    void writeBackProperty(const QString& dynPropName);
    void applyExtraColumnLayout();
    void updateRowAppearance(int row);
    void sortRowsBySeedPointId();
    Qt::CheckState columnCheckState(int col) const;
    bool rowEligibleForColumn(int row, int col) const;
    bool rowHasAnyExtra(int row) const;
    void clearExtraModesOnRow(int row);
    void toggleColumnChecks(int col);
    void refreshHeader();
    void refreshItems();
    void updateExtraButtons();
    int nameColumn() const;
    bool isExtraColumn(int col) const;
    QList<QStandardItem*> makeRowItems(const QString& name) const;

    QStandardItemModel* Model = nullptr;
    QTreeView* View = nullptr;
    QPushButton* AddExtraBtn = nullptr;
    QPushButton* RemoveExtraBtn = nullptr;
    QAbstractItemDelegate* ExtraDelegate = nullptr;

    QString InletPropName;
    QString ExcludedPropName;

    int ExtraColumns = 0;
    QStringList PendingExtraRoles;
    bool HavePendingExtraRoles = false;

    bool UpdatingFromDynamicProperty = false;
    bool UpdatingFromUI = false;
    bool UpdatingFromColumnToggle = false;
};

#endif
