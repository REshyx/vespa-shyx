#include "pqSHYXSelectionPatchTableWidget.h"

#include "pqActiveObjects.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqSelectionManager.h"
#include "pqTreeView.h"

#include "vtkAlgorithm.h"
#include "vtkConvertSelection.h"
#include "vtkDataSet.h"
#include "vtkIdTypeArray.h"
#include "vtkNew.h"
#include "vtkSMInputProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"
#include "vtkSelection.h"

#include <QAbstractItemView>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace
{
constexpr int kColIndex = 0;
constexpr int kColName = 1;
constexpr int kColCells = 2;

vtkSMProperty* propertyFromGroup(
  vtkSMPropertyGroup* group, vtkSMProxy* proxy, const char* function, const char* fallbackName)
{
  if (group)
  {
    if (auto* p = group->GetProperty(function))
    {
      return p;
    }
  }
  return proxy ? proxy->GetProperty(fallbackName) : nullptr;
}

QString compactIdList(const std::vector<vtkIdType>& ids)
{
  if (ids.empty())
  {
    return QString();
  }
  std::vector<vtkIdType> sorted = ids;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  QStringList parts;
  vtkIdType runStart = sorted.front();
  vtkIdType runEnd = runStart;
  for (size_t i = 1; i < sorted.size(); ++i)
  {
    if (sorted[i] == runEnd + 1)
    {
      runEnd = sorted[i];
      continue;
    }
    if (runStart == runEnd)
    {
      parts << QString::number(static_cast<qint64>(runStart));
    }
    else
    {
      parts << QStringLiteral("%1-%2")
                 .arg(static_cast<qint64>(runStart))
                 .arg(static_cast<qint64>(runEnd));
    }
    runStart = runEnd = sorted[i];
  }
  if (runStart == runEnd)
  {
    parts << QString::number(static_cast<qint64>(runStart));
  }
  else
  {
    parts << QStringLiteral("%1-%2")
               .arg(static_cast<qint64>(runStart))
               .arg(static_cast<qint64>(runEnd));
  }
  return parts.join(QLatin1Char(','));
}

std::vector<vtkIdType> parseCompactIds(const QString& text)
{
  std::vector<vtkIdType> ids;
  const QStringList tokens = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
  for (QString token : tokens)
  {
    token = token.trimmed();
    const int dash = token.indexOf(QLatin1Char('-'));
    if (dash > 0 && dash + 1 < token.size())
    {
      bool okA = false;
      bool okB = false;
      const qint64 a = token.left(dash).toLongLong(&okA);
      const qint64 b = token.mid(dash + 1).toLongLong(&okB);
      if (okA && okB)
      {
        const qint64 lo = a < b ? a : b;
        const qint64 hi = a < b ? b : a;
        for (qint64 v = lo; v <= hi; ++v)
        {
          ids.push_back(static_cast<vtkIdType>(v));
        }
        continue;
      }
    }
    bool ok = false;
    const qint64 v = token.toLongLong(&ok);
    if (ok)
    {
      ids.push_back(static_cast<vtkIdType>(v));
    }
  }
  return ids;
}

int countCompactIds(const QString& text)
{
  return static_cast<int>(parseCompactIds(text).size());
}

vtkDataSet* clientInputDataSet(vtkSMProxy* filter)
{
  if (!filter)
  {
    return nullptr;
  }
  auto* inputProp = vtkSMInputProperty::SafeDownCast(filter->GetProperty("Input"));
  if (!inputProp || inputProp->GetNumberOfProxies() == 0)
  {
    return nullptr;
  }
  auto* src = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
  if (!src)
  {
    return nullptr;
  }
  auto* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  if (!alg)
  {
    return nullptr;
  }
  const unsigned int port = inputProp->GetOutputPortForConnection(0);
  return vtkDataSet::SafeDownCast(alg->GetOutputDataObject(static_cast<int>(port)));
}

bool collectActiveCellIds(vtkDataSet* inputDs, std::vector<vtkIdType>& ids, QString& error)
{
  ids.clear();
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqSelectionManager* selMgr = core ? core->selectionManager() : nullptr;
  if (!selMgr || !selMgr->hasActiveSelection())
  {
    error = QObject::tr("No active view selection.");
    return false;
  }
  pqOutputPort* port = selMgr->getSelectedPort();
  vtkSMSourceProxy* selIn = port ? port->getSelectionInput() : nullptr;
  if (!selIn)
  {
    error = QObject::tr("Active selection has no selection source.");
    return false;
  }
  selIn->UpdatePipeline();
  auto* selAlg = vtkAlgorithm::SafeDownCast(selIn->GetClientSideObject());
  vtkSelection* selection =
    selAlg ? vtkSelection::SafeDownCast(selAlg->GetOutputDataObject(0)) : nullptr;
  if (!selection)
  {
    error = QObject::tr("Could not read vtkSelection.");
    return false;
  }
  if (!inputDs)
  {
    error = QObject::tr("Filter Input has no client-side vtkDataSet.");
    return false;
  }

  vtkNew<vtkIdTypeArray> selected;
  vtkConvertSelection::GetSelectedCells(selection, inputDs, selected);
  const vtkIdType n = selected->GetNumberOfTuples();
  ids.reserve(static_cast<size_t>(n));
  const vtkIdType nMesh = inputDs->GetNumberOfCells();
  for (vtkIdType i = 0; i < n; ++i)
  {
    const vtkIdType cid = selected->GetValue(i);
    if (cid >= 0 && cid < nMesh)
    {
      ids.push_back(cid);
    }
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  if (ids.empty())
  {
    error = QObject::tr("View selection did not resolve to cell ids on the filter Input.");
    return false;
  }
  return true;
}
}

pqSHYXSelectionPatchTableWidget::pqSHYXSelectionPatchTableWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  auto* tip = new QLabel(
    tr("Select cells on the Input in the 3D view, then Add (Copy Active Selection is not needed). "
       "Rename only (any name). The table keeps every row. Apply merges same names into one "
       "output patch and keeps the earlier mark. Unique names are marked 0, 1, 2, ... in table "
       "order. Overlaps are allowed. Unselected cells are not appended."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, 3, this);
  this->Model->setHeaderData(kColIndex, Qt::Horizontal, tr("#"));
  this->Model->setHeaderData(kColName, Qt::Horizontal, tr("Name"));
  this->Model->setHeaderData(kColCells, Qt::Horizontal, tr("Cells"));

  this->View = new pqTreeView(this);
  this->View->setObjectName("SHYXSelectionPatchTable");
  this->View->setRootIsDecorated(false);
  this->View->setAlternatingRowColors(true);
  this->View->setAllColumnsShowFocus(true);
  this->View->setUniformRowHeights(true);
  this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->View->setSelectionMode(QAbstractItemView::ExtendedSelection);
  this->View->setEditTriggers(
    QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  this->View->setSortingEnabled(false);
  this->View->setMaximumRowCountBeforeScrolling(
    pqPropertyWidget::hintsWidgetHeightNumberOfRows(smgroup ? smgroup->GetHints() : nullptr, 8));
  this->View->setModel(this->Model);

  auto* header = this->View->header();
  header->setSectionResizeMode(kColIndex, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColName, QHeaderView::Stretch);
  header->setSectionResizeMode(kColCells, QHeaderView::ResizeToContents);
  header->setStretchLastSection(false);
  vbox->addWidget(this->View, 1);

  auto* buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->setSpacing(4);
  auto* addBtn = new QPushButton(tr("Add from selection"), this);
  addBtn->setToolTip(tr("Snapshot the current cell selection as a new geo_N row."));
  auto* removeBtn = new QPushButton(tr("Remove selected"), this);
  buttons->addWidget(addBtn);
  buttons->addWidget(removeBtn);
  buttons->addStretch(1);
  vbox->addLayout(buttons);

  this->Status = new QLabel(this);
  this->Status->setWordWrap(true);
  this->Status->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(this->Status);

  QObject::connect(this->Model, &QStandardItemModel::itemChanged, this,
    &pqSHYXSelectionPatchTableWidget::onItemChanged);
  QObject::connect(addBtn, &QPushButton::clicked, this,
    &pqSHYXSelectionPatchTableWidget::onAddFromSelection);
  QObject::connect(removeBtn, &QPushButton::clicked, this,
    &pqSHYXSelectionPatchTableWidget::onRemoveSelected);

  vtkSMProperty* namesProp = propertyFromGroup(smgroup, smproxy, "Names", "PatchNames");
  if (namesProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(namesProp) : nullptr;
    this->NamesPropertyName = QString::fromUtf8(pname ? pname : "PatchNames");
    this->addPropertyLink(
      this, this->NamesPropertyName.toUtf8().data(), SIGNAL(patchesChanged()), namesProp);
  }
  vtkSMProperty* idsProp = propertyFromGroup(smgroup, smproxy, "CellIds", "PatchCellIds");
  if (idsProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(idsProp) : nullptr;
    this->CellIdsPropertyName = QString::fromUtf8(pname ? pname : "PatchCellIds");
    this->addPropertyLink(
      this, this->CellIdsPropertyName.toUtf8().data(), SIGNAL(patchesChanged()), idsProp);
  }

  this->setChangeAvailableAsChangeFinished(true);
  this->rebuildFromProperty();
}

pqSHYXSelectionPatchTableWidget::~pqSHYXSelectionPatchTableWidget() = default;

bool pqSHYXSelectionPatchTableWidget::event(QEvent* e)
{
  if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
  {
    auto* devt = static_cast<QDynamicPropertyChangeEvent*>(e);
    const QString name = QString::fromLatin1(devt->propertyName());
    if (name == this->NamesPropertyName || name == this->CellIdsPropertyName)
    {
      this->rebuildFromProperty();
      return true;
    }
  }
  return this->Superclass::event(e);
}

void pqSHYXSelectionPatchTableWidget::apply()
{
  this->writeBackProperty();
  this->Superclass::apply();
}

void pqSHYXSelectionPatchTableWidget::reset()
{
  this->Superclass::reset();
  this->rebuildFromProperty();
}

void pqSHYXSelectionPatchTableWidget::onItemChanged(QStandardItem*)
{
  if (this->UpdatingFromProperty)
  {
    return;
  }
  this->writeBackProperty();
}

void pqSHYXSelectionPatchTableWidget::setStatus(const QString& text, bool error)
{
  if (!this->Status)
  {
    return;
  }
  this->Status->setText(text);
  this->Status->setStyleSheet(error ? QStringLiteral("color: #b00020; font-size: 11px;")
                                    : QStringLiteral("color: gray; font-size: 11px;"));
}

int pqSHYXSelectionPatchTableWidget::nextPartIndex() const
{
  int maxN = -1;
  const int rows = this->Model->rowCount();
  for (int r = 0; r < rows; ++r)
  {
    const QString name = this->Model->item(r, kColName)
      ? this->Model->item(r, kColName)->text().trimmed()
      : QString();
    const int under = name.lastIndexOf(QLatin1Char('_'));
    if (under < 0 || under + 1 >= name.size())
    {
      continue;
    }
    bool ok = false;
    const int n = name.mid(under + 1).toInt(&ok);
    if (ok)
    {
      maxN = std::max(maxN, n);
    }
  }
  return maxN + 1;
}

void pqSHYXSelectionPatchTableWidget::onAddFromSelection()
{
  std::vector<vtkIdType> ids;
  QString error;
  vtkDataSet* inputDs = clientInputDataSet(this->proxy());
  if (!collectActiveCellIds(inputDs, ids, error))
  {
    this->setStatus(error, true);
    return;
  }

  PatchRow row;
  row.Name = QStringLiteral("geo_%1").arg(this->nextPartIndex());
  row.CellIds = compactIdList(ids);
  QList<PatchRow> rows = this->collectRows();
  rows.push_back(row);
  this->rebuildRows(rows);
  this->writeBackProperty();
  this->setStatus(
    tr("Added %1 (%2 cells). Same names merge on Apply.").arg(row.Name).arg(ids.size()),
    false);
}

void pqSHYXSelectionPatchTableWidget::onRemoveSelected()
{
  const QModelIndexList selected = this->View->selectionModel()->selectedRows();
  if (selected.isEmpty())
  {
    this->setStatus(tr("Select table rows to remove."), true);
    return;
  }
  QList<int> rows;
  rows.reserve(selected.size());
  for (const QModelIndex& idx : selected)
  {
    rows.push_back(idx.row());
  }
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  for (int r : rows)
  {
    this->Model->removeRow(r);
  }
  this->writeBackProperty();
  this->setStatus(tr("Removed %1 row(s).").arg(rows.size()), false);
}

QStringList pqSHYXSelectionPatchTableWidget::linesFromProperty(const QString& propertyName) const
{
  QStringList lines;
  if (propertyName.isEmpty())
  {
    return lines;
  }
  const QVariant v = this->property(propertyName.toUtf8().constData());
  const QString text = v.toString();
  if (text.isEmpty())
  {
    return lines;
  }
  return text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
}

void pqSHYXSelectionPatchTableWidget::rebuildFromProperty()
{
  const QStringList names = this->linesFromProperty(this->NamesPropertyName);
  const QStringList ids = this->linesFromProperty(this->CellIdsPropertyName);
  int n = static_cast<int>(names.size());
  n = std::max(n, static_cast<int>(ids.size()));
  QList<PatchRow> rows;
  rows.reserve(n);
  for (int i = 0; i < n; ++i)
  {
    PatchRow row;
    row.Name = i < names.size() ? names[i] : QString();
    row.CellIds = i < ids.size() ? ids[i] : QString();
    if (row.Name.isEmpty() && row.CellIds.isEmpty())
    {
      continue;
    }
    rows.push_back(row);
  }
  this->rebuildRows(rows);
}

void pqSHYXSelectionPatchTableWidget::rebuildRows(const QList<PatchRow>& rows)
{
  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  this->Model->removeRows(0, this->Model->rowCount());
  for (int i = 0; i < rows.size(); ++i)
  {
    const PatchRow& row = rows[i];
    auto* indexItem = new QStandardItem(QString::number(i));
    indexItem->setEditable(false);
    auto* nameItem = new QStandardItem(row.Name);
    auto* cellsItem = new QStandardItem(QString::number(countCompactIds(row.CellIds)));
    cellsItem->setEditable(false);
    cellsItem->setData(row.CellIds, Qt::UserRole);
    this->Model->appendRow({ indexItem, nameItem, cellsItem });
  }
}

QList<pqSHYXSelectionPatchTableWidget::PatchRow> pqSHYXSelectionPatchTableWidget::collectRows() const
{
  QList<PatchRow> rows;
  const int n = this->Model->rowCount();
  rows.reserve(n);
  for (int r = 0; r < n; ++r)
  {
    PatchRow row;
    row.Name = this->Model->item(r, kColName) ? this->Model->item(r, kColName)->text() : QString();
    if (auto* cells = this->Model->item(r, kColCells))
    {
      row.CellIds = cells->data(Qt::UserRole).toString();
    }
    rows.push_back(row);
  }
  return rows;
}

void pqSHYXSelectionPatchTableWidget::writeBackProperty()
{
  QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
  const QList<PatchRow> rows = this->collectRows();
  QStringList names;
  QStringList ids;
  names.reserve(rows.size());
  ids.reserve(rows.size());
  for (const PatchRow& row : rows)
  {
    names << row.Name;
    ids << row.CellIds;
  }
  const QString nameText = names.join(QLatin1Char('\n'));
  const QString idText = ids.join(QLatin1Char('\n'));
  if (!this->NamesPropertyName.isEmpty())
  {
    this->setProperty(this->NamesPropertyName.toUtf8().constData(), nameText);
  }
  if (!this->CellIdsPropertyName.isEmpty())
  {
    this->setProperty(this->CellIdsPropertyName.toUtf8().constData(), idText);
  }
  Q_EMIT this->patchesChanged();
}
