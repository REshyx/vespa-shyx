#include "pqSHYXPartitionedBlockNamesWidget.h"

#include "vtkDataAssembly.h"
#include "vtkPVDataInformation.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"

#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QList>
#include <QPair>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringList>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>

#include <string>

namespace
{
constexpr int kColIndex = 0;
constexpr int kColType = 1;
constexpr int kColName = 2;

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

QString labelForNode(vtkDataAssembly* assembly, int node)
{
  if (!assembly || node < 0)
  {
    return QString();
  }

  const char* label = nullptr;
  if (assembly->GetAttribute(node, "label", label) && label && label[0] != '\0')
  {
    return QString::fromUtf8(label);
  }

  const char* name = assembly->GetNodeName(node);
  return QString::fromUtf8(name ? name : "");
}

int firstNodeByAnyPath(vtkDataAssembly* assembly, const char* pathA, const char* pathB)
{
  if (!assembly)
  {
    return -1;
  }

  int node = assembly->GetFirstNodeByPath(pathA);
  if (node < 0)
  {
    node = assembly->GetFirstNodeByPath(pathB);
  }
  return node;
}

void appendChildren(vtkDataAssembly* assembly, int parent, const QString& type,
  QList<QPair<QString, QString>>& rows)
{
  if (!assembly || parent < 0)
  {
    return;
  }

  const int n = assembly->GetNumberOfChildren(parent);
  for (int i = 0; i < n; ++i)
  {
    const int child = assembly->GetChild(parent, i);
    rows.push_back(qMakePair(type, labelForNode(assembly, child)));
  }
}
}

// ---------------------------------------------------------------------------
pqSHYXPartitionedBlockNamesWidget::pqSHYXPartitionedBlockNamesWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  auto* tip = new QLabel(
    tr("Double-click the Name column, edit block names, then Apply. Use Refresh after the "
       "filter has produced output to populate the current tetrahedra / node set / side set list."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, 3, this);
  this->Model->setHorizontalHeaderLabels({ tr("#"), tr("Type"), tr("Name") });

  this->View = new QTreeView(this);
  this->View->setObjectName("SHYXPartitionedBlockNames");
  this->View->setRootIsDecorated(false);
  this->View->setAlternatingRowColors(true);
  this->View->setAllColumnsShowFocus(true);
  this->View->setUniformRowHeights(true);
  this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->View->setEditTriggers(
    QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  this->View->setSortingEnabled(false);
  this->View->setModel(this->Model);

  auto* header = this->View->header();
  header->setSectionResizeMode(kColIndex, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColType, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColName, QHeaderView::Stretch);
  header->setStretchLastSection(true);

  vbox->addWidget(this->View, 1);

  auto* refresh = new QPushButton(tr("Refresh from current output"), this);
  refresh->setToolTip(tr("Update the row list from the filter output's vtkDataAssembly."));
  vbox->addWidget(refresh);

  QObject::connect(this->Model, &QStandardItemModel::itemChanged, this,
    &pqSHYXPartitionedBlockNamesWidget::onItemChanged);
  QObject::connect(refresh, &QPushButton::clicked, this,
    &pqSHYXPartitionedBlockNamesWidget::onRefreshClicked);

  vtkSMProperty* namesProp = propertyFromGroup(smgroup, smproxy, "Names", "BlockNames");
  if (namesProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(namesProp) : nullptr;
    this->PropertyName = QString::fromUtf8(pname ? pname : "BlockNames");
    this->addPropertyLink(
      this, this->PropertyName.toUtf8().data(), SIGNAL(blockNamesChanged()), namesProp);
  }

  this->setChangeAvailableAsChangeFinished(true);
  this->onRefreshClicked();
}

// ---------------------------------------------------------------------------
pqSHYXPartitionedBlockNamesWidget::~pqSHYXPartitionedBlockNamesWidget() = default;

// ---------------------------------------------------------------------------
bool pqSHYXPartitionedBlockNamesWidget::event(QEvent* e)
{
  if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
  {
    auto* devt = static_cast<QDynamicPropertyChangeEvent*>(e);
    const QString name = QString::fromLatin1(devt->propertyName());
    if (name == this->PropertyName)
    {
      this->rebuildFromProperty();
      return true;
    }
  }
  return this->Superclass::event(e);
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::apply()
{
  this->writeBackProperty();
  this->Superclass::apply();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::reset()
{
  this->Superclass::reset();
  this->rebuildFromProperty();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onItemChanged(QStandardItem* item)
{
  if (!item || this->UpdatingFromProperty || item->column() != kColName)
  {
    return;
  }

  this->writeBackProperty();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onRefreshClicked()
{
  QList<QPair<QString, QString>> rows = this->collectCurrentOutputNames();
  const QList<QString> customNames = this->currentNamesFromProperty();

  for (int i = 0; i < rows.size() && i < customNames.size(); ++i)
  {
    if (!customNames[i].isEmpty())
    {
      rows[i].second = customNames[i];
    }
  }

  if (rows.isEmpty() && !customNames.isEmpty())
  {
    for (const QString& name : customNames)
    {
      rows.push_back(qMakePair(tr("Block"), name));
    }
  }

  this->rebuildRows(rows);
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::rebuildFromProperty()
{
  if (!this->Model || this->PropertyName.isEmpty())
  {
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  const QList<QString> names = this->currentNamesFromProperty();

  if (this->Model->rowCount() == 0)
  {
    QList<QPair<QString, QString>> rows;
    for (const QString& name : names)
    {
      rows.push_back(qMakePair(tr("Block"), name));
    }
    this->rebuildRows(rows);
    return;
  }

  QSignalBlocker blocker(this->Model);
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    if (auto* item = this->Model->item(row, kColName))
    {
      item->setText(row < names.size() ? names[row] : QString());
    }
  }
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::rebuildRows(
  const QList<QPair<QString, QString>>& typedNames)
{
  if (!this->Model)
  {
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  QSignalBlocker blocker(this->Model);
  this->Model->removeRows(0, this->Model->rowCount());

  for (int row = 0; row < typedNames.size(); ++row)
  {
    auto* indexItem = new QStandardItem(QString::number(row));
    indexItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    indexItem->setTextAlignment(Qt::AlignCenter);

    auto* typeItem = new QStandardItem(typedNames[row].first);
    typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);

    auto* nameItem = new QStandardItem(typedNames[row].second);
    nameItem->setFlags(
      Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemNeverHasChildren);

    this->Model->appendRow({ indexItem, typeItem, nameItem });
  }
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::writeBackProperty()
{
  if (this->PropertyName.isEmpty() || !this->Model)
  {
    return;
  }

  QStringList names;
  names.reserve(this->Model->rowCount());
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* item = this->Model->item(row, kColName);
    names.push_back(item ? item->text().trimmed() : QString());
  }

  {
    QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
    this->setProperty(this->PropertyName.toUtf8().data(), names.join(QLatin1Char('\n')));
  }

  Q_EMIT this->blockNamesChanged();
}

// ---------------------------------------------------------------------------
QList<QString> pqSHYXPartitionedBlockNamesWidget::currentNamesFromProperty() const
{
  QList<QString> names;
  if (this->PropertyName.isEmpty())
  {
    return names;
  }

  const QVariant value = this->property(this->PropertyName.toUtf8().data());
  const QString text = value.toString();
  if (text.isEmpty())
  {
    return names;
  }

  const QStringList split = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  for (QString name : split)
  {
    if (name.endsWith(QLatin1Char('\r')))
    {
      name.chop(1);
    }
    names.push_back(name.trimmed());
  }
  return names;
}

// ---------------------------------------------------------------------------
QList<QPair<QString, QString>> pqSHYXPartitionedBlockNamesWidget::collectCurrentOutputNames() const
{
  QList<QPair<QString, QString>> rows;
  auto* source = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!source)
  {
    return rows;
  }

  source->UpdatePipeline();
  vtkPVDataInformation* info = source->GetDataInformation(0);
  vtkDataAssembly* assembly = info ? info->GetDataAssembly() : nullptr;
  if (!assembly)
  {
    return rows;
  }

  const int elemBlocks =
    firstNodeByAnyPath(assembly, "/IOSS/element_blocks", "/element_blocks");
  const int nodeSets = firstNodeByAnyPath(assembly, "/IOSS/node_sets", "/node_sets");
  const int sideSets = firstNodeByAnyPath(assembly, "/IOSS/side_sets", "/side_sets");

  appendChildren(assembly, elemBlocks, tr("Element block"), rows);
  appendChildren(assembly, nodeSets, tr("Node set"), rows);
  appendChildren(assembly, sideSets, tr("Side set"), rows);

  return rows;
}
