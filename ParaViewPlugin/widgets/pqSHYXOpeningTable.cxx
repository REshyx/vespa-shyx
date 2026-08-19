#include "pqSHYXOpeningTable.h"

#include "pqArrayListDomain.h"

#include "vtkSMArraySelectionDomain.h"
#include "vtkSMDomain.h"
#include "vtkSMDomainIterator.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"
#include "vtkSMStringVectorProperty.h"

#include <vtkSmartPointer.h>

#include <QBrush>
#include <QColor>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QList>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>

#include <string>

namespace
{

constexpr int kColInlet = 0;
constexpr int kColRemove = 1;
constexpr int kColName = 2;

constexpr int kRoleOpeningName = Qt::UserRole + 1;

vtkSMArraySelectionDomain* findArraySelectionDomain(vtkSMProperty* prop)
{
    if (!prop)
    {
        return nullptr;
    }
    vtkSmartPointer<vtkSMDomainIterator> iter;
    iter.TakeReference(prop->NewDomainIterator());
    for (iter->Begin(); !iter->IsAtEnd(); iter->Next())
    {
        if (auto* d = vtkSMArraySelectionDomain::SafeDownCast(iter->GetDomain()))
        {
            return d;
        }
    }
    return nullptr;
}

vtkSMProperty* propertyFromGroup(
    vtkSMPropertyGroup* group, vtkSMProxy* proxy, const char* function, const char* fallbackName)
{
    if (group)
    {
        if (auto* p = group->GetProperty(function))
        {
            return p;
        }
        const unsigned int n = group->GetNumberOfProperties();
        for (unsigned int i = 0; i < n; ++i)
        {
            const char* name = group->GetPropertyName(i);
            if (name && fallbackName && std::string(name) == fallbackName)
            {
                return group->GetProperty(i);
            }
        }
    }
    return proxy ? proxy->GetProperty(fallbackName) : nullptr;
}

QString openingNameOfRow(QStandardItemModel* model, int row)
{
    if (auto* item = model->item(row, kColName))
    {
        return item->data(kRoleOpeningName).toString();
    }
    return QString();
}

}

// ---------------------------------------------------------------------------
pqSHYXOpeningTable::pqSHYXOpeningTable(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
    : Superclass(smproxy, parentObject)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(2);

    auto* tip = new QLabel(
        tr("Apply once to populate openings. Inlet = VMTK source seed; Remove = excluded from output and centerline seeds."),
        this);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    vbox->addWidget(tip);

    this->Model = new QStandardItemModel(0, 3, this);
    this->Model->setHorizontalHeaderLabels({ tr("Inlet"), tr("Remove"), tr("Seed point") });

    this->View = new QTreeView(this);
    this->View->setObjectName("SHYXOpeningTable");
    this->View->setRootIsDecorated(false);
    this->View->setAllColumnsShowFocus(true);
    this->View->setUniformRowHeights(true);
    this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->View->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->View->setSortingEnabled(false);
    this->View->setModel(this->Model);

    auto* header = this->View->header();
    header->setSectionResizeMode(kColInlet, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kColRemove, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kColName, QHeaderView::Stretch);
    header->setStretchLastSection(true);

    vbox->addWidget(this->View, 1);

    QObject::connect(this->Model, &QStandardItemModel::itemChanged,
        this, &pqSHYXOpeningTable::onItemChanged);

    // Resolve member properties (XML may use function="Inlet"/"Excluded" or just name).
    vtkSMProperty* inletProp =
        propertyFromGroup(smgroup, smproxy, "Inlet", "InletStatus");
    vtkSMProperty* excludedProp =
        propertyFromGroup(smgroup, smproxy, "Excluded", "ExcludedStatus");

    if (inletProp)
    {
        const char* pname = smproxy ? smproxy->GetPropertyName(inletProp) : nullptr;
        this->InletPropName = QString::fromUtf8(pname ? pname : "InletStatus");

        if (auto* dom = findArraySelectionDomain(inletProp))
        {
            new pqArrayListDomain(this, this->InletPropName, smproxy, inletProp, dom);
        }
        this->addPropertyLink(this, this->InletPropName.toUtf8().data(),
            SIGNAL(inletChanged()), inletProp);
    }

    if (excludedProp)
    {
        const char* pname = smproxy ? smproxy->GetPropertyName(excludedProp) : nullptr;
        this->ExcludedPropName = QString::fromUtf8(pname ? pname : "ExcludedStatus");

        if (auto* dom = findArraySelectionDomain(excludedProp))
        {
            new pqArrayListDomain(this, this->ExcludedPropName, smproxy, excludedProp, dom);
        }
        this->addPropertyLink(this, this->ExcludedPropName.toUtf8().data(),
            SIGNAL(excludedChanged()), excludedProp);
    }

    this->setChangeAvailableAsChangeFinished(true);
}

// ---------------------------------------------------------------------------
pqSHYXOpeningTable::~pqSHYXOpeningTable() = default;

// ---------------------------------------------------------------------------
bool pqSHYXOpeningTable::event(QEvent* e)
{
    if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
    {
        auto* devt = static_cast<QDynamicPropertyChangeEvent*>(e);
        const QString name = QString::fromLatin1(devt->propertyName());
        if (name == this->InletPropName || name == this->ExcludedPropName)
        {
            this->rebuildFromDynamicProperty(name);
            return true;
        }
    }
    return this->Superclass::event(e);
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::rebuildFromDynamicProperty(const QString& dynPropName)
{
    if (!this->Model || dynPropName.isEmpty())
    {
        return;
    }

    QScopedValueRollback<bool> guard(this->UpdatingFromDynamicProperty, true);

    const QVariant v = this->property(dynPropName.toUtf8().data());
    const QList<QList<QVariant>> rows = v.value<QList<QList<QVariant>>>();

    const bool isInletCol = (dynPropName == this->InletPropName);
    const int targetCol = isInletCol ? kColInlet : kColRemove;

    // Index existing rows by opening name to avoid wiping the other column's state.
    QHash<QString, int> rowOf;
    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        rowOf.insert(openingNameOfRow(this->Model, r), r);
    }

    QList<QString> incomingNames;
    incomingNames.reserve(rows.size());

    for (const auto& tuple : rows)
    {
        if (tuple.size() < 2)
        {
            continue;
        }
        const QString name = tuple[0].toString();
        const bool checked = tuple[1].toBool();
        incomingNames.push_back(name);

        int row = rowOf.value(name, -1);
        if (row < 0)
        {
            row = this->Model->rowCount();

            auto* inletItem = new QStandardItem();
            inletItem->setFlags(
                Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren);
            inletItem->setCheckState(Qt::Unchecked);
            inletItem->setTextAlignment(Qt::AlignCenter);

            auto* removeItem = new QStandardItem();
            removeItem->setFlags(
                Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren);
            removeItem->setCheckState(Qt::Unchecked);
            removeItem->setTextAlignment(Qt::AlignCenter);

            auto* nameItem = new QStandardItem(name);
            nameItem->setData(name, kRoleOpeningName);
            nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);

            this->Model->appendRow({ inletItem, removeItem, nameItem });
            rowOf.insert(name, row);
        }

        if (auto* item = this->Model->item(row, targetCol))
        {
            const Qt::CheckState newState = checked ? Qt::Checked : Qt::Unchecked;
            if (item->checkState() != newState)
            {
                item->setCheckState(newState);
            }
        }
    }

    // Drop rows that are no longer present in the incoming property AND not in the other prop.
    // We only know about the *current* dynamic property here; rebuild prunes against incomingNames
    // only when both lists agree. Conservative: keep rows that any column still reports.
    if (!rows.isEmpty())
    {
        QSet<QString> incomingSet(incomingNames.begin(), incomingNames.end());
        for (int r = this->Model->rowCount() - 1; r >= 0; --r)
        {
            const QString rowName = openingNameOfRow(this->Model, r);
            if (!incomingSet.contains(rowName))
            {
                // Only remove if the row is empty on the *other* column too (i.e., unchecked there).
                auto* otherItem = this->Model->item(r, isInletCol ? kColRemove : kColInlet);
                if (otherItem && otherItem->checkState() == Qt::Unchecked)
                {
                    this->Model->removeRow(r);
                }
            }
        }
    }

    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        this->updateRowAppearance(r);
    }
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::onItemChanged(QStandardItem* item)
{
    if (!item || this->UpdatingFromDynamicProperty)
    {
        return;
    }

    const int col = item->column();
    const int row = item->row();
    if (col != kColInlet && col != kColRemove)
    {
        return;
    }

    // Removing an opening implies it cannot also be an inlet → auto-uncheck inlet.
    if (col == kColRemove && item->checkState() == Qt::Checked)
    {
        if (auto* inlet = this->Model->item(row, kColInlet))
        {
            if (inlet->checkState() != Qt::Unchecked)
            {
                QSignalBlocker blocker(this->Model);
                inlet->setCheckState(Qt::Unchecked);
            }
        }
    }

    this->updateRowAppearance(row);

    if (col == kColInlet)
    {
        this->writeBackProperty(this->InletPropName);
    }
    else
    {
        // Remove change may have flipped the inlet column above; push both.
        this->writeBackProperty(this->InletPropName);
        this->writeBackProperty(this->ExcludedPropName);
    }
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::writeBackProperty(const QString& dynPropName)
{
    if (dynPropName.isEmpty())
    {
        return;
    }

    const int col = (dynPropName == this->InletPropName) ? kColInlet : kColRemove;

    QList<QList<QVariant>> rows;
    rows.reserve(this->Model->rowCount());
    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        const QString name = openingNameOfRow(this->Model, r);
        if (name.isEmpty())
        {
            continue;
        }
        auto* item = this->Model->item(r, col);
        const bool checked = item && item->checkState() == Qt::Checked;
        rows.push_back({ QVariant(name), QVariant(checked ? 1 : 0) });
    }

    QVariant value;
    value.setValue(rows);

    {
        QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
        this->setProperty(dynPropName.toUtf8().data(), value);
    }

    if (dynPropName == this->InletPropName)
    {
        Q_EMIT this->inletChanged();
    }
    else
    {
        Q_EMIT this->excludedChanged();
    }
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::updateRowAppearance(int row)
{
    if (!this->Model || row < 0 || row >= this->Model->rowCount())
    {
        return;
    }

    auto* nameItem = this->Model->item(row, kColName);
    auto* inletItem = this->Model->item(row, kColInlet);
    auto* removeItem = this->Model->item(row, kColRemove);
    if (!nameItem || !inletItem || !removeItem)
    {
        return;
    }

    const bool removed = removeItem->checkState() == Qt::Checked;

    // Suppress itemChanged signals so cosmetic updates don't re-trigger writeBack.
    QSignalBlocker blocker(this->Model);

    QFont font = nameItem->font();
    font.setStrikeOut(removed);
    nameItem->setFont(font);

    nameItem->setForeground(removed ? QBrush(QColor(150, 150, 150)) : QBrush());

    Qt::ItemFlags inletFlags = Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren;
    if (!removed)
    {
        inletFlags |= Qt::ItemIsEnabled;
    }
    inletItem->setFlags(inletFlags);
}
