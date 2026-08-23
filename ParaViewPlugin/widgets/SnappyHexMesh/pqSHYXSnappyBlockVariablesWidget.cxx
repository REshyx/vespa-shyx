#include "pqSHYXSnappyBlockVariablesWidget.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqDataAssemblyTreeModel.h"
#include "pqDataRepresentation.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"
#include "pqUndoStack.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkCommand.h"
#include "vtkCompositeDataSet.h"
#include "vtkDataAssembly.h"
#include "vtkDataObject.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkInformation.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkPVDataInformation.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"
#include "vtkSMTrace.h"

#include <QAbstractItemView>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMap>
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

#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr int kColVisibility = 0;
constexpr int kColIndex = 1;
constexpr int kColType = 2;
constexpr int kColName = 3;
constexpr int kFirstVariableCol = 4;
constexpr int kSelectorPathRole = Qt::UserRole;
constexpr int kDataSetIndexRole = Qt::UserRole + 1;
constexpr int kVisibleRole = Qt::UserRole + 2;

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
  auto* visibility =
    vtkSMStringVectorProperty::SafeDownCast(reprProxy->GetProperty("BlockVisibilities"));
  return visibility
    ? visibility
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
    return {};
  }
  const char* label = nullptr;
  if (assembly->GetAttribute(node, "label", label) && label && label[0] != '\0')
  {
    return QString::fromUtf8(label);
  }
  const char* name = assembly->GetNodeName(node);
  return QString::fromUtf8(name ? name : "");
}

bool isInternalMeshName(const QString& name)
{
  const QString lower = name.toLower();
  return lower == QLatin1String("internalmesh") || lower.contains(QLatin1String("internalmesh"));
}

void collectLeaves(vtkDataAssembly* assembly, int node, QList<pqSHYXSnappyBlockVariablesWidget::BlockRow>& rows)
{
  if (!assembly || node < 0)
  {
    return;
  }
  const int n = assembly->GetNumberOfChildren(node);
  if (n > 0)
  {
    for (int i = 0; i < n; ++i)
    {
      collectLeaves(assembly, assembly->GetChild(node, i), rows);
    }
    return;
  }

  const QString name = labelForNode(assembly, node);
  if (name.isEmpty() || name.compare(QLatin1String("Root"), Qt::CaseInsensitive) == 0)
  {
    return;
  }

  pqSHYXSnappyBlockVariablesWidget::BlockRow row;
  row.Name = name;
  row.Type = isInternalMeshName(name)
    ? pqSHYXSnappyBlockVariablesWidget::tr("Internal mesh")
    : pqSHYXSnappyBlockVariablesWidget::tr("Patch");
  row.SelectorPath = QString::fromStdString(assembly->GetNodePath(node));
  const std::vector<unsigned int> indices =
    assembly->GetDataSetIndices(node, /*traverse_subtree=*/false);
  row.DataSetIndex = indices.empty() ? -1 : static_cast<int>(indices.front());
  row.Variables = QStringList{ QString() };
  rows.push_back(row);
}

void collectCompositeLeaves(vtkDataObject* obj, const QString& name, const QString& selector,
  int& dataSetIndex, QList<pqSHYXSnappyBlockVariablesWidget::BlockRow>& rows)
{
  if (!obj)
  {
    return;
  }
  if (auto* mb = vtkMultiBlockDataSet::SafeDownCast(obj))
  {
    const unsigned int n = mb->GetNumberOfBlocks();
    for (unsigned int i = 0; i < n; ++i)
    {
      QString childName;
      if (vtkInformation* meta = mb->GetMetaData(i))
      {
        if (const char* raw = meta->Get(vtkCompositeDataSet::NAME()))
        {
          childName = QString::fromUtf8(raw);
        }
      }
      if (childName.isEmpty())
      {
        childName = QString::number(static_cast<int>(i));
      }
      const QString childSelector = selector.isEmpty()
        ? QStringLiteral("/") + childName
        : selector + QLatin1Char('/') + childName;
      collectCompositeLeaves(mb->GetBlock(i), childName, childSelector, dataSetIndex, rows);
    }
    return;
  }

  if (name.isEmpty() || name.compare(QLatin1String("Root"), Qt::CaseInsensitive) == 0)
  {
    return;
  }
  pqSHYXSnappyBlockVariablesWidget::BlockRow row;
  row.Name = name;
  row.Type = isInternalMeshName(name)
    ? pqSHYXSnappyBlockVariablesWidget::tr("Internal mesh")
    : pqSHYXSnappyBlockVariablesWidget::tr("Patch");
  row.SelectorPath = selector;
  row.DataSetIndex = dataSetIndex++;
  row.Variables = QStringList{ QString() };
  rows.push_back(row);
}

vtkDataAssembly* assemblyFromInformation(vtkPVDataInformation* info)
{
  if (!info)
  {
    return nullptr;
  }
  if (vtkDataAssembly* dataAsm = info->GetDataAssembly())
  {
    return dataAsm;
  }
  return info->GetHierarchy();
}
}

pqSHYXSnappyBlockVariablesWidget::pqSHYXSnappyBlockVariablesWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  auto* tip = new QLabel(
    tr("After a successful mesh Apply, click Refresh to list internalMesh and patches. "
       "Add Variable / Delete Variable columns. Finite values are written to "
       "0/shyx_BoundaryVariable1/... (empty / NaN → 0). Changing only these values skips "
       "snappyHexMesh. Click the eye to show or hide that block in the active view."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, kFirstVariableCol + this->VariableColumnCount, this);
  this->setVariableColumnCount(this->VariableColumnCount);

  this->View = new QTreeView(this);
  this->View->setObjectName("SHYXSnappyBlockVariables");
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
  vbox->addWidget(this->View, 1);

  auto* buttons = new QHBoxLayout();
  auto* refresh = new QPushButton(tr("Refresh from current output"), this);
  refresh->setToolTip(tr("Fill rows from the filter output (internalMesh and patches)."));
  buttons->addWidget(refresh);
  auto* addVariable = new QPushButton(tr("Add variable"), this);
  buttons->addWidget(addVariable);
  auto* deleteVariable = new QPushButton(tr("Delete variable"), this);
  buttons->addWidget(deleteVariable);
  buttons->addStretch(1);
  vbox->addLayout(buttons);

  QObject::connect(this->Model, &QStandardItemModel::itemChanged, this,
    &pqSHYXSnappyBlockVariablesWidget::onItemChanged);
  QObject::connect(this->View, &QTreeView::clicked, this,
    &pqSHYXSnappyBlockVariablesWidget::onViewClicked);
  QObject::connect(this->View->header(), &QHeaderView::sectionClicked, this,
    &pqSHYXSnappyBlockVariablesWidget::onHeaderSectionClicked);
  QObject::connect(refresh, &QPushButton::clicked, this,
    &pqSHYXSnappyBlockVariablesWidget::onRefreshClicked);
  QObject::connect(addVariable, &QPushButton::clicked, this,
    &pqSHYXSnappyBlockVariablesWidget::onAddVariableClicked);
  QObject::connect(deleteVariable, &QPushButton::clicked, this,
    &pqSHYXSnappyBlockVariablesWidget::onDeleteVariableClicked);

  vtkSMProperty* namesProp = propertyFromGroup(smgroup, smproxy, "Names", "BlockNames");
  if (namesProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(namesProp) : nullptr;
    this->NamesPropertyName = QString::fromUtf8(pname ? pname : "BlockNames");
    this->addPropertyLink(
      this, this->NamesPropertyName.toUtf8().data(), SIGNAL(blockVariablesChanged()), namesProp);
  }
  vtkSMProperty* varsProp =
    propertyFromGroup(smgroup, smproxy, "BoundaryVariables", "BoundaryVariables");
  if (varsProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(varsProp) : nullptr;
    this->BoundaryVariablesPropertyName =
      QString::fromUtf8(pname ? pname : "BoundaryVariables");
    this->addPropertyLink(this, this->BoundaryVariablesPropertyName.toUtf8().data(),
      SIGNAL(blockVariablesChanged()), varsProp);
  }

  this->BlockVisibilityVTKConnect = vtkEventQtSlotConnect::New();
  QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::viewChanged, this,
    &pqSHYXSnappyBlockVariablesWidget::onActiveViewOrRepresentationChanged);
  QObject::connect(&pqActiveObjects::instance(),
    QOverload<pqDataRepresentation*>::of(&pqActiveObjects::representationChanged), this,
    &pqSHYXSnappyBlockVariablesWidget::onActiveViewOrRepresentationChanged);
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
        &pqSHYXSnappyBlockVariablesWidget::onOutputDataUpdated));
    }
  }

  this->setChangeAvailableAsChangeFinished(true);
  // Do not Refresh/UpdatePipeline here: creating the filter would otherwise
  // execute snappyHexMesh before the user clicks Apply.
  this->rebuildFromProperty();
  this->connectBlockVisibilityObserver();
}

pqSHYXSnappyBlockVariablesWidget::~pqSHYXSnappyBlockVariablesWidget()
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

void pqSHYXSnappyBlockVariablesWidget::setView(pqView* view)
{
  this->Superclass::setView(view);
  this->connectBlockVisibilityObserver();
}

bool pqSHYXSnappyBlockVariablesWidget::event(QEvent* e)
{
  if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
  {
    auto* devt = static_cast<QDynamicPropertyChangeEvent*>(e);
    const QString name = QString::fromLatin1(devt->propertyName());
    if (name == this->NamesPropertyName || name == this->BoundaryVariablesPropertyName)
    {
      this->rebuildFromProperty();
      return true;
    }
  }
  return this->Superclass::event(e);
}

void pqSHYXSnappyBlockVariablesWidget::apply()
{
  this->writeBackProperty();
  this->Superclass::apply();
}

void pqSHYXSnappyBlockVariablesWidget::reset()
{
  this->Superclass::reset();
  this->rebuildFromProperty();
}

void pqSHYXSnappyBlockVariablesWidget::onItemChanged(QStandardItem* item)
{
  if (!item || this->UpdatingFromProperty || item->column() < kFirstVariableCol)
  {
    return;
  }
  this->writeBackProperty();
}

void pqSHYXSnappyBlockVariablesWidget::onRefreshClicked()
{
  QList<BlockRow> rows = this->collectCurrentOutputNames();
  const QMap<QString, QStringList> varsByName = this->variablesByName();
  const QList<QString> propNames = this->currentNamesFromProperty();
  const QList<QStringList> propVars = this->currentBoundaryVariablesFromProperty();

  auto assignVars = [&](BlockRow& row, int propIndex)
  {
    if (varsByName.contains(row.Name) && !varsByName.value(row.Name).isEmpty())
    {
      row.Variables = varsByName.value(row.Name);
    }
    else if (propIndex >= 0 && propIndex < propVars.size() && !propVars[propIndex].isEmpty())
    {
      row.Variables = propVars[propIndex];
    }
  };

  for (int i = 0; i < rows.size(); ++i)
  {
    int propIndex = -1;
    for (int j = 0; j < propNames.size(); ++j)
    {
      if (propNames[j].compare(rows[i].Name, Qt::CaseInsensitive) == 0)
      {
        propIndex = j;
        break;
      }
    }
    assignVars(rows[i], propIndex);
  }

  if (rows.isEmpty() && !propNames.isEmpty())
  {
    for (int i = 0; i < propNames.size(); ++i)
    {
      BlockRow row;
      row.Type = isInternalMeshName(propNames[i]) ? tr("Internal mesh") : tr("Patch");
      row.Name = propNames[i];
      assignVars(row, i);
      rows.push_back(row);
    }
  }

  this->rebuildRows(rows);
}

void pqSHYXSnappyBlockVariablesWidget::onOutputDataUpdated()
{
  this->onRefreshClicked();
  this->updateEyeIcons();
}

void pqSHYXSnappyBlockVariablesWidget::onAddVariableClicked()
{
  this->setVariableColumnCount(this->VariableColumnCount + 1);
  this->writeBackProperty();
}

void pqSHYXSnappyBlockVariablesWidget::onDeleteVariableClicked()
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
  this->writeBackProperty();
}

void pqSHYXSnappyBlockVariablesWidget::rebuildFromProperty()
{
  if (!this->Model || this->NamesPropertyName.isEmpty())
  {
    return;
  }

  const QList<QString> names = this->currentNamesFromProperty();
  const QList<QStringList> variables = this->currentBoundaryVariablesFromProperty();
  QList<BlockRow> rows;
  rows.reserve(names.size());
  for (int i = 0; i < names.size(); ++i)
  {
    BlockRow row;
    row.Type = isInternalMeshName(names[i]) ? tr("Internal mesh") : tr("Patch");
    row.Name = names[i];
    row.Variables = i < variables.size() ? variables[i] : QStringList{ QString() };
    rows.push_back(row);
  }
  this->rebuildRows(rows);
}

void pqSHYXSnappyBlockVariablesWidget::rebuildRows(const QList<BlockRow>& rows)
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
    auto* visibilityItem = new QStandardItem();
    visibilityItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    visibilityItem->setTextAlignment(Qt::AlignCenter);
    visibilityItem->setIcon(eyeIcon(true));
    visibilityItem->setData(rows[row].SelectorPath, kSelectorPathRole);
    visibilityItem->setData(rows[row].DataSetIndex, kDataSetIndexRole);
    visibilityItem->setData(true, kVisibleRole);

    auto* indexItem = new QStandardItem(QString::number(row));
    indexItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    indexItem->setTextAlignment(Qt::AlignCenter);

    auto* typeItem = new QStandardItem(rows[row].Type);
    typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);

    auto* nameItem = new QStandardItem(rows[row].Name);
    nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);

    QList<QStandardItem*> items = { visibilityItem, indexItem, typeItem, nameItem };
    for (int c = 0; c < this->VariableColumnCount; ++c)
    {
      const QString value =
        (c < rows[row].Variables.size()) ? rows[row].Variables[c] : QString();
      auto* variableItem = new QStandardItem(value);
      variableItem->setFlags(
        Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemNeverHasChildren);
      variableItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      items.push_back(variableItem);
    }
    this->Model->appendRow(items);
  }
  this->updateEyeIcons();
}

void pqSHYXSnappyBlockVariablesWidget::writeBackProperty()
{
  if (this->NamesPropertyName.isEmpty() || !this->Model)
  {
    return;
  }

  QStringList names;
  QStringList variableRows;
  names.reserve(this->Model->rowCount());
  variableRows.reserve(this->Model->rowCount());
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* nameItem = this->Model->item(row, kColName);
    names.push_back(nameItem ? nameItem->text().trimmed() : QString());
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
      this->setProperty(this->BoundaryVariablesPropertyName.toUtf8().data(),
        variableRows.join(QLatin1Char('\n')));
    }
  }
  Q_EMIT this->blockVariablesChanged();
}

void pqSHYXSnappyBlockVariablesWidget::setVariableColumnCount(int count)
{
  if (!this->Model)
  {
    return;
  }
  count = std::max(1, count);
  this->VariableColumnCount = count;
  this->Model->setColumnCount(kFirstVariableCol + count);

  QStringList labels = { QString(), tr("#"), tr("Type"), tr("Name") };
  for (int i = 0; i < count; ++i)
  {
    labels.push_back(tr("Variable %1").arg(i + 1));
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
    for (int c = 0; c < count; ++c)
    {
      header->setSectionResizeMode(kFirstVariableCol + c, QHeaderView::ResizeToContents);
    }
    header->setStretchLastSection(false);
  }

  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    for (int c = 0; c < count; ++c)
    {
      if (!this->Model->item(row, kFirstVariableCol + c))
      {
        auto* variableItem = new QStandardItem();
        variableItem->setFlags(
          Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemNeverHasChildren);
        variableItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        this->Model->setItem(row, kFirstVariableCol + c, variableItem);
      }
    }
  }
}

QList<QString> pqSHYXSnappyBlockVariablesWidget::currentNamesFromProperty() const
{
  QList<QString> names;
  if (this->NamesPropertyName.isEmpty())
  {
    return names;
  }
  const QString text = this->property(this->NamesPropertyName.toUtf8().data()).toString();
  const QStringList split = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  for (QString name : split)
  {
    if (name.endsWith(QLatin1Char('\r')))
    {
      name.chop(1);
    }
    names.push_back(name.trimmed());
  }
  if (names.size() == 1 && names.front().isEmpty())
  {
    names.clear();
  }
  return names;
}

QList<QStringList> pqSHYXSnappyBlockVariablesWidget::currentBoundaryVariablesFromProperty() const
{
  QList<QStringList> variables;
  if (this->BoundaryVariablesPropertyName.isEmpty())
  {
    return variables;
  }
  const QString text =
    this->property(this->BoundaryVariablesPropertyName.toUtf8().data()).toString();
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

QMap<QString, QStringList> pqSHYXSnappyBlockVariablesWidget::variablesByName() const
{
  QMap<QString, QStringList> out;
  if (this->Model && this->Model->rowCount() > 0)
  {
    for (int row = 0; row < this->Model->rowCount(); ++row)
    {
      auto* nameItem = this->Model->item(row, kColName);
      const QString name = nameItem ? nameItem->text().trimmed() : QString();
      if (name.isEmpty())
      {
        continue;
      }
      QStringList vars;
      for (int c = 0; c < this->VariableColumnCount; ++c)
      {
        auto* item = this->Model->item(row, kFirstVariableCol + c);
        vars.push_back(item ? item->text().trimmed() : QString());
      }
      out.insert(name, vars);
    }
    return out;
  }

  const QList<QString> names = this->currentNamesFromProperty();
  const QList<QStringList> vars = this->currentBoundaryVariablesFromProperty();
  for (int i = 0; i < names.size(); ++i)
  {
    if (!names[i].isEmpty())
    {
      out.insert(names[i], i < vars.size() ? vars[i] : QStringList());
    }
  }
  return out;
}

QList<pqSHYXSnappyBlockVariablesWidget::BlockRow>
pqSHYXSnappyBlockVariablesWidget::collectCurrentOutputNames() const
{
  QList<BlockRow> rows;
  auto* source = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!source)
  {
    return rows;
  }
  // Do not UpdatePipeline(): that re-runs snappyHexMesh. After Apply, ParaView
  // already gathered output info. OpenFOAM MultiBlock has no vtkDataAssembly;
  // block names live on GetHierarchy() (and the client-side MultiBlock).
  vtkPVDataInformation* info = source->GetDataInformation(0);
  if (vtkDataAssembly* assembly = assemblyFromInformation(info))
  {
    collectLeaves(assembly, vtkDataAssembly::GetRootNode(), rows);
  }
  if (!rows.isEmpty())
  {
    return rows;
  }

  auto* alg = vtkAlgorithm::SafeDownCast(source->GetClientSideObject());
  vtkDataObject* output = alg ? alg->GetOutputDataObject(0) : nullptr;
  int dataSetIndex = 0;
  collectCompositeLeaves(output, QString(), QStringLiteral("/Root"), dataSetIndex, rows);
  return rows;
}

void pqSHYXSnappyBlockVariablesWidget::onViewClicked(const QModelIndex& index)
{
  if (!index.isValid() || index.column() != kColVisibility)
  {
    return;
  }
  this->toggleRowVisibility(index.row());
}

void pqSHYXSnappyBlockVariablesWidget::onHeaderSectionClicked(int logicalIndex)
{
  if (logicalIndex != kColVisibility)
  {
    return;
  }
  this->toggleAllVisibility();
}

void pqSHYXSnappyBlockVariablesWidget::onBlockVisibilityModified()
{
  if (!this->UpdatingBlockVisibility)
  {
    this->updateEyeIcons();
  }
}

void pqSHYXSnappyBlockVariablesWidget::onActiveViewOrRepresentationChanged()
{
  this->connectBlockVisibilityObserver();
}

void pqSHYXSnappyBlockVariablesWidget::disconnectBlockVisibilityObserver()
{
  if (this->BlockVisibilityVTKConnect)
  {
    this->BlockVisibilityVTKConnect->Disconnect();
  }
  this->ObservedRepresentation = nullptr;
}

void pqSHYXSnappyBlockVariablesWidget::connectBlockVisibilityObserver()
{
  this->disconnectBlockVisibilityObserver();
  auto* repr = this->currentRepresentation();
  this->ObservedRepresentation = repr;
  vtkSMStringVectorProperty* prop = this->visibilityProperty(repr);
  if (this->BlockVisibilityVTKConnect && prop)
  {
    this->BlockVisibilityVTKConnect->Connect(prop, vtkCommand::ModifiedEvent, this,
      SLOT(onBlockVisibilityModified()));
  }
  this->updateEyeIcons();
}

pqDataRepresentation* pqSHYXSnappyBlockVariablesWidget::currentRepresentation() const
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

vtkSMStringVectorProperty* pqSHYXSnappyBlockVariablesWidget::visibilityProperty(
  pqDataRepresentation* repr) const
{
  return blockVisibilityProperty(repr ? repr->getProxy() : nullptr);
}

vtkDataAssembly* pqSHYXSnappyBlockVariablesWidget::activeAssembly(
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
    if (vtkDataAssembly* named = info->GetDataAssembly(assemblyName))
    {
      return named;
    }
    if (QString::fromUtf8(assemblyName).compare(QLatin1String("Hierarchy"), Qt::CaseInsensitive) == 0)
    {
      return info->GetHierarchy();
    }
  }
  return assemblyFromInformation(info);
}

QString pqSHYXSnappyBlockVariablesWidget::selectorForRow(int row) const
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

void pqSHYXSnappyBlockVariablesWidget::updateEyeIcons()
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
    item->setIcon(eyeIcon(visible));
    item->setData(visible, kVisibleRole);
    item->setToolTip(visible ? tr("Hide this block in the active view")
                             : tr("Show this block in the active view"));
  }

  if (this->View && this->View->header())
  {
    const Qt::CheckState headerState = this->headerVisibilityState();
    this->Model->setHeaderData(
      kColVisibility, Qt::Horizontal, eyeIcon(headerState), Qt::DecorationRole);
    QString tip;
    switch (headerState)
    {
      case Qt::Checked:
        tip = tr("All listed blocks are shown. Click to hide all");
        break;
      case Qt::Unchecked:
        tip = tr("All listed blocks are hidden. Click to show all");
        break;
      default:
        tip = tr("Some blocks are hidden. Click to show all listed blocks");
        break;
    }
    this->Model->setHeaderData(kColVisibility, Qt::Horizontal, tip, Qt::ToolTipRole);
    this->View->header()->viewport()->repaint();
  }
}

void pqSHYXSnappyBlockVariablesWidget::toggleRowVisibility(int row)
{
  if (!this->Model || row < 0 || row >= this->Model->rowCount())
  {
    return;
  }
  auto* visItem = this->Model->item(row, kColVisibility);
  const bool currentlyVisible = visItem ? visItem->data(kVisibleRole).toBool() : true;
  this->setBlocksVisible({ row }, !currentlyVisible);
}

Qt::CheckState pqSHYXSnappyBlockVariablesWidget::headerVisibilityState() const
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

void pqSHYXSnappyBlockVariablesWidget::toggleAllVisibility()
{
  if (!this->Model || this->Model->rowCount() == 0)
  {
    return;
  }
  QList<int> rows;
  rows.reserve(this->Model->rowCount());
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    rows.push_back(row);
  }
  this->setBlocksVisible(rows, this->headerVisibilityState() != Qt::Checked);
}

void pqSHYXSnappyBlockVariablesWidget::setBlocksVisible(const QList<int>& rows, bool visible)
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
