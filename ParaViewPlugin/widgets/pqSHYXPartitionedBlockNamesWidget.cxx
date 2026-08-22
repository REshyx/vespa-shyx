#include "pqSHYXPartitionedBlockNamesWidget.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqDataAssemblyTreeModel.h"
#include "pqDataRepresentation.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"
#include "pqUndoStack.h"
#include "pqView.h"

#include "vtkCommand.h"
#include "vtkDataAssembly.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkPVDataInformation.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"
#include "vtkSMTrace.h"

#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QPixmap>
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
#include <vector>

namespace
{
constexpr int kColVisibility = 0;
constexpr int kColIndex = 1;
constexpr int kColType = 2;
constexpr int kColName = 3;
constexpr int kColWriteNormal = 4;
constexpr int kFirstVariableCol = 5;
constexpr int kSelectorPathRole = Qt::UserRole;
constexpr int kDataSetIndexRole = Qt::UserRole + 1;
constexpr int kVisibleRole = Qt::UserRole + 2;
constexpr char kNodeSetNamePrefix[] = "node_";

QIcon eyeIcon(Qt::CheckState state)
{
  if (state == Qt::Unchecked)
  {
    return QIcon(QStringLiteral(":/pqWidgets/Icons/pqEyeballClosed.svg"));
  }
  if (state == Qt::Checked)
  {
    return QIcon(QStringLiteral(":/pqWidgets/Icons/pqEyeball.svg"));
  }

  const QIcon open(QStringLiteral(":/pqWidgets/Icons/pqEyeball.svg"));
  QIcon mixed;
  for (const int dim : { 16, 20, 24, 32 })
  {
    const QPixmap src = open.pixmap(dim, dim);
    QPixmap faded(src.size());
    faded.fill(Qt::transparent);
    QPainter painter(&faded);
    painter.setOpacity(0.4);
    painter.drawPixmap(0, 0, src);
    painter.end();
    mixed.addPixmap(faded);
  }
  return mixed;
}

QIcon eyeIcon(bool visible)
{
  return eyeIcon(visible ? Qt::Checked : Qt::Unchecked);
}

QStringList checkedSelectorsFromProperty(
  vtkSMStringVectorProperty* prop, vtkDataAssembly* assembly)
{
  if (!prop)
  {
    return { QStringLiteral("/") };
  }

  const std::vector<std::string>& elems = prop->GetElements();
  if (elems.empty())
  {
    return {};
  }

  const char* root = assembly ? assembly->GetRootNodeName() : nullptr;
  const std::string rootName = std::string("/") + (root ? root : "");
  if (elems[0] == "/" || elems[0] == rootName)
  {
    return { QStringLiteral("/") };
  }

  QStringList checked;
  checked.reserve(static_cast<int>(elems.size()));
  for (const std::string& elem : elems)
  {
    checked.push_back(QString::fromStdString(elem));
  }
  return checked;
}

int findNodeByDataSetIndex(vtkDataAssembly* assembly, int parent, unsigned int index)
{
  if (!assembly || parent < 0)
  {
    return -1;
  }

  const std::vector<unsigned int> indices =
    assembly->GetDataSetIndices(parent, /*traverse_subtree=*/false);
  for (unsigned int value : indices)
  {
    if (value == index)
    {
      return parent;
    }
  }

  const int n = assembly->GetNumberOfChildren(parent);
  for (int i = 0; i < n; ++i)
  {
    const int found = findNodeByDataSetIndex(assembly, assembly->GetChild(parent, i), index);
    if (found >= 0)
    {
      return found;
    }
  }
  return -1;
}

vtkSMStringVectorProperty* blockVisibilityProperty(vtkSMProxy* reprProxy)
{
  if (!reprProxy)
  {
    return nullptr;
  }
  auto* visibility = vtkSMStringVectorProperty::SafeDownCast(reprProxy->GetProperty("BlockVisibilities"));
  return visibility ? visibility
                    : vtkSMStringVectorProperty::SafeDownCast(reprProxy->GetProperty("BlockSelectors"));
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
    pqSHYXPartitionedBlockNamesWidget::BlockRow row;
    row.Type = type;
    row.Name = labelForNode(assembly, child);
    row.WriteNormal = false;
    row.Variables = QStringList{ QString() };
    row.SelectorPath = QString::fromStdString(assembly->GetNodePath(child));
    const std::vector<unsigned int> indices =
      assembly->GetDataSetIndices(child, /*traverse_subtree=*/false);
    row.DataSetIndex = indices.empty() ? -1 : static_cast<int>(indices.front());
    rows.push_back(row);
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
    tr("Click the eye to show or hide that block in the active view (same as Hide Block). The "
       "header eye shows or hides every listed block. Side and node rows stay linked. Double-click "
       "Side set Name cells to edit paired side/node block "
       "names. For Side set rows, check Write Normal to accumulate BoundaryRadialValueNormal onto "
       "tetrahedra volume points; edit Variable columns to write BoundaryVariable1/2/... when "
       "finite (leave empty / NaN to skip). Node set names mirror the matching Side set row with a "
       "\"node_\" prefix. Use Refresh after the filter has produced output to populate the block "
       "list."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, kFirstVariableCol + this->VariableColumnCount, this);
  this->setVariableColumnCount(this->VariableColumnCount);

  this->View = new QTreeView(this);
  this->View->setObjectName("SHYXPartitionedBlockNames");
  this->View->setRootIsDecorated(false);
  this->View->setIndentation(0);
  this->View->setAlternatingRowColors(true);
  this->View->setAllColumnsShowFocus(true);
  this->View->setUniformRowHeights(true);
  this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->View->setEditTriggers(
    QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  this->View->setSortingEnabled(false);
  this->View->setModel(this->Model);

  auto* header = this->View->header();
  header->setSectionsClickable(true);
  header->setHighlightSections(false);
  header->setSectionResizeMode(kColVisibility, QHeaderView::ResizeToContents);
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
  QObject::connect(this->View, &QTreeView::clicked, this,
    &pqSHYXPartitionedBlockNamesWidget::onViewClicked);
  QObject::connect(this->View->header(), &QHeaderView::sectionClicked, this,
    &pqSHYXPartitionedBlockNamesWidget::onHeaderSectionClicked);
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

  this->BlockVisibilityVTKConnect = vtkEventQtSlotConnect::New();
  QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::viewChanged, this,
    &pqSHYXPartitionedBlockNamesWidget::onActiveViewOrRepresentationChanged);
  QObject::connect(&pqActiveObjects::instance(),
    QOverload<pqDataRepresentation*>::of(&pqActiveObjects::representationChanged), this,
    &pqSHYXPartitionedBlockNamesWidget::onActiveViewOrRepresentationChanged);
  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    if (auto* src = smm->findItem<pqPipelineSource*>(smproxy))
    {
      this->RepresentationConnections.push_back(
        QObject::connect(src, &pqPipelineSource::representationAdded, this,
          [this](pqPipelineSource*, pqDataRepresentation*, int)
          { this->connectBlockVisibilityObserver(); }));
      this->RepresentationConnections.push_back(
        QObject::connect(src, &pqPipelineSource::representationRemoved, this,
          [this](pqPipelineSource*, pqDataRepresentation*, int)
          { this->connectBlockVisibilityObserver(); }));
      this->RepresentationConnections.push_back(QObject::connect(
        src, QOverload<pqPipelineSource*>::of(&pqPipelineSource::dataUpdated), this,
        [this](pqPipelineSource*) { this->updateEyeIcons(); }));
    }
  }

  this->setChangeAvailableAsChangeFinished(true);
  this->onRefreshClicked();
  this->connectBlockVisibilityObserver();
}

// ---------------------------------------------------------------------------
pqSHYXPartitionedBlockNamesWidget::~pqSHYXPartitionedBlockNamesWidget()
{
  this->disconnectBlockVisibilityObserver();
  for (const QMetaObject::Connection& c : this->RepresentationConnections)
  {
    QObject::disconnect(c);
  }
  this->RepresentationConnections.clear();
  if (this->BlockVisibilityVTKConnect)
  {
    this->BlockVisibilityVTKConnect->Delete();
    this->BlockVisibilityVTKConnect = nullptr;
  }
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::setView(pqView* view)
{
  this->Superclass::setView(view);
  this->connectBlockVisibilityObserver();
}

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
      pqSHYXPartitionedBlockNamesWidget::BlockRow row;
      row.Type = tr("Block");
      row.Name = name;
      rows.push_back(row);
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
      BlockRow row;
      row.Type = tr("Block");
      row.Name = names[i];
      row.WriteNormal = i < writeNormals.size() ? writeNormals[i] != 0 : false;
      row.Variables = i < variables.size() ? variables[i] : QStringList{ QString() };
      rows.push_back(row);
    }
    this->rebuildRows(rows);
    return;
  }

  {
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
            : QString();
          item->setText(value);
        }
      }
    }
    this->syncNodeRowsFromSideRows();
  }
  this->updateEyeIcons();
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
  {
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
    auto* visibilityItem = new QStandardItem();
    visibilityItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    visibilityItem->setTextAlignment(Qt::AlignCenter);
    visibilityItem->setIcon(eyeIcon(true));
    visibilityItem->setData(rows[row].SelectorPath, kSelectorPathRole);
    visibilityItem->setData(rows[row].DataSetIndex, kDataSetIndexRole);
    visibilityItem->setData(true, kVisibleRole);
    visibilityItem->setToolTip(tr("Show/hide this block in the active view (side/node pairs stay linked)"));

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

    QList<QStandardItem*> items = { visibilityItem, indexItem, typeItem, nameItem, writeNormalItem };
    for (int c = 0; c < this->VariableColumnCount; ++c)
    {
      const QString value = (c < rows[row].Variables.size() && !rows[row].Variables[c].isEmpty())
        ? rows[row].Variables[c]
        : QString();
      auto* variableItem = new QStandardItem(isSideSet ? value : QString());
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
  this->updateEyeIcons();
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
      const QString text = item ? item->text().trimmed() : QString();
      const double value = text.toDouble(&ok);
      variables.push_back(ok ? QString::number(value, 'g', 16) : QString());
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
        nodeVariable->setText(sideVariable ? sideVariable->text() : QString());
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

  QStringList labels = { QString(), tr("#"), tr("Type"), tr("Name"), tr("Write Normal") };
  for (int i = 0; i < count; ++i)
  {
    labels.push_back(tr("Variable%1").arg(i + 1));
  }
  this->Model->setHorizontalHeaderLabels(labels);
  this->Model->setHeaderData(kColVisibility, Qt::Horizontal, eyeIcon(true), Qt::DecorationRole);
  this->Model->setHeaderData(kColVisibility, Qt::Horizontal,
    tr("Show or hide all listed blocks in the active view"), Qt::ToolTipRole);

  if (this->View && this->View->header())
  {
    auto* header = this->View->header();
    header->setSectionsClickable(true);
    header->setSectionResizeMode(kColVisibility, QHeaderView::ResizeToContents);
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
        auto* item = new QStandardItem(QString());
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

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onViewClicked(const QModelIndex& index)
{
  if (!index.isValid() || index.column() != kColVisibility)
  {
    return;
  }
  this->toggleRowVisibility(index.row());
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onHeaderSectionClicked(int logicalIndex)
{
  if (logicalIndex != kColVisibility)
  {
    return;
  }
  this->toggleAllVisibility();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onBlockVisibilityModified()
{
  if (this->UpdatingBlockVisibility)
  {
    return;
  }
  this->updateEyeIcons();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::onActiveViewOrRepresentationChanged()
{
  this->connectBlockVisibilityObserver();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::disconnectBlockVisibilityObserver()
{
  if (this->BlockVisibilityVTKConnect)
  {
    this->BlockVisibilityVTKConnect->Disconnect();
  }
  this->ObservedRepresentation = nullptr;
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::connectBlockVisibilityObserver()
{
  pqDataRepresentation* repr = this->currentRepresentation();
  if (repr == this->ObservedRepresentation)
  {
    this->updateEyeIcons();
    return;
  }

  this->disconnectBlockVisibilityObserver();
  this->ObservedRepresentation = repr;
  if (!repr || !this->BlockVisibilityVTKConnect)
  {
    this->updateEyeIcons();
    return;
  }

  if (vtkSMStringVectorProperty* prop = this->visibilityProperty(repr))
  {
    this->BlockVisibilityVTKConnect->Connect(
      prop, vtkCommand::ModifiedEvent, this, SLOT(onBlockVisibilityModified()));
  }
  this->updateEyeIcons();
}

// ---------------------------------------------------------------------------
pqDataRepresentation* pqSHYXPartitionedBlockNamesWidget::currentRepresentation() const
{
  auto* smm = pqApplicationCore::instance()->getServerManagerModel();
  auto* src = smm ? smm->findItem<pqPipelineSource*>(this->proxy()) : nullptr;
  if (!src)
  {
    return nullptr;
  }

  pqView* view = this->view();
  if (!view)
  {
    view = pqActiveObjects::instance().activeView();
  }
  if (view)
  {
    if (auto* repr = src->getRepresentation(0, view))
    {
      return repr;
    }
  }

  const QList<pqView*> views = src->getViews();
  for (pqView* candidate : views)
  {
    if (auto* repr = src->getRepresentation(0, candidate))
    {
      return repr;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
vtkSMStringVectorProperty* pqSHYXPartitionedBlockNamesWidget::visibilityProperty(
  pqDataRepresentation* repr) const
{
  return blockVisibilityProperty(repr ? repr->getProxy() : nullptr);
}

// ---------------------------------------------------------------------------
vtkDataAssembly* pqSHYXPartitionedBlockNamesWidget::activeAssembly(
  pqDataRepresentation* repr) const
{
  if (!repr)
  {
    return nullptr;
  }

  vtkPVDataInformation* info = repr->getInputDataInformation();
  if (!info)
  {
    auto* source = vtkSMSourceProxy::SafeDownCast(this->proxy());
    info = source ? source->GetDataInformation(0) : nullptr;
  }
  if (!info)
  {
    return nullptr;
  }

  vtkSMProxy* reprProxy = repr->getProxy();
  const char* assemblyName = nullptr;
  if (reprProxy && reprProxy->GetProperty("Assembly"))
  {
    assemblyName = vtkSMPropertyHelper(reprProxy, "Assembly").GetAsString();
  }
  if (assemblyName && assemblyName[0] != '\0')
  {
    return info->GetDataAssembly(assemblyName);
  }
  return info->GetDataAssembly();
}

// ---------------------------------------------------------------------------
QString pqSHYXPartitionedBlockNamesWidget::selectorForRow(int row) const
{
  if (!this->Model)
  {
    return {};
  }
  auto* visItem = this->Model->item(row, kColVisibility);
  const QString stored = visItem ? visItem->data(kSelectorPathRole).toString() : QString();
  auto* repr = this->currentRepresentation();
  vtkDataAssembly* assembly = this->activeAssembly(repr);
  if (!assembly)
  {
    return stored;
  }

  if (!stored.isEmpty() && assembly->GetFirstNodeByPath(stored.toUtf8().constData()) >= 0)
  {
    return stored;
  }

  const int dsIndex = visItem ? visItem->data(kDataSetIndexRole).toInt() : -1;
  if (dsIndex >= 0)
  {
    const int node =
      findNodeByDataSetIndex(assembly, vtkDataAssembly::GetRootNode(), static_cast<unsigned int>(dsIndex));
    if (node >= 0)
    {
      return QString::fromStdString(assembly->GetNodePath(node));
    }
  }
  return stored;
}

// ---------------------------------------------------------------------------
int pqSHYXPartitionedBlockNamesWidget::pairedRow(int row) const
{
  if (!this->Model || row < 0 || row >= this->Model->rowCount())
  {
    return -1;
  }

  QList<int> sideRows;
  QList<int> nodeRows;
  for (int r = 0; r < this->Model->rowCount(); ++r)
  {
    auto* typeItem = this->Model->item(r, kColType);
    if (!typeItem)
    {
      continue;
    }
    if (typeItem->text() == tr("Side set"))
    {
      sideRows.push_back(r);
    }
    else if (typeItem->text() == tr("Node set"))
    {
      nodeRows.push_back(r);
    }
  }

  const int nPairs = std::min(sideRows.size(), nodeRows.size());
  for (int i = 0; i < nPairs; ++i)
  {
    if (sideRows[i] == row)
    {
      return nodeRows[i];
    }
    if (nodeRows[i] == row)
    {
      return sideRows[i];
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::updateEyeIcons()
{
  if (!this->Model)
  {
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  auto* repr = this->currentRepresentation();
  vtkDataAssembly* assembly = this->activeAssembly(repr);
  vtkSMStringVectorProperty* prop = this->visibilityProperty(repr);

  pqDataAssemblyTreeModel treeModel;
  bool haveTree = false;
  if (assembly)
  {
    treeModel.setUserCheckable(true);
    treeModel.setDataAssembly(assembly);
    treeModel.setCheckedNodes(checkedSelectorsFromProperty(prop, assembly));
    haveTree = true;
  }

  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* item = this->Model->item(row, kColVisibility);
    if (!item)
    {
      continue;
    }

    bool visible = !haveTree;
    if (haveTree)
    {
      const QString path = this->selectorForRow(row);
      if (!path.isEmpty())
      {
        const std::vector<int> nodes = assembly->SelectNodes({ path.toStdString() });
        if (!nodes.empty())
        {
          const QModelIndex idx = treeModel.index(nodes.front());
          visible = treeModel.data(idx, Qt::CheckStateRole).toInt() != Qt::Unchecked;
        }
      }
    }

    const QVariant previous = item->data(kVisibleRole);
    if (previous.isValid() && previous.toBool() == visible && !item->icon().isNull())
    {
      continue;
    }

    item->setIcon(eyeIcon(visible));
    item->setData(visible, kVisibleRole);
    item->setToolTip(visible
        ? tr("Hide this block in the active view (side/node pairs stay linked)")
        : tr("Show this block in the active view (side/node pairs stay linked)"));
  }

  if (this->View)
  {
    this->View->viewport()->repaint();
    if (this->View->header())
    {
      const Qt::CheckState headerState = this->headerVisibilityState();
      this->Model->setHeaderData(
        kColVisibility, Qt::Horizontal, eyeIcon(headerState), Qt::DecorationRole);
      QString tip;
      switch (headerState)
      {
        case Qt::Checked:
          tip = tr("Hide all listed blocks in the active view");
          break;
        case Qt::Unchecked:
          tip = tr("Show all listed blocks in the active view");
          break;
        default:
          tip = tr("Some blocks are hidden. Click to show all listed blocks");
          break;
      }
      this->Model->setHeaderData(kColVisibility, Qt::Horizontal, tip, Qt::ToolTipRole);
      this->View->header()->viewport()->repaint();
    }
  }
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::toggleRowVisibility(int row)
{
  if (!this->Model || row < 0 || row >= this->Model->rowCount())
  {
    return;
  }

  auto* visItem = this->Model->item(row, kColVisibility);
  const bool currentlyVisible = visItem ? visItem->data(kVisibleRole).toBool() : true;
  QList<int> rows = { row };
  const int pair = this->pairedRow(row);
  if (pair >= 0 && pair != row)
  {
    rows.push_back(pair);
  }
  this->setBlocksVisible(rows, !currentlyVisible);
}

// ---------------------------------------------------------------------------
Qt::CheckState pqSHYXPartitionedBlockNamesWidget::headerVisibilityState() const
{
  if (!this->Model || this->Model->rowCount() == 0)
  {
    return Qt::Checked;
  }

  int visibleCount = 0;
  int totalCount = 0;
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* item = this->Model->item(row, kColVisibility);
    if (!item)
    {
      continue;
    }
    ++totalCount;
    if (item->data(kVisibleRole).toBool())
    {
      ++visibleCount;
    }
  }

  if (totalCount == 0 || visibleCount == totalCount)
  {
    return Qt::Checked;
  }
  if (visibleCount == 0)
  {
    return Qt::Unchecked;
  }
  return Qt::PartiallyChecked;
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::toggleAllVisibility()
{
  if (!this->Model || this->Model->rowCount() == 0)
  {
    return;
  }

  if (this->headerVisibilityState() == Qt::Checked)
  {
    QList<int> rows;
    rows.reserve(this->Model->rowCount());
    for (int row = 0; row < this->Model->rowCount(); ++row)
    {
      rows.push_back(row);
    }
    this->setBlocksVisible(rows, false);
    return;
  }

  auto* repr = this->currentRepresentation();
  vtkSMProxy* reprProxy = repr ? repr->getProxy() : nullptr;
  vtkSMStringVectorProperty* prop = this->visibilityProperty(repr);
  if (!repr || !reprProxy || !prop)
  {
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingBlockVisibility, true);
  SM_SCOPED_TRACE(PropertiesModified).arg("proxy", reprProxy);
  BEGIN_UNDO_SET(tr("Show All Blocks"));
  prop->SetElements(std::vector<std::string>({ "/" }));
  reprProxy->UpdateVTKObjects();
  END_UNDO_SET();
  repr->renderViewEventually();
  this->updateEyeIcons();
}

// ---------------------------------------------------------------------------
void pqSHYXPartitionedBlockNamesWidget::setBlocksVisible(const QList<int>& rows, bool visible)
{
  auto* repr = this->currentRepresentation();
  vtkSMProxy* reprProxy = repr ? repr->getProxy() : nullptr;
  vtkSMStringVectorProperty* prop = this->visibilityProperty(repr);
  vtkDataAssembly* assembly = this->activeAssembly(repr);
  if (!repr || !reprProxy || !prop || !assembly)
  {
    return;
  }

  QList<int> nodeIds;
  for (int row : rows)
  {
    const QString path = this->selectorForRow(row);
    if (path.isEmpty())
    {
      continue;
    }
    const std::vector<int> nodes = assembly->SelectNodes({ path.toStdString() });
    for (int id : nodes)
    {
      nodeIds.push_back(id);
    }
  }
  if (nodeIds.isEmpty())
  {
    return;
  }

  pqDataAssemblyTreeModel treeModel;
  treeModel.setUserCheckable(true);
  treeModel.setDataAssembly(assembly);
  treeModel.setCheckedNodes(checkedSelectorsFromProperty(prop, assembly));

  const QModelIndexList indexes = treeModel.index(nodeIds);
  for (const QModelIndex& idx : indexes)
  {
    treeModel.setData(idx, visible ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
  }

  const QStringList checkedNodes = treeModel.checkedNodes();
  std::vector<std::string> values(static_cast<size_t>(checkedNodes.size()));
  for (int i = 0; i < checkedNodes.size(); ++i)
  {
    values[static_cast<size_t>(i)] = checkedNodes[i].toStdString();
  }

  QScopedValueRollback<bool> guard(this->UpdatingBlockVisibility, true);
  SM_SCOPED_TRACE(PropertiesModified).arg("proxy", reprProxy);
  BEGIN_UNDO_SET(visible ? tr("Show Block") : tr("Hide Block"));
  prop->SetElements(values);
  reprProxy->UpdateVTKObjects();
  END_UNDO_SET();
  repr->renderViewEventually();
  this->updateEyeIcons();
}
