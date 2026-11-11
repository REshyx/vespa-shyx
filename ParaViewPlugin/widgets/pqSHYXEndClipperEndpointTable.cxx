#include "pqSHYXEndClipperEndpointTable.h"

#include "pqArrayListDomain.h"

#include "vtkSMArraySelectionDomain.h"
#include "vtkSMDomain.h"
#include "vtkSMDomainIterator.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"

#include <vtkSmartPointer.h>

#include <QAbstractItemView>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>
#include <map>
#include <vector>

namespace
{
constexpr int kColName  = 0;
constexpr int kColDepth = 1;
constexpr int kRoleEndpointId = Qt::UserRole + 1;

int clampDepth(int depth)
{
    if (depth < 1)
    {
        return 1;
    }
    if (depth > 10)
    {
        return 10;
    }
    return depth;
}

int endpointIdFromLabel(const QString& label)
{
    static const QRegularExpression re(QStringLiteral("^Endpoint\\s+(\\d+)"));
    const QRegularExpressionMatch m = re.match(label);
    if (!m.hasMatch())
    {
        return -1;
    }
    bool ok = false;
    const int id = m.captured(1).toInt(&ok);
    return ok && id > 0 ? id : -1;
}

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

class DepthDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(
        QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* spin = new QSpinBox(parent);
        spin->setRange(1, 10);
        spin->setFrame(false);
        spin->setAlignment(Qt::AlignCenter);
        return spin;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* spin = qobject_cast<QSpinBox*>(editor);
        if (!spin)
        {
            return;
        }
        spin->setValue(clampDepth(index.data(Qt::EditRole).toInt()));
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        auto* spin = qobject_cast<QSpinBox*>(editor);
        if (!spin || !model)
        {
            return;
        }
        const int depth = spin->value();
        model->setData(index, depth, Qt::EditRole);
        model->setData(index, QString::number(depth), Qt::DisplayRole);
    }
};

class EndpointTableHeader : public QHeaderView
{
public:
    std::function<Qt::CheckState()> GetState;
    std::function<void()> ToggleAll;

    explicit EndpointTableHeader(QWidget* parentObject)
        : QHeaderView(Qt::Horizontal, parentObject)
    {
        this->setHighlightSections(false);
        this->setSectionsClickable(false);
        this->setSectionsMovable(false);
    }

protected:
    QSize sectionSizeFromContents(int logicalIndex) const override
    {
        QSize sz = QHeaderView::sectionSizeFromContents(logicalIndex);
        if (logicalIndex != kColName)
        {
            return sz;
        }
        QStyleOptionViewItem option;
        option.initFrom(this);
        option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
        const QRect checkRect =
            this->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, this);
        sz.rwidth() += checkRect.width();
        sz.setHeight(qMax(sz.height(), checkRect.height() + 4));
        return sz;
    }

    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override
    {
        if (!rect.isValid())
        {
            return;
        }
        if (logicalIndex != kColName || !this->GetState)
        {
            this->CheckRect = QRect();
            this->QHeaderView::paintSection(painter, rect, logicalIndex);
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
        QRect checkRect =
            this->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &coption, this);
        checkRect.moveLeft(rect.x() + checkRect.x());
        checkRect.moveTop(rect.y() + (rect.height() - checkRect.height()) / 2);
        coption.rect = checkRect;
        coption.state = coption.state & ~QStyle::State_HasFocus;
        switch (this->GetState())
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
        this->CheckRect = checkRect;

        QRect labelRect = rect;
        labelRect.setLeft(checkRect.right() + 4);
        this->QHeaderView::paintSection(painter, labelRect, logicalIndex);
    }

    void mousePressEvent(QMouseEvent* evt) override
    {
        this->PressPosition = evt->pos();
        if (!this->CheckRect.contains(evt->pos()))
        {
            this->QHeaderView::mousePressEvent(evt);
        }
    }

    void mouseReleaseEvent(QMouseEvent* evt) override
    {
        const bool wasClick = (evt->pos() - this->PressPosition).manhattanLength() < 3;
        const bool onCheck = this->CheckRect.contains(this->PressPosition);
        this->PressPosition = QPoint();
        if (evt->button() == Qt::LeftButton && wasClick && onCheck && this->ToggleAll)
        {
            this->ToggleAll();
            return;
        }
        this->QHeaderView::mouseReleaseEvent(evt);
    }

private:
    QPoint PressPosition;
    mutable QRect CheckRect;
};

int depthOfItem(const QStandardItem* item, int fallback)
{
    if (!item)
    {
        return fallback;
    }
    bool ok = false;
    int depth = item->data(Qt::EditRole).toInt(&ok);
    if (!ok)
    {
        depth = item->text().toInt(&ok);
    }
    return ok ? clampDepth(depth) : fallback;
}
}

pqSHYXEndClipperEndpointTable::pqSHYXEndClipperEndpointTable(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
    : Superclass(smproxy, parentObject)
{
    Q_UNUSED(smgroup);

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(2);

    this->Hint = new QLabel(this);
    this->Hint->setWordWrap(true);
    this->Hint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    vbox->addWidget(this->Hint);

    this->Model = new QStandardItemModel(0, 2, this);
    this->Model->setHorizontalHeaderLabels({ tr("Endpoint"), tr("Tangent Depth") });
    this->Model->setHeaderData(kColName, Qt::Horizontal,
        tr("Check endpoints to clip. The header checkbox selects or clears every row."),
        Qt::ToolTipRole);
    this->Model->setHeaderData(kColDepth, Qt::Horizontal,
        tr("Skeleton edges walked inward from this endpoint to estimate the cut normal. "
           "1 follows the leaf; larger values average a longer segment. Range 1-10."),
        Qt::ToolTipRole);

    this->View = new QTreeView(this);
    this->View->setObjectName(QStringLiteral("SHYXEndClipperEndpointTable"));
    this->View->setModel(this->Model);
    this->View->setItemDelegateForColumn(kColDepth, new DepthDelegate(this->View));
    this->View->setRootIsDecorated(false);
    this->View->setIndentation(0);
    this->View->setAllColumnsShowFocus(true);
    this->View->setUniformRowHeights(true);
    this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->View->setSelectionMode(QAbstractItemView::SingleSelection);
    this->View->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::DoubleClicked |
        QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    this->View->setMinimumHeight(140);

    auto* header = new EndpointTableHeader(this->View);
    header->GetState = [this]() { return this->columnCheckState(); };
    header->ToggleAll = [this]() { this->toggleAllChecks(); };
    header->setStretchLastSection(false);
    this->View->setHeader(header);
    header->setSectionResizeMode(kColName, QHeaderView::Stretch);
    header->setSectionResizeMode(kColDepth, QHeaderView::ResizeToContents);
    vbox->addWidget(this->View, 1);

    QObject::connect(this->Model, &QStandardItemModel::itemChanged, this,
        &pqSHYXEndClipperEndpointTable::onItemChanged);
    QObject::connect(this->View, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (index.column() == kColDepth)
        {
            this->View->edit(index);
        }
    });

    this->EndpointPropName = QStringLiteral("EndpointStatus");
    vtkSMProperty* statusProp = smproxy ? smproxy->GetProperty("EndpointStatus") : nullptr;
    if (statusProp)
    {
        if (auto* dom = findArraySelectionDomain(statusProp))
        {
            new pqArrayListDomain(this, this->EndpointPropName, smproxy, statusProp, dom);
        }
        this->addPropertyLink(this, this->EndpointPropName.toUtf8().data(),
            SIGNAL(endpointStatusChanged()), statusProp);
    }

    this->updateHint();
}

pqSHYXEndClipperEndpointTable::~pqSHYXEndClipperEndpointTable() = default;

bool pqSHYXEndClipperEndpointTable::event(QEvent* e)
{
    if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
    {
        auto* devt = static_cast<QDynamicPropertyChangeEvent*>(e);
        if (QString::fromLatin1(devt->propertyName()) == this->EndpointPropName)
        {
            this->rebuild();
            return true;
        }
    }
    return this->Superclass::event(e);
}

void pqSHYXEndClipperEndpointTable::apply()
{
    auto* ivp = this->proxy()
        ? vtkSMIntVectorProperty::SafeDownCast(this->proxy()->GetProperty("EndpointTangentDepths"))
        : nullptr;
    if (ivp && ivp->GetNumberOfUncheckedElements() > 0 && this->Model && this->Model->rowCount() > 0)
    {
        this->writeDepths(true);
    }
    this->Superclass::apply();
    if (this->proxy())
    {
        this->proxy()->UpdateVTKObjects();
    }
}

void pqSHYXEndClipperEndpointTable::reset()
{
    this->Superclass::reset();
    if (this->proxy())
    {
        if (auto* ivp = vtkSMIntVectorProperty::SafeDownCast(
                this->proxy()->GetProperty("EndpointTangentDepths")))
        {
            ivp->ClearUncheckedElements();
        }
    }
    this->rebuild();
}

void pqSHYXEndClipperEndpointTable::onItemChanged(QStandardItem* item)
{
    if (!item || this->Updating)
    {
        return;
    }
    if (item->column() == kColName)
    {
        this->writeStatus();
        this->refreshHeader();
    }
    else if (item->column() == kColDepth)
    {
        this->writeDepths(false);
    }
}

void pqSHYXEndClipperEndpointTable::rebuild()
{
    if (!this->Model)
    {
        return;
    }

    QScopedValueRollback<bool> guard(this->Updating, true);
    const int selectedId = this->currentEndpointId();
    const int fallback = this->fallbackDepth();

    std::map<int, int> savedDepth;
    if (this->proxy())
    {
        auto* ivp =
            vtkSMIntVectorProperty::SafeDownCast(this->proxy()->GetProperty("EndpointTangentDepths"));
        if (ivp)
        {
            unsigned int count = ivp->GetNumberOfUncheckedElements();
            const bool useUnchecked = count > 0;
            if (!useUnchecked)
            {
                count = ivp->GetNumberOfElements();
            }
            count = (count / 2) * 2;
            for (unsigned int i = 0; i < count; i += 2)
            {
                const int id = useUnchecked ? ivp->GetUncheckedElement(i) : ivp->GetElement(i);
                const int depth =
                    useUnchecked ? ivp->GetUncheckedElement(i + 1) : ivp->GetElement(i + 1);
                if (id > 0)
                {
                    savedDepth[id] = clampDepth(depth);
                }
            }
        }
    }

    const QVariant raw = this->property(this->EndpointPropName.toUtf8().data());
    const QList<QList<QVariant>> rows = raw.value<QList<QList<QVariant>>>();

    this->Model->removeRows(0, this->Model->rowCount());
    int restoreRow = -1;
    for (const QList<QVariant>& tuple : rows)
    {
        if (tuple.size() < 2)
        {
            continue;
        }
        const QString name = tuple[0].toString();
        if (name.isEmpty())
        {
            continue;
        }
        const int id = endpointIdFromLabel(name);
        const bool checked = tuple[1].toBool();
        const auto found = savedDepth.find(id);
        const int depth = found != savedDepth.end() ? found->second : fallback;

        auto* nameItem = new QStandardItem(name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
            Qt::ItemNeverHasChildren);
        nameItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        nameItem->setData(id, kRoleEndpointId);

        auto* depthItem = new QStandardItem(QString::number(depth));
        depthItem->setData(depth, Qt::EditRole);
        depthItem->setTextAlignment(Qt::AlignCenter);
        depthItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable |
            Qt::ItemNeverHasChildren);

        const int row = this->Model->rowCount();
        QList<QStandardItem*> rowItems;
        rowItems << nameItem << depthItem;
        this->Model->appendRow(rowItems);
        if (id > 0 && id == selectedId)
        {
            restoreRow = row;
        }
    }

    if (restoreRow >= 0)
    {
        this->View->setCurrentIndex(this->Model->index(restoreRow, kColName));
    }
    this->refreshHeader();
    this->updateHint();
}

void pqSHYXEndClipperEndpointTable::writeStatus()
{
    if (!this->Model || this->EndpointPropName.isEmpty())
    {
        return;
    }

    QList<QList<QVariant>> rows;
    rows.reserve(this->Model->rowCount());
    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        auto* item = this->Model->item(r, kColName);
        if (!item)
        {
            continue;
        }
        const QString name = item->text();
        if (name.isEmpty())
        {
            continue;
        }
        const bool checked = item->checkState() == Qt::Checked;
        rows.push_back({ QVariant(name), QVariant(checked ? 1 : 0) });
    }

    QVariant value;
    value.setValue(rows);
    {
        QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
        this->setProperty(this->EndpointPropName.toUtf8().data(), value);
    }
    Q_EMIT this->endpointStatusChanged();
}

void pqSHYXEndClipperEndpointTable::writeDepths(bool pushChecked)
{
    auto* ivp = this->proxy()
        ? vtkSMIntVectorProperty::SafeDownCast(this->proxy()->GetProperty("EndpointTangentDepths"))
        : nullptr;
    if (!ivp || !this->Model || this->Model->rowCount() == 0)
    {
        return;
    }

    const int fallback = this->fallbackDepth();
    std::vector<int> packed;
    packed.reserve(static_cast<std::size_t>(this->Model->rowCount()) * 2);
    for (int r = 0; r < this->Model->rowCount(); ++r)
    {
        auto* nameItem = this->Model->item(r, kColName);
        if (!nameItem)
        {
            continue;
        }
        const int id = nameItem->data(kRoleEndpointId).toInt();
        if (id < 1)
        {
            continue;
        }
        packed.push_back(id);
        packed.push_back(depthOfItem(this->Model->item(r, kColDepth), fallback));
    }
    if (packed.empty())
    {
        return;
    }

    const unsigned int n = static_cast<unsigned int>(packed.size());
    if (pushChecked)
    {
        ivp->SetElements(packed.data(), n);
        ivp->ClearUncheckedElements();
        return;
    }

    const unsigned int existing = ivp->GetNumberOfUncheckedElements();
    if (existing == n)
    {
        bool same = true;
        for (unsigned int i = 0; i < n; ++i)
        {
            if (ivp->GetUncheckedElement(i) != packed[static_cast<std::size_t>(i)])
            {
                same = false;
                break;
            }
        }
        if (same)
        {
            return;
        }
    }
    ivp->SetUncheckedElements(packed.data(), n);
    Q_EMIT this->changeAvailable();
    QTimer::singleShot(0, this, [this]() {
        if (!this->Updating)
        {
            Q_EMIT this->changeFinished();
        }
    });
}

void pqSHYXEndClipperEndpointTable::toggleAllChecks()
{
    if (!this->Model || this->Model->rowCount() == 0)
    {
        return;
    }
    const bool check = this->columnCheckState() != Qt::Checked;
    {
        QSignalBlocker blocker(this->Model);
        for (int r = 0; r < this->Model->rowCount(); ++r)
        {
            if (auto* item = this->Model->item(r, kColName))
            {
                item->setCheckState(check ? Qt::Checked : Qt::Unchecked);
            }
        }
    }
    this->writeStatus();
    this->refreshHeader();
}

void pqSHYXEndClipperEndpointTable::refreshHeader()
{
    if (this->View && this->View->header())
    {
        this->View->header()->viewport()->update();
    }
}

void pqSHYXEndClipperEndpointTable::updateHint()
{
    if (!this->Hint)
    {
        return;
    }
    const int n = this->Model ? this->Model->rowCount() : 0;
    if (n == 0)
    {
        this->Hint->setText(tr("Apply once to list centerline endpoints. "
                               "Each row has its own Tangent Depth (1-10)."));
    }
    else
    {
        this->Hint->setText(tr("Check a row to clip it. Tangent Depth is per endpoint. "
                               "Select a row to show its cut plane."));
    }
}

Qt::CheckState pqSHYXEndClipperEndpointTable::columnCheckState() const
{
    if (!this->Model || this->Model->rowCount() == 0)
    {
        return Qt::Unchecked;
    }
    int checked = 0;
    const int total = this->Model->rowCount();
    for (int r = 0; r < total; ++r)
    {
        auto* item = this->Model->item(r, kColName);
        if (item && item->checkState() == Qt::Checked)
        {
            ++checked;
        }
    }
    if (checked == 0)
    {
        return Qt::Unchecked;
    }
    if (checked == total)
    {
        return Qt::Checked;
    }
    return Qt::PartiallyChecked;
}

int pqSHYXEndClipperEndpointTable::fallbackDepth() const
{
    int depth = 1;
    if (this->proxy())
    {
        if (auto* ivp = vtkSMIntVectorProperty::SafeDownCast(this->proxy()->GetProperty("TangentDepth")))
        {
            if (ivp->GetNumberOfElements() > 0)
            {
                depth = ivp->GetElement(0);
            }
        }
    }
    return clampDepth(depth);
}

int pqSHYXEndClipperEndpointTable::currentEndpointId() const
{
    if (!this->View || !this->Model)
    {
        return -1;
    }
    const QModelIndex current = this->View->currentIndex();
    if (!current.isValid())
    {
        return -1;
    }
    auto* item = this->Model->item(current.row(), kColName);
    if (!item)
    {
        return -1;
    }
    return item->data(kRoleEndpointId).toInt();
}
