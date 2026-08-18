#include "pqSHYXSnappyPatchTableWidget.h"

#include "pqTreeView.h"

#include "vtkAlgorithm.h"
#include "vtkCompositeDataSet.h"
#include "vtkDataObject.h"
#include "vtkInformation.h"
#include "vtkPartitionedDataSetCollection.h"
#include "vtkPolyData.h"
#include "vtkSMInputProperty.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCursor>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <functional>

namespace
{
vtkSMProperty* propertyFromGroup(
  vtkSMPropertyGroup* group, vtkSMProxy* proxy, const char* function, const char* fallbackName)
{
  if (group)
  {
    if (auto* p = group->GetProperty(function))
    {
      return p;
    }
    // Stay inside this group. Falling back to the whole proxy would pick
    // SurfacePatchTypes / RegionModes on every table and mis-classify Layers.
    const unsigned int n = group->GetNumberOfProperties();
    for (unsigned int i = 0; i < n; ++i)
    {
      const char* name = group->GetPropertyName(i);
      if (name && fallbackName && std::strcmp(name, fallbackName) == 0)
      {
        return group->GetProperty(i);
      }
    }
    return nullptr;
  }
  return proxy ? proxy->GetProperty(fallbackName) : nullptr;
}

QString propertyNameOf(vtkSMProxy* proxy, vtkSMProperty* prop, const char* fallback)
{
  const char* pname = (proxy && prop) ? proxy->GetPropertyName(prop) : nullptr;
  return QString::fromUtf8(pname ? pname : fallback);
}

int intProperty(vtkSMProxy* proxy, const char* name, int fallback)
{
  if (!proxy)
  {
    return fallback;
  }
  auto* p = vtkSMIntVectorProperty::SafeDownCast(proxy->GetProperty(name));
  if (!p || p->GetNumberOfElements() < 1)
  {
    return fallback;
  }
  return p->GetElement(0);
}

class ComboDelegate : public QStyledItemDelegate
{
public:
  ComboDelegate(const QStringList& items, QObject* parent)
    : QStyledItemDelegate(parent)
    , Items(items)
  {
  }

  QWidget* createEditor(
    QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
  {
    auto* box = new QComboBox(parent);
    box->addItems(this->Items);
    return box;
  }

  void setEditorData(QWidget* editor, const QModelIndex& index) const override
  {
    auto* box = qobject_cast<QComboBox*>(editor);
    if (!box)
    {
      return;
    }
    const QString text = index.data(Qt::EditRole).toString();
    const int i = box->findText(text);
    box->setCurrentIndex(i >= 0 ? i : 0);
  }

  void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
  {
    auto* box = qobject_cast<QComboBox*>(editor);
    if (box)
    {
      model->setData(index, box->currentText(), Qt::EditRole);
    }
  }

private:
  QStringList Items;
};

enum SurfaceCols
{
  SColIndex = 0,
  SColName = 1,
  SColMin = 2,
  SColMax = 3,
  SColType = 4
};
enum RegionCols
{
  RColIndex = 0,
  RColName = 1,
  RColMode = 2,
  RColLevel = 3,
  RColDist = 4
};
enum LayerCols
{
  LColIndex = 0,
  LColName = 1,
  LColN = 2
};
}

pqSHYXSnappyPatchTableWidget::pqSHYXSnappyPatchTableWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  // Classify from this group's functions only. Surface/Region tables nested
  // under castellated use the Kind constructor instead of this path.
  if (smgroup && smgroup->GetProperty("PatchTypes"))
  {
    this->TableKind = Surfaces;
  }
  else if (smgroup && smgroup->GetProperty("Modes"))
  {
    this->TableKind = Regions;
  }
  else
  {
    this->TableKind = Layers;
  }
  this->initialize(smproxy, smgroup);
}

pqSHYXSnappyPatchTableWidget::pqSHYXSnappyPatchTableWidget(
  vtkSMProxy* smproxy, Kind kind, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  this->TableKind = kind;
  this->initialize(smproxy, nullptr);
}

void pqSHYXSnappyPatchTableWidget::initialize(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  QString tipText;
  int nCols = 3;
  if (this->TableKind == Surfaces)
  {
    nCols = 5;
    tipText = tr("Add Input partitions as Surface patches. Each block is one STL / patch "
                 "(no firstSolid/secondSolid). Edit level and patchInfo type.");
  }
  else if (this->TableKind == Regions)
  {
    nCols = 5;
    tipText = tr("Add Input partitions as Region patches (volumetric shells). Mode inside / "
                 "outside needs a closed surface; distance uses Distance + Level.");
  }
  else
  {
    nCols = 3;
    tipText = tr("Add Input partitions as Layer patches (final patch names). nSurfaceLayers 0 "
                 "skips layers on that patch.");
  }
  auto* tip = new QLabel(tipText, this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, nCols, this);
  this->Model->setHeaderData(0, Qt::Horizontal, tr("#"));
  this->Model->setHeaderData(1, Qt::Horizontal, tr("Patch"));
  if (this->TableKind == Surfaces)
  {
    this->Model->setHeaderData(SColMin, Qt::Horizontal, tr("Level min"));
    this->Model->setHeaderData(SColMax, Qt::Horizontal, tr("Level max"));
    this->Model->setHeaderData(SColType, Qt::Horizontal, tr("Type"));
  }
  else if (this->TableKind == Regions)
  {
    this->Model->setHeaderData(RColMode, Qt::Horizontal, tr("Mode"));
    this->Model->setHeaderData(RColLevel, Qt::Horizontal, tr("Level"));
    this->Model->setHeaderData(RColDist, Qt::Horizontal, tr("Distance"));
  }
  else
  {
    this->Model->setHeaderData(LColN, Qt::Horizontal, tr("nSurfaceLayers"));
  }

  this->View = new pqTreeView(this);
  this->View->setObjectName("SHYXSnappyPatchTable");
  this->View->setRootIsDecorated(false);
  this->View->setAlternatingRowColors(true);
  this->View->setAllColumnsShowFocus(true);
  this->View->setUniformRowHeights(true);
  this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->View->setSelectionMode(QAbstractItemView::ExtendedSelection);
  this->View->setEditTriggers(
    QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  this->View->setSortingEnabled(false);
  const int defaultRows = (this->TableKind == Surfaces) ? 6 : 5;
  this->View->setMaximumRowCountBeforeScrolling(
    pqPropertyWidget::hintsWidgetHeightNumberOfRows(
      smgroup ? smgroup->GetHints() : nullptr, defaultRows));
  this->View->setModel(this->Model);

  auto* header = this->View->header();
  header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(1, QHeaderView::Stretch);
  for (int c = 2; c < nCols; ++c)
  {
    header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  header->setStretchLastSection(false);

  if (this->TableKind == Surfaces)
  {
    this->View->setItemDelegateForColumn(
      SColType, new ComboDelegate({ QStringLiteral("wall"), QStringLiteral("patch") }, this));
  }
  else if (this->TableKind == Regions)
  {
    this->View->setItemDelegateForColumn(RColMode,
      new ComboDelegate(
        { QStringLiteral("inside"), QStringLiteral("outside"), QStringLiteral("distance") }, this));
  }
  vbox->addWidget(this->View, 1);

  auto* buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->setSpacing(4);
  auto* addBtn = new QPushButton(tr("Add partition"), this);
  addBtn->setToolTip(tr("Add an Input partition as a table row."));
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
    &pqSHYXSnappyPatchTableWidget::onItemChanged);
  QObject::connect(addBtn, &QPushButton::clicked, this, &pqSHYXSnappyPatchTableWidget::onAddClicked);
  QObject::connect(removeBtn, &QPushButton::clicked, this,
    &pqSHYXSnappyPatchTableWidget::onRemoveSelected);

  auto link = [this, smproxy, smgroup](
                const char* function, const char* fallback, QString& dest) {
    vtkSMProperty* prop = propertyFromGroup(smgroup, smproxy, function, fallback);
    if (!prop)
    {
      return;
    }
    dest = propertyNameOf(smproxy, prop, fallback);
    this->addPropertyLink(this, dest.toUtf8().data(), SIGNAL(patchesChanged()), prop);
  };

  if (this->TableKind == Surfaces)
  {
    link("Names", "SurfaceNames", this->NamesPropertyName);
    link("LevelMin", "SurfaceLevelMin", this->LevelMinPropertyName);
    link("LevelMax", "SurfaceLevelMax", this->LevelMaxPropertyName);
    link("PatchTypes", "SurfacePatchTypes", this->PatchTypesPropertyName);
  }
  else if (this->TableKind == Regions)
  {
    link("Names", "RegionNames", this->NamesPropertyName);
    link("Modes", "RegionModes", this->ModesPropertyName);
    link("Levels", "RegionLevels", this->LevelsPropertyName);
    link("Distances", "RegionDistances", this->DistancesPropertyName);
  }
  else
  {
    link("Names", "LayerNames", this->NamesPropertyName);
    link("NSurfaceLayers", "LayerNSurfaceLayers", this->NLayersPropertyName);
  }

  this->setChangeAvailableAsChangeFinished(true);
  this->rebuildFromProperty();
}

pqSHYXSnappyPatchTableWidget::~pqSHYXSnappyPatchTableWidget() = default;

bool pqSHYXSnappyPatchTableWidget::event(QEvent* e)
{
  if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
  {
    auto* devt = static_cast<QDynamicPropertyChangeEvent*>(e);
    const QString name = QString::fromLatin1(devt->propertyName());
    const QStringList watched = { this->NamesPropertyName, this->LevelMinPropertyName,
      this->LevelMaxPropertyName, this->PatchTypesPropertyName, this->ModesPropertyName,
      this->LevelsPropertyName, this->DistancesPropertyName, this->NLayersPropertyName };
    if (watched.contains(name))
    {
      this->rebuildFromProperty();
      return true;
    }
  }
  return this->Superclass::event(e);
}

void pqSHYXSnappyPatchTableWidget::apply()
{
  this->writeBackProperty();
  this->Superclass::apply();
}

void pqSHYXSnappyPatchTableWidget::reset()
{
  this->Superclass::reset();
  this->rebuildFromProperty();
}

void pqSHYXSnappyPatchTableWidget::onItemChanged(QStandardItem*)
{
  if (this->UpdatingFromProperty)
  {
    return;
  }
  this->writeBackProperty();
}

void pqSHYXSnappyPatchTableWidget::setStatus(const QString& text, bool error)
{
  if (!this->Status)
  {
    return;
  }
  this->Status->setText(text);
  this->Status->setStyleSheet(error ? QStringLiteral("color: #b00020; font-size: 11px;")
                                    : QStringLiteral("color: gray; font-size: 11px;"));
}

int pqSHYXSnappyPatchTableWidget::defaultLevelMin() const
{
  return intProperty(this->proxy(), "RefinementMin", 0);
}

int pqSHYXSnappyPatchTableWidget::defaultLevelMax() const
{
  return intProperty(this->proxy(), "RefinementMax", 2);
}

int pqSHYXSnappyPatchTableWidget::defaultNSurfaceLayers() const
{
  return intProperty(this->proxy(), "NSurfaceLayers", 3);
}

pqSHYXSnappyPatchTableWidget::Row pqSHYXSnappyPatchTableWidget::defaultRow(const QString& name) const
{
  Row row;
  row.Name = name;
  row.LevelMin = this->defaultLevelMin();
  row.LevelMax = this->defaultLevelMax();
  row.Level = this->defaultLevelMax();
  row.NSurfaceLayers = this->defaultNSurfaceLayers();
  return row;
}

QStringList pqSHYXSnappyPatchTableWidget::inputPartitionNames() const
{
  QStringList names;
  vtkSMProxy* filter = this->proxy();
  if (!filter)
  {
    return names;
  }
  auto* inputProp = vtkSMInputProperty::SafeDownCast(filter->GetProperty("Input"));
  if (!inputProp || inputProp->GetNumberOfProxies() == 0)
  {
    return names;
  }
  auto* src = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
  if (!src)
  {
    return names;
  }
  src->UpdatePipeline();
  auto* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  if (!alg)
  {
    return names;
  }
  const unsigned int port = inputProp->GetOutputPortForConnection(0);
  vtkDataObject* out = alg->GetOutputDataObject(static_cast<int>(port));
  if (auto* pdc = vtkPartitionedDataSetCollection::SafeDownCast(out))
  {
    const unsigned int n = pdc->GetNumberOfPartitionedDataSets();
    for (unsigned int i = 0; i < n; ++i)
    {
      QString name = QStringLiteral("part_%1").arg(static_cast<int>(i));
      if (vtkInformation* meta = pdc->GetMetaData(i))
      {
        if (const char* nstr = meta->Get(vtkCompositeDataSet::NAME()))
        {
          if (nstr[0] != '\0')
          {
            name = QString::fromUtf8(nstr);
          }
        }
      }
      names << name;
    }
  }
  else if (vtkPolyData::SafeDownCast(out) || vtkDataObject::SafeDownCast(out))
  {
    names << QStringLiteral("geometry");
  }
  return names;
}

QStringList pqSHYXSnappyPatchTableWidget::unusedPartitionNames() const
{
  QStringList all = this->inputPartitionNames();
  const QList<Row> rows = this->collectRows();
  QStringList used;
  used.reserve(rows.size());
  for (const Row& row : rows)
  {
    used << row.Name;
  }
  QStringList unused;
  for (const QString& name : all)
  {
    if (!used.contains(name))
    {
      unused << name;
    }
  }
  return unused;
}

void pqSHYXSnappyPatchTableWidget::onAddClicked()
{
  const QStringList unused = this->unusedPartitionNames();
  if (unused.isEmpty())
  {
    const QStringList all = this->inputPartitionNames();
    if (all.isEmpty())
    {
      this->setStatus(tr("Apply the Input first so partition names are available."), true);
      return;
    }
    this->setStatus(tr("All Input partitions are already in the table."), true);
    return;
  }
  QMenu menu(this);
  for (const QString& name : unused)
  {
    menu.addAction(name, this, [this, name]() { this->onAddName(name); });
  }
  if (unused.size() > 1)
  {
    menu.addSeparator();
    menu.addAction(tr("Add all remaining"), this, &pqSHYXSnappyPatchTableWidget::onAddAllRemaining);
  }
  menu.exec(QCursor::pos());
}

void pqSHYXSnappyPatchTableWidget::onAddName(const QString& name)
{
  QList<Row> rows = this->collectRows();
  rows.push_back(this->defaultRow(name));
  this->rebuildRows(rows);
  this->writeBackProperty();
  this->setStatus(tr("Added %1.").arg(name), false);
}

void pqSHYXSnappyPatchTableWidget::onAddAllRemaining()
{
  QList<Row> rows = this->collectRows();
  const QStringList unused = this->unusedPartitionNames();
  for (const QString& name : unused)
  {
    rows.push_back(this->defaultRow(name));
  }
  this->rebuildRows(rows);
  this->writeBackProperty();
  this->setStatus(tr("Added %1 partition(s).").arg(unused.size()), false);
}

void pqSHYXSnappyPatchTableWidget::onRemoveSelected()
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

QStringList pqSHYXSnappyPatchTableWidget::linesFromProperty(const QString& propertyName) const
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

void pqSHYXSnappyPatchTableWidget::rebuildFromProperty()
{
  const QStringList names = this->linesFromProperty(this->NamesPropertyName);
  QList<Row> rows;
  const int n = static_cast<int>(names.size());
  rows.reserve(n);
  if (this->TableKind == Surfaces)
  {
    const QStringList mins = this->linesFromProperty(this->LevelMinPropertyName);
    const QStringList maxs = this->linesFromProperty(this->LevelMaxPropertyName);
    const QStringList types = this->linesFromProperty(this->PatchTypesPropertyName);
    for (int i = 0; i < n; ++i)
    {
      if (names[i].isEmpty())
      {
        continue;
      }
      Row row = this->defaultRow(names[i]);
      bool ok = false;
      if (i < mins.size())
      {
        const int v = mins[i].toInt(&ok);
        if (ok)
        {
          row.LevelMin = v;
        }
      }
      if (i < maxs.size())
      {
        const int v = maxs[i].toInt(&ok);
        if (ok)
        {
          row.LevelMax = v;
        }
      }
      if (i < types.size() && !types[i].isEmpty())
      {
        row.PatchType = types[i];
      }
      rows.push_back(row);
    }
  }
  else if (this->TableKind == Regions)
  {
    const QStringList modes = this->linesFromProperty(this->ModesPropertyName);
    const QStringList levels = this->linesFromProperty(this->LevelsPropertyName);
    const QStringList dists = this->linesFromProperty(this->DistancesPropertyName);
    for (int i = 0; i < n; ++i)
    {
      if (names[i].isEmpty())
      {
        continue;
      }
      Row row = this->defaultRow(names[i]);
      if (i < modes.size() && !modes[i].isEmpty())
      {
        row.Mode = modes[i];
      }
      bool ok = false;
      if (i < levels.size())
      {
        const int v = levels[i].toInt(&ok);
        if (ok)
        {
          row.Level = v;
        }
      }
      if (i < dists.size())
      {
        const double v = dists[i].toDouble(&ok);
        if (ok)
        {
          row.Distance = v;
        }
      }
      rows.push_back(row);
    }
  }
  else
  {
    const QStringList ns = this->linesFromProperty(this->NLayersPropertyName);
    for (int i = 0; i < n; ++i)
    {
      if (names[i].isEmpty())
      {
        continue;
      }
      Row row = this->defaultRow(names[i]);
      bool ok = false;
      if (i < ns.size())
      {
        const int v = ns[i].toInt(&ok);
        if (ok)
        {
          row.NSurfaceLayers = v;
        }
      }
      rows.push_back(row);
    }
  }
  this->rebuildRows(rows);
}

void pqSHYXSnappyPatchTableWidget::rebuildRows(const QList<Row>& rows)
{
  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  this->Model->removeRows(0, this->Model->rowCount());
  for (int i = 0; i < rows.size(); ++i)
  {
    const Row& row = rows[i];
    auto* indexItem = new QStandardItem(QString::number(i));
    indexItem->setEditable(false);
    auto* nameItem = new QStandardItem(row.Name);
    if (this->TableKind == Surfaces)
    {
      auto* minItem = new QStandardItem(QString::number(row.LevelMin));
      auto* maxItem = new QStandardItem(QString::number(row.LevelMax));
      auto* typeItem = new QStandardItem(row.PatchType);
      this->Model->appendRow({ indexItem, nameItem, minItem, maxItem, typeItem });
    }
    else if (this->TableKind == Regions)
    {
      auto* modeItem = new QStandardItem(row.Mode);
      auto* levelItem = new QStandardItem(QString::number(row.Level));
      auto* distItem = new QStandardItem(QString::number(row.Distance));
      this->Model->appendRow({ indexItem, nameItem, modeItem, levelItem, distItem });
    }
    else
    {
      auto* nItem = new QStandardItem(QString::number(row.NSurfaceLayers));
      this->Model->appendRow({ indexItem, nameItem, nItem });
    }
  }
}

QList<pqSHYXSnappyPatchTableWidget::Row> pqSHYXSnappyPatchTableWidget::collectRows() const
{
  QList<Row> rows;
  const int n = this->Model->rowCount();
  rows.reserve(n);
  for (int r = 0; r < n; ++r)
  {
    Row row = this->defaultRow(QString());
    row.Name = this->Model->item(r, 1) ? this->Model->item(r, 1)->text() : QString();
    if (this->TableKind == Surfaces)
    {
      if (auto* it = this->Model->item(r, SColMin))
      {
        row.LevelMin = it->text().toInt();
      }
      if (auto* it = this->Model->item(r, SColMax))
      {
        row.LevelMax = it->text().toInt();
      }
      if (auto* it = this->Model->item(r, SColType))
      {
        row.PatchType = it->text();
      }
    }
    else if (this->TableKind == Regions)
    {
      if (auto* it = this->Model->item(r, RColMode))
      {
        row.Mode = it->text();
      }
      if (auto* it = this->Model->item(r, RColLevel))
      {
        row.Level = it->text().toInt();
      }
      if (auto* it = this->Model->item(r, RColDist))
      {
        row.Distance = it->text().toDouble();
      }
    }
    else if (auto* it = this->Model->item(r, LColN))
    {
      row.NSurfaceLayers = it->text().toInt();
    }
    rows.push_back(row);
  }
  return rows;
}

void pqSHYXSnappyPatchTableWidget::writeBackProperty()
{
  QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
  const QList<Row> rows = this->collectRows();
  QStringList names;
  names.reserve(rows.size());
  for (const Row& row : rows)
  {
    names << row.Name;
  }
  auto setText = [this](const QString& propName, const QString& text) {
    if (!propName.isEmpty())
    {
      this->setProperty(propName.toUtf8().constData(), text);
    }
  };
  setText(this->NamesPropertyName, names.join(QLatin1Char('\n')));
  if (this->TableKind == Surfaces)
  {
    QStringList mins, maxs, types;
    for (const Row& row : rows)
    {
      mins << QString::number(row.LevelMin);
      maxs << QString::number(row.LevelMax);
      types << row.PatchType;
    }
    setText(this->LevelMinPropertyName, mins.join(QLatin1Char('\n')));
    setText(this->LevelMaxPropertyName, maxs.join(QLatin1Char('\n')));
    setText(this->PatchTypesPropertyName, types.join(QLatin1Char('\n')));
  }
  else if (this->TableKind == Regions)
  {
    QStringList modes, levels, dists;
    for (const Row& row : rows)
    {
      modes << row.Mode;
      levels << QString::number(row.Level);
      dists << QString::number(row.Distance);
    }
    setText(this->ModesPropertyName, modes.join(QLatin1Char('\n')));
    setText(this->LevelsPropertyName, levels.join(QLatin1Char('\n')));
    setText(this->DistancesPropertyName, dists.join(QLatin1Char('\n')));
  }
  else
  {
    QStringList ns;
    for (const Row& row : rows)
    {
      ns << QString::number(row.NSurfaceLayers);
    }
    setText(this->NLayersPropertyName, ns.join(QLatin1Char('\n')));
  }
  Q_EMIT this->patchesChanged();
}
