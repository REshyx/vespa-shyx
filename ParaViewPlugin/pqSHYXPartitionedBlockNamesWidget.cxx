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
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringList>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <string>

namespace
{
constexpr int kColIndex = 0;
constexpr int kColType = 1;
constexpr int kColName = 2;
constexpr int kColWriteNormal = 3;
constexpr int kFirstVariableCol = 4;
constexpr char kNodeSetNamePrefix[] = "node_";

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
  QList<pqSHYXPartitionedBlockNamesWidget::BlockRow>& rows)
{
  if (!assembly || parent < 0)
  {
    return;
  }

  const int n = assembly->GetNumberOfChildren(parent);
  for (int i = 0; i < n; ++i)
  {
    const int child = assembly->GetChild(parent, i);
    rows.push_back({ type, labelForNode(assembly, child), false, QStringList{ QStringLiteral("0") } });
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
    tr("Double-click Side set Name cells to edit paired side/node block names. For Side set rows, "
       "check Write Normal to accumulate BoundaryRadialValueNormal onto tetrahedra volume points; "
       "edit Variable columns to write BoundaryVariable1/2/... when non-zero. Node set names mirror "
       "the matching Side set row with a \"node_\" prefix. Use Refresh after the filter has produced "
       "output to populate the block list."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, kFirstVariableCol + this->VariableColumnCount, this);
  this->setVariableColumnCount(this->VariableColumnCount);

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
  header->setSectionResizeMode(kColWriteNormal, QHeaderView::ResizeToContents);
  header->setStretchLastSection(false);

  vbox->addWidget(this->View, 1);

  auto* buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->setSpacing(4);

  auto* refresh = new QPushButton(tr("Refresh from current output"), this);
  refresh->setToolTip(tr("Update the row list from the filter output's vtkDataAssembly."));
  buttons->addWidget(refresh);

  auto* addVariable = new QPushButton(tr("Add variable"), this);
  addVariable->setToolTip(tr("Append a Variable column for side-set boundary values."));
  buttons->addWidget(addVariable);

  auto* deleteVariable = new QPushButton(tr("Delete variable"), this);
  deleteVariable->setToolTip(tr("Delete the selected Variable column, or the last one."));
  buttons->addWidget(deleteVariable);
  buttons->addStretch(1);
  vbox->addLayout(buttons);

  QObject::connect(this->Model, &QStandardItemModel::itemChanged, this,
    &pqSHYXPartitionedBlockNamesWidget::onItemChanged);
  QObject::connect(refresh, &QPushButton::clicked, this,
    &pqSHYXPartitionedBlockNamesWidget::onRefreshClicked);
  QObject::connect(addVariable, &QPushButton::clicked, this,
    &pqSHYXPartitionedBlockNamesWidget::onAddVariableClicked);
  QObject::connect(deleteVariable, &QPushButton::clicked, this,
    &pqSHYXPartitionedBlockNamesWidget::onDeleteVariableClicked);

  vtkSMProperty* namesProp = propertyFromGroup(smgroup, smproxy, "Names", "BlockNames");
  if (namesProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(namesProp) : nullptr;
    this->NamesPropertyName = QString::fromUtf8(pname ? pname : "BlockNames");
    this->addPropertyLink(
      this, this->NamesPropertyName.toUtf8().data(), SIGNAL(blockNamesChanged()), namesProp);
  }

  vtkSMProperty* varsProp =
    propertyFromGroup(smgroup, smproxy, "BoundaryVariables", "BoundaryVariables");
  if (varsProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(varsProp) : nullptr;
    this->BoundaryVariablesPropertyName =
      QString::fromUtf8(pname ? pname : "BoundaryVariables");
    this->addPropertyLink(this, this->BoundaryVariablesPropertyName.toUtf8().data(),
      SIGNAL(blockNamesChanged()), varsProp);
  }

  vtkSMProperty* writeNormalsProp =
    propertyFromGroup(smgroup, smproxy, "BoundaryWriteNormals", "BoundaryWriteNormals");
  if (writeNormalsProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(writeNormalsProp) : nullptr;
    this->BoundaryWriteNormalsPropertyName =
      QString::fromUtf8(pname ? pname : "BoundaryWriteNormals");
    this->addPropertyLink(this, this->BoundaryWriteNormalsPropertyName.toUtf8().data(),
      SIGNAL(blockNamesChanged()), writeNormalsProp);
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
    if (name == this->NamesPropertyName || name == this->BoundaryVariablesPropertyName ||
      name == this->BoundaryWriteNormalsPropertyName)
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
  if (!item || this->UpdatingFromProperty ||
    (item->column() != kColName && item->column() != kColWriteNormal &&
      item->column() < kFirstVariableCol))
  {
    return;
  }

  this->syncNodeRowsFromSideRows();
  this->writeBackProperty();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onRefreshClicked()
{
  QList<BlockRow> rows = this->collectCurrentOutputNames();
  const QList<QString> customNames = this->currentNamesFromProperty();
  const QList<QStringList> variables = this->currentBoundaryVariablesFromProperty();
  const QList<int> writeNormals = this->currentBoundaryWriteNormalsFromProperty();

  for (int i = 0; i < rows.size() && i < customNames.size(); ++i)
  {
    if (!customNames[i].isEmpty())
    {
      rows[i].Name = customNames[i];
    }
  }

  for (int i = 0; i < rows.size() && i < variables.size(); ++i)
  {
    if (!variables[i].isEmpty())
    {
      rows[i].Variables = variables[i];
    }
  }

  for (int i = 0; i < rows.size() && i < writeNormals.size(); ++i)
  {
    rows[i].WriteNormal = writeNormals[i] != 0;
  }

  if (rows.isEmpty() && !customNames.isEmpty())
  {
    for (const QString& name : customNames)
    {
      rows.push_back({ tr("Block"), name, false, QStringList{ QStringLiteral("0") } });
    }
  }

  this->rebuildRows(rows);
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onAddVariableClicked()
{
  this->setVariableColumnCount(this->VariableColumnCount + 1);
  this->syncNodeRowsFromSideRows();
  this->writeBackProperty();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onDeleteVariableClicked()
{
  if (this->VariableColumnCount <= 1)
  {
    return;
  }

  int removeColumn = this->View ? this->View->currentIndex().column() : -1;
  if (removeColumn < kFirstVariableCol)
  {
    removeColumn = kFirstVariableCol + this->VariableColumnCount - 1;
  }
  this->Model->removeColumn(removeColumn);
  this->VariableColumnCount = std::max(1, this->Model->columnCount() - kFirstVariableCol);
  this->setVariableColumnCount(this->VariableColumnCount);
  this->syncNodeRowsFromSideRows();
  this->writeBackProperty();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::rebuildFromProperty()
{
  if (!this->Model || this->NamesPropertyName.isEmpty())
  {
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  const QList<QString> names = this->currentNamesFromProperty();
  const QList<QStringList> variables = this->currentBoundaryVariablesFromProperty();
  const QList<int> writeNormals = this->currentBoundaryWriteNormalsFromProperty();

  if (this->Model->rowCount() == 0)
  {
    QList<BlockRow> rows;
    for (int i = 0; i < names.size(); ++i)
    {
      rows.push_back({ tr("Block"), names[i],
        i < writeNormals.size() ? writeNormals[i] != 0 : false,
        i < variables.size() ? variables[i] : QStringList{ QStringLiteral("0") } });
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
    auto* typeItem = this->Model->item(row, kColType);
    const bool isSideSet = typeItem && typeItem->text() == tr("Side set");
    if (auto* writeNormalItem = this->Model->item(row, kColWriteNormal))
    {
      const bool checked = isSideSet && row < writeNormals.size() && writeNormals[row] != 0;
      writeNormalItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    for (int c = 0; c < this->VariableColumnCount; ++c)
    {
      if (auto* item = this->Model->item(row, kFirstVariableCol + c))
      {
        const QString value = (isSideSet && row < variables.size() && c < variables[row].size())
          ? variables[row][c]
          : QStringLiteral("0");
        item->setText(value);
      }
    }
  }
  this->syncNodeRowsFromSideRows();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::rebuildRows(
  const QList<BlockRow>& rows)
{
  if (!this->Model)
  {
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  QSignalBlocker blocker(this->Model);
  int nVariables = 1;
  for (const BlockRow& row : rows)
  {
    nVariables = std::max(nVariables, static_cast<int>(row.Variables.size()));
  }
  this->setVariableColumnCount(nVariables);
  this->Model->removeRows(0, this->Model->rowCount());

  for (int row = 0; row < rows.size(); ++row)
  {
    auto* indexItem = new QStandardItem(QString::number(row));
    indexItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    indexItem->setTextAlignment(Qt::AlignCenter);

    auto* typeItem = new QStandardItem(rows[row].Type);
    typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);

    const bool isSideSet = rows[row].Type == tr("Side set");
    auto* nameItem = new QStandardItem(rows[row].Name);
    if (isSideSet)
    {
      nameItem->setFlags(
        Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemNeverHasChildren);
    }
    else
    {
      nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    }

    auto* writeNormalItem = new QStandardItem();
    writeNormalItem->setTextAlignment(Qt::AlignCenter);
    if (isSideSet)
    {
      writeNormalItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
        Qt::ItemNeverHasChildren);
      writeNormalItem->setCheckState(rows[row].WriteNormal ? Qt::Checked : Qt::Unchecked);
    }
    else
    {
      writeNormalItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
      writeNormalItem->setCheckState(Qt::Unchecked);
    }

    QList<QStandardItem*> items = { indexItem, typeItem, nameItem, writeNormalItem };
    for (int c = 0; c < this->VariableColumnCount; ++c)
    {
      const QString value = (c < rows[row].Variables.size() && !rows[row].Variables[c].isEmpty())
        ? rows[row].Variables[c]
        : QStringLiteral("0");
      auto* variableItem = new QStandardItem(isSideSet ? value : QStringLiteral("0"));
      if (isSideSet)
      {
        variableItem->setFlags(
          Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemNeverHasChildren);
      }
      else
      {
        variableItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
      }
      variableItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      items.push_back(variableItem);
    }

    this->Model->appendRow(items);
  }
  this->syncNodeRowsFromSideRows();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::writeBackProperty()
{
  if (this->NamesPropertyName.isEmpty() || !this->Model)
  {
    return;
  }

  QStringList names;
  QStringList variableRows;
  QStringList writeNormalRows;
  names.reserve(this->Model->rowCount());
  variableRows.reserve(this->Model->rowCount());
  writeNormalRows.reserve(this->Model->rowCount());
  this->syncNodeRowsFromSideRows();
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* nameItem = this->Model->item(row, kColName);
    names.push_back(nameItem ? nameItem->text().trimmed() : QString());
    auto* typeItem = this->Model->item(row, kColType);
    const bool isSideSet = typeItem && typeItem->text() == tr("Side set");
    auto* writeNormalItem = this->Model->item(row, kColWriteNormal);
    const bool writeNormal =
      isSideSet && writeNormalItem && writeNormalItem->checkState() == Qt::Checked;
    writeNormalRows.push_back(writeNormal ? QStringLiteral("1") : QStringLiteral("0"));
    QStringList variables;
    for (int c = 0; c < this->VariableColumnCount; ++c)
    {
      auto* item = this->Model->item(row, kFirstVariableCol + c);
      bool ok = false;
      const double value = item ? item->text().trimmed().toDouble(&ok) : 0.0;
      variables.push_back(ok ? QString::number(value, 'g', 16) : QStringLiteral("0"));
    }
    variableRows.push_back(variables.join(QLatin1Char('\t')));
  }

  {
    QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
    this->setProperty(this->NamesPropertyName.toUtf8().data(), names.join(QLatin1Char('\n')));
    if (!this->BoundaryVariablesPropertyName.isEmpty())
    {
      this->setProperty(
        this->BoundaryVariablesPropertyName.toUtf8().data(), variableRows.join(QLatin1Char('\n')));
    }
    if (!this->BoundaryWriteNormalsPropertyName.isEmpty())
    {
      this->setProperty(this->BoundaryWriteNormalsPropertyName.toUtf8().data(),
        writeNormalRows.join(QLatin1Char('\n')));
    }
  }

  Q_EMIT this->blockNamesChanged();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::syncNodeRowsFromSideRows()
{
  if (!this->Model)
  {
    return;
  }

  QList<int> sideRows;
  QList<int> nodeRows;
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* typeItem = this->Model->item(row, kColType);
    if (!typeItem)
    {
      continue;
    }
    if (typeItem->text() == tr("Side set"))
    {
      sideRows.push_back(row);
    }
    else if (typeItem->text() == tr("Node set"))
    {
      nodeRows.push_back(row);
    }
  }

  QSignalBlocker blocker(this->Model);
  const int nPairs = std::min(sideRows.size(), nodeRows.size());
  for (int i = 0; i < nPairs; ++i)
  {
    const int sideRow = sideRows[i];
    const int nodeRow = nodeRows[i];
    if (auto* nodeName = this->Model->item(nodeRow, kColName))
    {
      auto* sideName = this->Model->item(sideRow, kColName);
      nodeName->setText(sideName ? QString::fromLatin1(kNodeSetNamePrefix) + sideName->text() : QString());
    }
    if (auto* nodeWriteNormal = this->Model->item(nodeRow, kColWriteNormal))
    {
      auto* sideWriteNormal = this->Model->item(sideRow, kColWriteNormal);
      nodeWriteNormal->setCheckState(
        sideWriteNormal ? sideWriteNormal->checkState() : Qt::Unchecked);
    }
    for (int c = 0; c < this->VariableColumnCount; ++c)
    {
      auto* nodeVariable = this->Model->item(nodeRow, kFirstVariableCol + c);
      auto* sideVariable = this->Model->item(sideRow, kFirstVariableCol + c);
      if (nodeVariable)
      {
        nodeVariable->setText(sideVariable ? sideVariable->text() : QStringLiteral("0"));
      }
    }
  }
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::setVariableColumnCount(int count)
{
  if (!this->Model)
  {
    return;
  }

  count = std::max(1, count);
  this->VariableColumnCount = count;
  this->Model->setColumnCount(kFirstVariableCol + count);

  QStringList labels = { tr("#"), tr("Type"), tr("Name"), tr("Write Normal") };
  for (int i = 0; i < count; ++i)
  {
    labels.push_back(tr("Variable%1").arg(i + 1));
  }
  this->Model->setHorizontalHeaderLabels(labels);

  if (this->View && this->View->header())
  {
    auto* header = this->View->header();
    header->setSectionResizeMode(kColIndex, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kColType, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kColName, QHeaderView::Stretch);
    header->setSectionResizeMode(kColWriteNormal, QHeaderView::ResizeToContents);
    for (int c = 0; c < count; ++c)
    {
      header->setSectionResizeMode(kFirstVariableCol + c, QHeaderView::ResizeToContents);
    }
    header->setStretchLastSection(false);
  }

  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* typeItem = this->Model->item(row, kColType);
    const bool isSideSet = typeItem && typeItem->text() == tr("Side set");
    for (int c = 0; c < count; ++c)
    {
      const int col = kFirstVariableCol + c;
      if (!this->Model->item(row, col))
      {
        auto* item = new QStandardItem(QStringLiteral("0"));
        if (isSideSet)
        {
          item->setFlags(
            Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemNeverHasChildren);
        }
        else
        {
          item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
        }
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        this->Model->setItem(row, col, item);
      }
    }
  }
}

// ---------------------------------------------------------------------------
QList<QString> pqSHYXPartitionedBlockNamesWidget::currentNamesFromProperty() const
{
  QList<QString> names;
  if (this->NamesPropertyName.isEmpty())
  {
    return names;
  }

  const QVariant value = this->property(this->NamesPropertyName.toUtf8().data());
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
QList<QStringList> pqSHYXPartitionedBlockNamesWidget::currentBoundaryVariablesFromProperty() const
{
  QList<QStringList> variables;
  if (this->BoundaryVariablesPropertyName.isEmpty())
  {
    return variables;
  }

  const QVariant value = this->property(this->BoundaryVariablesPropertyName.toUtf8().data());
  const QString text = value.toString();
  if (text.isEmpty())
  {
    return variables;
  }

  const QStringList split = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  for (QString line : split)
  {
    if (line.endsWith(QLatin1Char('\r')))
    {
      line.chop(1);
    }
    QStringList row = line.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
    for (QString& value : row)
    {
      value = value.trimmed();
    }
    variables.push_back(row);
  }
  return variables;
}

// ---------------------------------------------------------------------------
QList<int> pqSHYXPartitionedBlockNamesWidget::currentBoundaryWriteNormalsFromProperty() const
{
  QList<int> flags;
  if (this->BoundaryWriteNormalsPropertyName.isEmpty())
  {
    return flags;
  }

  const QVariant value = this->property(this->BoundaryWriteNormalsPropertyName.toUtf8().data());
  const QString text = value.toString();
  if (text.isEmpty())
  {
    return flags;
  }

  const QStringList split = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  for (QString line : split)
  {
    if (line.endsWith(QLatin1Char('\r')))
    {
      line.chop(1);
    }
    bool ok = false;
    const int flag = line.trimmed().toInt(&ok);
    flags.push_back(ok && flag != 0 ? 1 : 0);
  }
  return flags;
}

// ---------------------------------------------------------------------------
QList<pqSHYXPartitionedBlockNamesWidget::BlockRow>
pqSHYXPartitionedBlockNamesWidget::collectCurrentOutputNames() const
{
  QList<BlockRow> rows;
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
  appendChildren(assembly, sideSets, tr("Side set"), rows);
  appendChildren(assembly, nodeSets, tr("Node set"), rows);

  return rows;
}
