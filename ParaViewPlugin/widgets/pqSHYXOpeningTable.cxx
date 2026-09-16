#include "pqSHYXOpeningTable.h"

#include "pqArrayListDomain.h"

#include "vtkSMArraySelectionDomain.h"
#include "vtkSMDomain.h"
#include "vtkSMDomainIterator.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"

#include <vtkSmartPointer.h>

#include <QAbstractItemDelegate>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionHeader>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace
{

constexpr int kColInlet = 0;
constexpr int kColRemove = 1;
constexpr int kColName = 2;
constexpr int kColFirstExtra = 3;
constexpr int kMaxExtraCols = 16;

constexpr int kRoleOpeningName = Qt::UserRole + 1;
constexpr int kRoleExtraMode = Qt::UserRole + 2;

constexpr int kExtraRemove = 0;
constexpr int kExtraInlet = 1;
constexpr int kExtraOutlet = 2;

QString extraModeText(int mode)
{
    switch (mode)
    {
        case kExtraInlet:
            return QStringLiteral("in");
        case kExtraOutlet:
            return QStringLiteral("out");
        default:
            return QStringLiteral("——");
    }
}

void setExtraMode(QStandardItem* item, int mode)
{
    if (!item)
    {
        return;
    }
    if (mode != kExtraInlet && mode != kExtraOutlet)
    {
        mode = kExtraRemove;
    }
    item->setData(mode, kRoleExtraMode);
    item->setText(extraModeText(mode));
}

int extraModeOf(QStandardItem* item)
{
    if (!item)
    {
        return kExtraRemove;
    }
    const QVariant v = item->data(kRoleExtraMode);
    if (v.isValid())
    {
        const int mode = v.toInt();
        if (mode == kExtraInlet || mode == kExtraOutlet || mode == kExtraRemove)
        {
            return mode;
        }
    }
    return kExtraRemove;
}

QStandardItem* makeExtraItem(int mode = kExtraRemove)
{
    auto* extraItem = new QStandardItem(extraModeText(mode));
    extraItem->setData(mode, kRoleExtraMode);
    extraItem->setFlags(
        Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemNeverHasChildren);
    extraItem->setTextAlignment(Qt::AlignCenter);
    return extraItem;
}

class ExtraAddDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
        const QModelIndex&) const override
    {
        auto* cb = new QComboBox(parent);
        cb->addItem(QStringLiteral("——"), kExtraRemove);
        cb->addItem(QStringLiteral("in"), kExtraInlet);
        cb->addItem(QStringLiteral("out"), kExtraOutlet);
        QObject::connect(cb, QOverload<int>::of(&QComboBox::activated), cb, [this, cb]() {
            auto* self = const_cast<ExtraAddDelegate*>(this);
            Q_EMIT self->commitData(cb);
            Q_EMIT self->closeEditor(cb, QAbstractItemDelegate::NoHint);
        });
        return cb;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* cb = qobject_cast<QComboBox*>(editor);
        if (!cb)
        {
            return;
        }
        const int mode = index.data(kRoleExtraMode).toInt();
        const int idx = cb->findData(mode);
        cb->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
        override
    {
        auto* cb = qobject_cast<QComboBox*>(editor);
        if (!cb || !model)
        {
            return;
        }
        model->setData(index, cb->currentData().toInt(), kRoleExtraMode);
        model->setData(index, cb->currentText(), Qt::DisplayRole);
    }
};

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
    if (!model || row < 0 || row >= model->rowCount())
    {
        return QString();
    }
    if (auto* item = model->item(row, kColName))
    {
        return item->data(kRoleOpeningName).toString();
    }
    return QString();
}

qint64 seedPointIdFromLabel(const QString& name)
{
    static const QString prefix = QStringLiteral("SeedPoint: ");
    if (!name.startsWith(prefix))
    {
        return std::numeric_limits<qint64>::max();
    }

    int begin = prefix.size();
    int end = begin;
    if (end < name.size() && (name[end] == QLatin1Char('+') || name[end] == QLatin1Char('-')))
    {
        ++end;
    }
    while (end < name.size() && name[end].isDigit())
    {
        ++end;
    }
    if (end <= begin || (end == begin + 1 && !name[begin].isDigit()))
    {
        return std::numeric_limits<qint64>::max();
    }
    return name.mid(begin, end - begin).toLongLong();
}

int duplicateSuffixFromLabel(const QString& name)
{
    const int hash = name.lastIndexOf(QLatin1Char('#'));
    if (hash < 0)
    {
        return 1;
    }
    bool ok = false;
    const int n = name.mid(hash + 1).trimmed().toInt(&ok);
    return ok ? n : 1;
}

bool isCheckableOpeningColumn(int logicalIndex)
{
    return logicalIndex == kColInlet || logicalIndex == kColRemove;
}

/**
 * Horizontal header that paints a tri-state checkbox on Inlet and Remove.
 * pqHeaderView only tracks one checkbox rect, so two check columns need this.
 */
class OpeningTableHeaderView : public QHeaderView
{
public:
    std::function<Qt::CheckState(int)> GetState;
    std::function<void(int)> ToggleColumn;

    explicit OpeningTableHeaderView(QWidget* parentObject)
        : QHeaderView(Qt::Horizontal, parentObject)
    {
        this->setHighlightSections(false);
        this->setSectionsClickable(false);
    }

protected:
    QSize sectionSizeFromContents(int logicalIndex) const override
    {
        QSize sz = QHeaderView::sectionSizeFromContents(logicalIndex);
        if (!isCheckableOpeningColumn(logicalIndex))
        {
            return sz;
        }

        QStyleOptionViewItem option;
        option.initFrom(this);
        option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
        option.viewItemPosition = QStyleOptionViewItem::OnlyOne;
        const QRect checkRect =
            this->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, this);
        const QRect textRect =
            this->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, this);
        sz.rwidth() += qMax(textRect.x(), checkRect.width());
        sz.setHeight(qMax(sz.height(), checkRect.height() + 4));
        return sz;
    }

    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override
    {
        if (!rect.isValid())
        {
            return;
        }
        if (!isCheckableOpeningColumn(logicalIndex) || !this->GetState)
        {
            this->QHeaderView::paintSection(painter, rect, logicalIndex);
            this->CheckRects.remove(logicalIndex);
            return;
        }

        QStyleOptionHeader hoption;
        this->initStyleOption(&hoption);
        hoption.section = logicalIndex;
        hoption.rect = rect;
        this->style()->drawControl(QStyle::CE_HeaderSection, &hoption, painter, this);

        QStyleOptionViewItem coption;
        coption.initFrom(this);
        coption.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
        coption.viewItemPosition = QStyleOptionViewItem::OnlyOne;
        QRect checkRect =
            this->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &coption, this);
        checkRect.moveLeft(rect.x() + checkRect.x());
        checkRect.moveTop(rect.y() + (rect.height() - checkRect.height()) / 2);
        coption.rect = checkRect;
        coption.state = coption.state & ~QStyle::State_HasFocus;
        switch (this->GetState(logicalIndex))
        {
            case Qt::Checked:
                coption.state |= QStyle::State_On;
                break;
            case Qt::PartiallyChecked:
                coption.state |= QStyle::State_NoChange;
                break;
            case Qt::Unchecked:
            default:
                coption.state |= QStyle::State_Off;
                break;
        }
        this->style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &coption, painter, this);
        this->CheckRects[logicalIndex] = checkRect;

        QStyleOptionViewItem vioption;
        vioption.initFrom(this);
        vioption.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
        vioption.viewItemPosition = QStyleOptionViewItem::OnlyOne;
        const QRect textPad =
            this->style()->subElementRect(QStyle::SE_ItemViewItemText, &vioption, this);
        const int textOffset = qMax(textPad.x(), checkRect.width() + 2);

        QRect labelRect = rect;
        labelRect.setLeft(labelRect.x() + textOffset);
        painter->save();
        this->QHeaderView::paintSection(painter, labelRect, logicalIndex);
        painter->restore();
    }

    void mousePressEvent(QMouseEvent* evt) override
    {
        this->PressPosition = evt->pos();
        if (!this->checkboxAt(evt->pos()).isValid())
        {
            this->QHeaderView::mousePressEvent(evt);
        }
    }

    void mouseReleaseEvent(QMouseEvent* evt) override
    {
        const bool wasClick = (evt->pos() - this->PressPosition).manhattanLength() < 3;
        const QRect checkRect = this->checkboxAt(this->PressPosition);
        this->PressPosition = QPoint();
        if (evt->button() == Qt::LeftButton && wasClick && checkRect.isValid() && this->ToggleColumn)
        {
            const int logicalIndex = this->logicalIndexAt(checkRect.center());
            if (isCheckableOpeningColumn(logicalIndex))
            {
                this->ToggleColumn(logicalIndex);
                return;
            }
        }
        this->QHeaderView::mouseReleaseEvent(evt);
    }

private:
    QRect checkboxAt(const QPoint& pos) const
    {
        const int logicalIndex = this->logicalIndexAt(pos);
        const auto it = this->CheckRects.constFind(logicalIndex);
        if (it != this->CheckRects.cend() && it->contains(pos))
        {
            return *it;
        }
        return QRect();
    }

    QPoint PressPosition;
    mutable QHash<int, QRect> CheckRects;
};

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
        tr("Apply once to populate openings. Inlet = VMTK tree source; unchecked = tree outlet. "
           "Remove = excluded from seeds. Add Extra appends Extra_1, Extra_2, … columns "
           "(—— / in / out); each Extra_n is an independent many-to-many extra Voronoi group. "
           "Remove Extra drops the last Extra_n. Header checkboxes select a whole column."),
        this);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    vbox->addWidget(tip);

    this->Model = new QStandardItemModel(0, 3, this);

    this->View = new QTreeView(this);
    this->View->setObjectName("SHYXOpeningTable");
    this->View->setRootIsDecorated(false);
    this->View->setAllColumnsShowFocus(true);
    this->View->setUniformRowHeights(true);
    this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->View->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::DoubleClicked |
        QAbstractItemView::EditKeyPressed);
    this->View->setSortingEnabled(false);
    this->View->setModel(this->Model);
    this->ExtraDelegate = new ExtraAddDelegate(this->View);

    auto* header = new OpeningTableHeaderView(this->View);
    header->GetState = [this](int col) { return this->columnCheckState(col); };
    header->ToggleColumn = [this](int col) { this->toggleColumnChecks(col); };
    this->View->setHeader(header);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setStretchLastSection(true);

    vbox->addWidget(this->View, 1);

    auto* extraBtns = new QHBoxLayout();
    extraBtns->setContentsMargins(0, 0, 0, 0);
    extraBtns->setSpacing(4);
    this->AddExtraBtn = new QPushButton(tr("Add Extra"), this);
    this->AddExtraBtn->setToolTip(tr(
        "Append Extra_n. Each Extra_n column is one extra Voronoi group: every in in that "
        "column connects to every out in the same column, reusing the cached Delaunay/Voronoi."));
    this->RemoveExtraBtn = new QPushButton(tr("Remove Extra"), this);
    this->RemoveExtraBtn->setToolTip(tr("Remove the last Extra_n column."));
    extraBtns->addWidget(this->AddExtraBtn);
    extraBtns->addWidget(this->RemoveExtraBtn);
    extraBtns->addStretch(1);
    vbox->addLayout(extraBtns);

    this->applyExtraColumnLayout();

    QObject::connect(this->Model, &QStandardItemModel::itemChanged,
        this, &pqSHYXOpeningTable::onItemChanged);
    QObject::connect(this->AddExtraBtn, &QPushButton::clicked, this, &pqSHYXOpeningTable::onAddExtra);
    QObject::connect(
        this->RemoveExtraBtn, &QPushButton::clicked, this, &pqSHYXOpeningTable::onRemoveExtra);

    vtkSMProperty* inletProp = propertyFromGroup(smgroup, smproxy, "Inlet", "InletStatus");
    vtkSMProperty* excludedProp = propertyFromGroup(smgroup, smproxy, "Excluded", "ExcludedStatus");
    vtkSMProperty* extraCountProp =
        propertyFromGroup(smgroup, smproxy, "ExtraColumnCount", "ExtraColumnCount");
    vtkSMProperty* extraRolesProp = propertyFromGroup(smgroup, smproxy, "ExtraRoles", "ExtraRoles");

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

    if (extraCountProp)
    {
        this->addPropertyLink(
            this, "extraColumnCount", SIGNAL(extraColumnCountChanged()), extraCountProp);
    }
    if (extraRolesProp)
    {
        this->addPropertyLink(this, "extraRoles", SIGNAL(extraRolesChanged()), extraRolesProp);
    }

    this->setChangeAvailableAsChangeFinished(true);
}

// ---------------------------------------------------------------------------
pqSHYXOpeningTable::~pqSHYXOpeningTable() = default;

// ---------------------------------------------------------------------------
int pqSHYXOpeningTable::nameColumn() const
{
    return kColName;
}

bool pqSHYXOpeningTable::isExtraColumn(int col) const
{
    return col >= kColFirstExtra && col < kColFirstExtra + this->ExtraColumns;
}

int pqSHYXOpeningTable::extraColumnCount() const
{
    return this->ExtraColumns;
}

void pqSHYXOpeningTable::setExtraColumnCount(int count)
{
    if (count < 0)
    {
        count = 0;
    }
    if (count > kMaxExtraCols)
    {
        count = kMaxExtraCols;
    }
    if (this->ExtraColumns == count && this->Model &&
        this->Model->columnCount() == kColFirstExtra + this->ExtraColumns)
    {
        this->updateExtraButtons();
        return;
    }
    this->ExtraColumns = count;
    this->applyExtraColumnLayout();
}

QList<QStandardItem*> pqSHYXOpeningTable::makeRowItems(const QString& name) const
{
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

    QList<QStandardItem*> items;
    items << inletItem << removeItem;

    auto* nameItem = new QStandardItem(name);
    nameItem->setData(name, kRoleOpeningName);
    nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    items << nameItem;

    for (int e = 0; e < this->ExtraColumns; ++e)
    {
        items << makeExtraItem();
    }
    return items;
}

void pqSHYXOpeningTable::applyExtraColumnLayout()
{
    if (!this->Model)
    {
        return;
    }

    QSignalBlocker blocker(this->Model);

    struct SavedRow
    {
        Qt::CheckState Inlet = Qt::Unchecked;
        Qt::CheckState Remove = Qt::Unchecked;
        QList<int> Extras;
        QString Name;
    };
    QList<SavedRow> saved;
    saved.reserve(this->Model->rowCount());
    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        SavedRow row;
        if (auto* inlet = this->Model->item(r, kColInlet))
        {
            row.Inlet = inlet->checkState();
        }
        if (auto* remove = this->Model->item(r, kColRemove))
        {
            row.Remove = remove->checkState();
        }
        const int oldExtraCount = qMax(0, this->Model->columnCount() - kColFirstExtra);
        for (int e = 0; e < oldExtraCount; ++e)
        {
            row.Extras.push_back(extraModeOf(this->Model->item(r, kColFirstExtra + e)));
        }
        row.Name = openingNameOfRow(this->Model, r);
        saved.push_back(row);
    }

    this->Model->removeRows(0, this->Model->rowCount());
    this->Model->setColumnCount(kColFirstExtra + this->ExtraColumns);

    auto* inletHeader = new QStandardItem(tr("Inlet"));
    inletHeader->setToolTip(tr(
        "Check openings that are inlets (VMTK source seeds). Click the header checkbox to select all, or to clear all when every eligible inlet is already checked. A mixed column shows a partial check."));
    auto* removeHeader = new QStandardItem(tr("Remove"));
    removeHeader->setToolTip(tr(
        "Check openings to exclude from output seeds and centerline sources. Click the header checkbox to select all, or to clear all when every opening is already removed. A mixed column shows a partial check."));
    auto* nameHeader = new QStandardItem(tr("Seed point"));
    this->Model->setHorizontalHeaderItem(kColInlet, inletHeader);
    this->Model->setHorizontalHeaderItem(kColRemove, removeHeader);
    this->Model->setHorizontalHeaderItem(kColName, nameHeader);
    for (int e = 0; e < this->ExtraColumns; ++e)
    {
        auto* extraHeader = new QStandardItem(QStringLiteral("Extra_%1").arg(e + 1));
        extraHeader->setToolTip(tr(
            "Extra Voronoi traces after the inlet/outlet tree. Default —— (not extra). "
            "in / out in this column: every Extra_%1 in is connected to every Extra_%1 out "
            "(many-to-many), reusing the cached Delaunay/Voronoi.")
                .arg(e + 1));
        this->Model->setHorizontalHeaderItem(kColFirstExtra + e, extraHeader);
    }

    for (const SavedRow& row : saved)
    {
        auto items = this->makeRowItems(row.Name);
        if (items.size() >= 2)
        {
            items[kColInlet]->setCheckState(row.Inlet);
            items[kColRemove]->setCheckState(row.Remove);
        }
        for (int e = 0; e < this->ExtraColumns; ++e)
        {
            const int mode = (e < row.Extras.size()) ? row.Extras[e] : kExtraRemove;
            setExtraMode(items[kColFirstExtra + e], mode);
        }
        this->Model->appendRow(items);
    }

    if (this->View)
    {
        for (int c = 0; c < this->Model->columnCount(); ++c)
        {
            this->View->setItemDelegateForColumn(c, nullptr);
        }
        for (int e = 0; e < this->ExtraColumns; ++e)
        {
            this->View->setItemDelegateForColumn(kColFirstExtra + e, this->ExtraDelegate);
        }
        if (auto* header = this->View->header())
        {
            header->setSectionResizeMode(kColInlet, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(kColRemove, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(kColName, QHeaderView::Stretch);
            for (int e = 0; e < this->ExtraColumns; ++e)
            {
                header->setSectionResizeMode(kColFirstExtra + e, QHeaderView::ResizeToContents);
            }
            header->setStretchLastSection(this->ExtraColumns == 0);
        }
    }

    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        this->updateRowAppearance(r);
    }
    this->updateExtraButtons();
    this->refreshHeader();
}

void pqSHYXOpeningTable::updateExtraButtons()
{
    if (this->AddExtraBtn)
    {
        this->AddExtraBtn->setEnabled(this->ExtraColumns < kMaxExtraCols);
    }
    if (this->RemoveExtraBtn)
    {
        this->RemoveExtraBtn->setEnabled(this->ExtraColumns > 0);
    }
}

void pqSHYXOpeningTable::onAddExtra()
{
    if (this->ExtraColumns >= kMaxExtraCols)
    {
        return;
    }
    ++this->ExtraColumns;
    this->applyExtraColumnLayout();
    Q_EMIT this->extraColumnCountChanged();
}

void pqSHYXOpeningTable::onRemoveExtra()
{
    if (this->ExtraColumns <= 0)
    {
        return;
    }
    --this->ExtraColumns;
    this->applyExtraColumnLayout();
    Q_EMIT this->extraColumnCountChanged();
    Q_EMIT this->extraRolesChanged();
}

QStringList pqSHYXOpeningTable::extraRoles() const
{
    QStringList roles;
    if (!this->Model)
    {
        return roles;
    }
    for (int e = 0; e < this->ExtraColumns; ++e)
    {
        const int col = kColFirstExtra + e;
        for (int r = 0; r < this->Model->rowCount(); ++r)
        {
            const int mode = extraModeOf(this->Model->item(r, col));
            if (mode != kExtraInlet && mode != kExtraOutlet)
            {
                continue;
            }
            const QString name = openingNameOfRow(this->Model, r);
            if (name.isEmpty())
            {
                continue;
            }
            roles << QString::number(e) << name << extraModeText(mode);
        }
    }
    return roles;
}

void pqSHYXOpeningTable::setExtraRoles(const QStringList& roles)
{
    if (!this->Model || this->Model->rowCount() == 0)
    {
        this->PendingExtraRoles = roles;
        this->HavePendingExtraRoles = true;
        return;
    }
    this->HavePendingExtraRoles = false;
    this->PendingExtraRoles.clear();
    QScopedValueRollback<bool> guard(this->UpdatingFromDynamicProperty, true);
    QSignalBlocker blocker(this->Model);

    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        this->clearExtraModesOnRow(r);
    }

    QHash<QString, int> rowOf;
    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        rowOf.insert(openingNameOfRow(this->Model, r), r);
    }

    for (int i = 0; i + 2 < roles.size(); i += 3)
    {
        bool ok = false;
        const int extraIdx = roles[i].toInt(&ok);
        if (!ok || extraIdx < 0 || extraIdx >= this->ExtraColumns)
        {
            continue;
        }
        const QString name = roles[i + 1];
        const QString role = roles[i + 2];
        int mode = kExtraRemove;
        if (role == QLatin1String("in"))
        {
            mode = kExtraInlet;
        }
        else if (role == QLatin1String("out"))
        {
            mode = kExtraOutlet;
        }
        else
        {
            continue;
        }
        const int row = rowOf.value(name, -1);
        if (row < 0)
        {
            continue;
        }
        setExtraMode(this->Model->item(row, kColFirstExtra + extraIdx), mode);
    }

    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        this->updateRowAppearance(r);
    }
}

bool pqSHYXOpeningTable::rowHasAnyExtra(int row) const
{
    for (int e = 0; e < this->ExtraColumns; ++e)
    {
        if (extraModeOf(this->Model->item(row, kColFirstExtra + e)) != kExtraRemove)
        {
            return true;
        }
    }
    return false;
}

void pqSHYXOpeningTable::clearExtraModesOnRow(int row)
{
    for (int e = 0; e < this->ExtraColumns; ++e)
    {
        if (auto* extraItem = this->Model->item(row, kColFirstExtra + e))
        {
            setExtraMode(extraItem, kExtraRemove);
        }
    }
}

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
            this->Model->appendRow(this->makeRowItems(name));
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

    if (!rows.isEmpty())
    {
        QSet<QString> incomingSet(incomingNames.begin(), incomingNames.end());
        for (int r = this->Model->rowCount() - 1; r >= 0; --r)
        {
            const QString rowName = openingNameOfRow(this->Model, r);
            if (!incomingSet.contains(rowName))
            {
                auto* otherItem = this->Model->item(r, isInletCol ? kColRemove : kColInlet);
                if (otherItem && otherItem->checkState() == Qt::Unchecked && !this->rowHasAnyExtra(r))
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

    this->sortRowsBySeedPointId();
    this->refreshHeader();
    if (this->HavePendingExtraRoles)
    {
        const QStringList pending = this->PendingExtraRoles;
        this->HavePendingExtraRoles = false;
        this->PendingExtraRoles.clear();
        this->setExtraRoles(pending);
    }
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::onItemChanged(QStandardItem* item)
{
    if (!item || this->UpdatingFromDynamicProperty || this->UpdatingFromColumnToggle)
    {
        return;
    }

    const int col = item->column();
    const int row = item->row();
    if (col != kColInlet && col != kColRemove && !this->isExtraColumn(col))
    {
        return;
    }

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
        QSignalBlocker blocker(this->Model);
        this->clearExtraModesOnRow(row);
    }

    if (this->isExtraColumn(col) && extraModeOf(item) != kExtraRemove)
    {
        if (auto* removeItem = this->Model->item(row, kColRemove))
        {
            if (removeItem->checkState() != Qt::Unchecked)
            {
                QSignalBlocker blocker(this->Model);
                removeItem->setCheckState(Qt::Unchecked);
            }
        }
    }

    this->updateRowAppearance(row);
    this->refreshHeader();

    if (col == kColInlet)
    {
        this->writeBackProperty(this->InletPropName);
    }
    else if (this->isExtraColumn(col))
    {
        Q_EMIT this->extraRolesChanged();
        this->writeBackProperty(this->ExcludedPropName);
    }
    else
    {
        this->writeBackProperty(this->InletPropName);
        this->writeBackProperty(this->ExcludedPropName);
        Q_EMIT this->extraRolesChanged();
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
    else if (dynPropName == this->ExcludedPropName)
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

    auto* nameItem = this->Model->item(row, this->nameColumn());
    auto* inletItem = this->Model->item(row, kColInlet);
    auto* removeItem = this->Model->item(row, kColRemove);
    if (!nameItem || !inletItem || !removeItem)
    {
        return;
    }

    const bool removed = removeItem->checkState() == Qt::Checked;

    QSignalBlocker blocker(this->Model);

    QFont font = nameItem->font();
    font.setStrikeOut(removed);
    nameItem->setFont(font);
    nameItem->setForeground(removed ? QBrush(QColor(150, 150, 150)) : QBrush());

    Qt::ItemFlags inletFlags = Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren;
    Qt::ItemFlags extraFlags = Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
    if (!removed)
    {
        inletFlags |= Qt::ItemIsEnabled;
        extraFlags |= Qt::ItemIsEnabled | Qt::ItemIsEditable;
    }
    else
    {
        this->clearExtraModesOnRow(row);
    }
    inletItem->setFlags(inletFlags);
    for (int e = 0; e < this->ExtraColumns; ++e)
    {
        if (auto* extraItem = this->Model->item(row, kColFirstExtra + e))
        {
            extraItem->setFlags(extraFlags);
        }
    }
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::sortRowsBySeedPointId()
{
    if (!this->Model)
    {
        return;
    }

    const int n = this->Model->rowCount();
    if (n <= 1)
    {
        return;
    }

    struct Row
    {
        qint64 sid = 0;
        int suffix = 1;
        QList<QStandardItem*> items;
    };

    std::vector<Row> rows(static_cast<size_t>(n));

    {
        QSignalBlocker blocker(this->Model);
        for (int r = n - 1; r >= 0; --r)
        {
            Row& row = rows[static_cast<size_t>(r)];
            const QString name = openingNameOfRow(this->Model, r);
            row.sid = seedPointIdFromLabel(name);
            row.suffix = duplicateSuffixFromLabel(name);
            row.items = this->Model->takeRow(r);
        }

        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.sid != b.sid)
            {
                return a.sid < b.sid;
            }
            return a.suffix < b.suffix;
        });

        for (Row& row : rows)
        {
            this->Model->appendRow(row.items);
        }
    }
}

// ---------------------------------------------------------------------------
bool pqSHYXOpeningTable::rowEligibleForColumn(int row, int col) const
{
    if (!this->Model || row < 0 || row >= this->Model->rowCount())
    {
        return false;
    }
    if (col == kColRemove)
    {
        return true;
    }
    if (col != kColInlet)
    {
        return false;
    }
    auto* removeItem = this->Model->item(row, kColRemove);
    return !removeItem || removeItem->checkState() != Qt::Checked;
}

// ---------------------------------------------------------------------------
Qt::CheckState pqSHYXOpeningTable::columnCheckState(int col) const
{
    if (!this->Model || (col != kColInlet && col != kColRemove))
    {
        return Qt::Unchecked;
    }

    int eligible = 0;
    int checked = 0;
    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        if (!this->rowEligibleForColumn(r, col))
        {
            continue;
        }
        ++eligible;
        auto* item = this->Model->item(r, col);
        if (item && item->checkState() == Qt::Checked)
        {
            ++checked;
        }
    }

    if (eligible == 0 || checked == 0)
    {
        return Qt::Unchecked;
    }
    if (checked == eligible)
    {
        return Qt::Checked;
    }
    return Qt::PartiallyChecked;
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::toggleColumnChecks(int col)
{
    if (!this->Model || (col != kColInlet && col != kColRemove))
    {
        return;
    }

    const bool checkAll = this->columnCheckState(col) != Qt::Checked;
    const Qt::CheckState newState = checkAll ? Qt::Checked : Qt::Unchecked;

    bool anyChanged = false;
    {
        QScopedValueRollback<bool> guard(this->UpdatingFromColumnToggle, true);
        for (int r = 0; r < this->Model->rowCount(); ++r)
        {
            if (!this->rowEligibleForColumn(r, col))
            {
                continue;
            }
            auto* item = this->Model->item(r, col);
            if (!item || item->checkState() == newState)
            {
                continue;
            }
            anyChanged = true;
            item->setCheckState(newState);
            if (col == kColRemove && checkAll)
            {
                if (auto* inlet = this->Model->item(r, kColInlet))
                {
                    inlet->setCheckState(Qt::Unchecked);
                }
                this->clearExtraModesOnRow(r);
            }
            this->updateRowAppearance(r);
        }
    }

    if (!anyChanged)
    {
        this->refreshHeader();
        return;
    }

    this->writeBackProperty(this->InletPropName);
    this->writeBackProperty(this->ExcludedPropName);
    Q_EMIT this->extraRolesChanged();
    this->refreshHeader();
    this->refreshItems();
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::refreshHeader()
{
    if (this->View && this->View->header() && this->View->header()->viewport())
    {
        this->View->header()->viewport()->update();
    }
}

// ---------------------------------------------------------------------------
void pqSHYXOpeningTable::refreshItems()
{
    if (this->View && this->View->viewport())
    {
        this->View->viewport()->update();
    }
}
