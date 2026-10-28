#include "pqSHYXCameraAdjustWidget.h"

#include "pqActiveObjects.h"
#include "pqTreeView.h"
#include "pqView.h"

#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"

#include <QAbstractItemView>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QModelIndex>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int kColIndex      = 0;
constexpr int kColName       = 1;
constexpr int kColPosition   = 2;
constexpr int kColFocalPoint = 3;
constexpr int kColViewUp     = 4;
constexpr int kColLens       = 5;

constexpr int kRolePosition   = Qt::UserRole;
constexpr int kRoleFocalPoint = Qt::UserRole + 1;
constexpr int kRoleViewUp     = Qt::UserRole + 2;
constexpr int kRoleLens       = Qt::UserRole + 3;

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

QString FormatNumber(double value)
{
  return QString::number(value, 'g', 12);
}

QString FormatVector(const double values[3])
{
  return QStringLiteral("%1,%2,%3")
    .arg(FormatNumber(values[0]), FormatNumber(values[1]), FormatNumber(values[2]));
}

QString FormatLens(double viewAngle, double parallelScale, int parallelProjection)
{
  return QStringLiteral("%1,%2,%3")
    .arg(FormatNumber(viewAngle), FormatNumber(parallelScale))
    .arg(parallelProjection ? 1 : 0);
}

QString DisplayNumber(double value)
{
  return QString::number(value, 'g', 6);
}

QString DisplayVector(const double values[3])
{
  return QStringLiteral("%1, %2, %3")
    .arg(DisplayNumber(values[0]), DisplayNumber(values[1]), DisplayNumber(values[2]));
}

QString DisplayLens(double viewAngle, double parallelScale, int parallelProjection)
{
  return QStringLiteral("%1 / %2 (%3)")
    .arg(DisplayNumber(viewAngle), DisplayNumber(parallelScale))
    .arg(parallelProjection ? QStringLiteral("ortho") : QStringLiteral("persp"));
}

bool ParseVector(const QString& text, double values[3])
{
  const QStringList parts = text.split(QLatin1Char(','));
  if (parts.size() < 3)
  {
    return false;
  }
  bool ok = true;
  for (int i = 0; i < 3; ++i)
  {
    bool         partOk = false;
    const double value  = parts[i].trimmed().toDouble(&partOk);
    ok                  = ok && partOk;
    values[i]           = partOk ? value : 0.0;
  }
  return ok;
}

bool ParseLens(
  const QString& text, double& viewAngle, double& parallelScale, int& parallelProjection)
{
  const QStringList parts = text.split(QLatin1Char(','));
  if (parts.size() < 3)
  {
    return false;
  }
  bool okAngle       = false;
  bool okScale       = false;
  bool okParallel    = false;
  viewAngle          = parts[0].trimmed().toDouble(&okAngle);
  parallelScale      = parts[1].trimmed().toDouble(&okScale);
  parallelProjection = parts[2].trimmed().toDouble(&okParallel) != 0.0 ? 1 : 0;
  return okAngle && okScale && okParallel;
}

/** Camera element shared by every RenderView: the view proxy exposes Position / FocalPoint /
 *  ViewUp / ViewAngle / ParallelScale / ParallelProjection from its ActiveCamera subproxy. */
struct ViewCamera
{
  vtkSMProxy*    Proxy              = nullptr;
  vtkSMProperty* Position           = nullptr;
  vtkSMProperty* FocalPoint         = nullptr;
  vtkSMProperty* ViewUp             = nullptr;
  vtkSMProperty* ViewAngle          = nullptr;
  vtkSMProperty* ParallelScale      = nullptr;
  vtkSMProperty* ParallelProjection = nullptr;

  bool Valid() const { return this->Proxy && this->Position && this->FocalPoint && this->ViewUp; }
};

ViewCamera CameraOfActiveView()
{
  ViewCamera  out;
  pqView*     view  = pqActiveObjects::instance().activeView();
  vtkSMProxy* proxy = view ? view->getProxy() : nullptr;
  if (!proxy)
  {
    return out;
  }
  vtkSMProperty* position   = proxy->GetProperty("CameraPosition");
  vtkSMProperty* focalPoint = proxy->GetProperty("CameraFocalPoint");
  vtkSMProperty* viewUp     = proxy->GetProperty("CameraViewUp");
  if (!position || !focalPoint || !viewUp)
  {
    // Chart / spreadsheet / other non-3D views have no camera.
    return out;
  }
  out.Proxy              = proxy;
  out.Position           = position;
  out.FocalPoint         = focalPoint;
  out.ViewUp             = viewUp;
  out.ViewAngle          = proxy->GetProperty("CameraViewAngle");
  out.ParallelScale      = proxy->GetProperty("CameraParallelScale");
  out.ParallelProjection = proxy->GetProperty("CameraParallelProjection");
  return out;
}

bool ReadVector(vtkSMProperty* property, double values[3])
{
  if (!property)
  {
    return false;
  }
  vtkSMPropertyHelper helper(property);
  if (helper.GetNumberOfElements() < 3)
  {
    return false;
  }
  for (int i = 0; i < 3; ++i)
  {
    values[i] = helper.GetAsDouble(i);
  }
  return true;
}
} // namespace

//-----------------------------------------------------------------------------
pqSHYXCameraAdjustWidget::pqSHYXCameraAdjustWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  auto* tip = new QLabel(
    tr("Named camera presets stored in this node, so this branch keeps its own camera instead of a "
       "global view setting. Add captures the active 3D view; Update overwrites the selected row; "
       "Delete removes rows; Apply (or double-click a value cell) moves the active view camera to "
       "the selected preset. The output vertices mark the preset positions."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, 6, this);
  this->Model->setHeaderData(kColIndex, Qt::Horizontal, tr("#"));
  this->Model->setHeaderData(kColName, Qt::Horizontal, tr("Name"));
  this->Model->setHeaderData(kColPosition, Qt::Horizontal, tr("Position"));
  this->Model->setHeaderData(kColFocalPoint, Qt::Horizontal, tr("Focal Point"));
  this->Model->setHeaderData(kColViewUp, Qt::Horizontal, tr("View Up"));
  this->Model->setHeaderData(kColLens, Qt::Horizontal, tr("Angle / Scale"));

  this->View = new pqTreeView(this);
  this->View->setObjectName("SHYXCameraAdjustTable");
  this->View->setRootIsDecorated(false);
  this->View->setAlternatingRowColors(true);
  this->View->setAllColumnsShowFocus(true);
  this->View->setUniformRowHeights(true);
  this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->View->setSelectionMode(QAbstractItemView::ExtendedSelection);
  this->View->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  this->View->setSortingEnabled(false);
  this->View->setMaximumRowCountBeforeScrolling(
    pqPropertyWidget::hintsWidgetHeightNumberOfRows(smgroup ? smgroup->GetHints() : nullptr, 8));
  this->View->setModel(this->Model);

  auto* header = this->View->header();
  header->setHighlightSections(false);
  header->setSectionResizeMode(kColIndex, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColName, QHeaderView::Stretch);
  header->setSectionResizeMode(kColPosition, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColFocalPoint, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColViewUp, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColLens, QHeaderView::ResizeToContents);
  header->setStretchLastSection(false);
  vbox->addWidget(this->View, 1);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->setContentsMargins(0, 0, 0, 0);
  buttonRow->setSpacing(4);
  auto* addButton = new QPushButton(tr("Add (current camera)"), this);
  addButton->setToolTip(
    tr("Append the active 3D view camera (position, focal point, view up, view angle, parallel "
       "scale / projection) as a new preset."));
  auto* updateButton = new QPushButton(tr("Update selected"), this);
  updateButton->setToolTip(tr("Overwrite the first selected row from the active 3D view camera."));
  auto* removeButton = new QPushButton(tr("Delete selected"), this);
  removeButton->setToolTip(tr("Remove the selected preset rows."));
  auto* applyButton = new QPushButton(tr("Apply to view"), this);
  applyButton->setToolTip(tr("Move the active 3D view camera to the first selected preset."));
  buttonRow->addWidget(addButton);
  buttonRow->addWidget(updateButton);
  buttonRow->addWidget(removeButton);
  buttonRow->addStretch(1);
  buttonRow->addWidget(applyButton);
  vbox->addLayout(buttonRow);

  this->Status = new QLabel(this);
  this->Status->setWordWrap(true);
  this->Status->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(this->Status);

  QObject::connect(addButton, &QPushButton::clicked, this, &pqSHYXCameraAdjustWidget::onAdd);
  QObject::connect(
    updateButton, &QPushButton::clicked, this, &pqSHYXCameraAdjustWidget::onUpdateSelected);
  QObject::connect(
    removeButton, &QPushButton::clicked, this, &pqSHYXCameraAdjustWidget::onRemoveSelected);
  QObject::connect(
    applyButton, &QPushButton::clicked, this, &pqSHYXCameraAdjustWidget::onApplySelected);
  QObject::connect(
    this->Model, &QStandardItemModel::itemChanged, this, &pqSHYXCameraAdjustWidget::onItemChanged);
  QObject::connect(
    this->View, &pqTreeView::doubleClicked, this, &pqSHYXCameraAdjustWidget::onRowDoubleClicked);

  vtkSMProperty* namesProp = propertyFromGroup(smgroup, smproxy, "Names", "CameraNames");
  if (namesProp)
  {
    const char* pname       = smproxy ? smproxy->GetPropertyName(namesProp) : nullptr;
    this->NamesPropertyName = QString::fromUtf8(pname ? pname : "CameraNames");
    this->addPropertyLink(
      this, this->NamesPropertyName.toUtf8().data(), SIGNAL(camerasChanged()), namesProp);
  }
  vtkSMProperty* positionsProp =
    propertyFromGroup(smgroup, smproxy, "Positions", "CameraPositions");
  if (positionsProp)
  {
    const char* pname           = smproxy ? smproxy->GetPropertyName(positionsProp) : nullptr;
    this->PositionsPropertyName = QString::fromUtf8(pname ? pname : "CameraPositions");
    this->addPropertyLink(
      this, this->PositionsPropertyName.toUtf8().data(), SIGNAL(camerasChanged()), positionsProp);
  }
  vtkSMProperty* focalsProp =
    propertyFromGroup(smgroup, smproxy, "FocalPoints", "CameraFocalPoints");
  if (focalsProp)
  {
    const char* pname             = smproxy ? smproxy->GetPropertyName(focalsProp) : nullptr;
    this->FocalPointsPropertyName = QString::fromUtf8(pname ? pname : "CameraFocalPoints");
    this->addPropertyLink(
      this, this->FocalPointsPropertyName.toUtf8().data(), SIGNAL(camerasChanged()), focalsProp);
  }
  vtkSMProperty* upsProp = propertyFromGroup(smgroup, smproxy, "ViewUps", "CameraViewUps");
  if (upsProp)
  {
    const char* pname         = smproxy ? smproxy->GetPropertyName(upsProp) : nullptr;
    this->ViewUpsPropertyName = QString::fromUtf8(pname ? pname : "CameraViewUps");
    this->addPropertyLink(
      this, this->ViewUpsPropertyName.toUtf8().data(), SIGNAL(camerasChanged()), upsProp);
  }
  vtkSMProperty* lensProp = propertyFromGroup(smgroup, smproxy, "Lens", "CameraLens");
  if (lensProp)
  {
    const char* pname      = smproxy ? smproxy->GetPropertyName(lensProp) : nullptr;
    this->LensPropertyName = QString::fromUtf8(pname ? pname : "CameraLens");
    this->addPropertyLink(
      this, this->LensPropertyName.toUtf8().data(), SIGNAL(camerasChanged()), lensProp);
  }

  this->setChangeAvailableAsChangeFinished(true);
  this->rebuildFromProperty();
}

//-----------------------------------------------------------------------------
pqSHYXCameraAdjustWidget::~pqSHYXCameraAdjustWidget() = default;

//-----------------------------------------------------------------------------
bool pqSHYXCameraAdjustWidget::event(QEvent* e)
{
  if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
  {
    auto*         devt = static_cast<QDynamicPropertyChangeEvent*>(e);
    const QString name = QString::fromLatin1(devt->propertyName());
    if (name == this->NamesPropertyName || name == this->PositionsPropertyName ||
      name == this->FocalPointsPropertyName || name == this->ViewUpsPropertyName ||
      name == this->LensPropertyName)
    {
      this->rebuildFromProperty();
      return true;
    }
  }
  return this->Superclass::event(e);
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::apply()
{
  QScopedValueRollback<bool> guard(this->Applying, true);
  this->writeBackProperty();
  this->Superclass::apply();
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::reset()
{
  this->Superclass::reset();
  this->rebuildFromProperty();
}

//-----------------------------------------------------------------------------
QStringList pqSHYXCameraAdjustWidget::linesFromProperty(const QString& propertyName) const
{
  QStringList lines;
  if (propertyName.isEmpty())
  {
    return lines;
  }
  const QString text = this->property(propertyName.toUtf8().constData()).toString();
  if (text.isEmpty())
  {
    return lines;
  }
  return text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::rebuildFromProperty()
{
  const QStringList names     = this->linesFromProperty(this->NamesPropertyName);
  const QStringList positions = this->linesFromProperty(this->PositionsPropertyName);
  const QStringList focals    = this->linesFromProperty(this->FocalPointsPropertyName);
  const QStringList ups       = this->linesFromProperty(this->ViewUpsPropertyName);
  const QStringList lenses    = this->linesFromProperty(this->LensPropertyName);

  int n = static_cast<int>(names.size());
  n     = std::max(n, static_cast<int>(positions.size()));
  n     = std::max(n, static_cast<int>(focals.size()));
  n     = std::max(n, static_cast<int>(ups.size()));
  n     = std::max(n, static_cast<int>(lenses.size()));

  QList<CameraRow> rows;
  rows.reserve(n);
  for (int i = 0; i < n; ++i)
  {
    CameraRow row;
    if (i < names.size())
    {
      row.Name = names[i];
    }
    if (i < positions.size())
    {
      ParseVector(positions[i], row.Position);
    }
    if (i < focals.size())
    {
      ParseVector(focals[i], row.FocalPoint);
    }
    if (i < ups.size())
    {
      ParseVector(ups[i], row.ViewUp);
    }
    if (i < lenses.size())
    {
      ParseLens(lenses[i], row.ViewAngle, row.ParallelScale, row.ParallelProjection);
    }
    rows.push_back(row);
  }
  this->rebuildRows(rows);
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::rebuildRows(const QList<CameraRow>& rows)
{
  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  this->Model->removeRows(0, this->Model->rowCount());
  for (int i = 0; i < rows.size(); ++i)
  {
    const CameraRow& row       = rows[i];
    auto*            indexItem = new QStandardItem(QString::number(i));
    indexItem->setEditable(false);
    indexItem->setData(FormatVector(row.Position), kRolePosition);
    indexItem->setData(FormatVector(row.FocalPoint), kRoleFocalPoint);
    indexItem->setData(FormatVector(row.ViewUp), kRoleViewUp);
    indexItem->setData(
      FormatLens(row.ViewAngle, row.ParallelScale, row.ParallelProjection), kRoleLens);
    auto* nameItem = new QStandardItem(row.Name);
    if (row.Name.isEmpty())
    {
      nameItem->setText(QStringLiteral("Camera_%1").arg(i));
    }
    auto* positionItem = new QStandardItem(DisplayVector(row.Position));
    positionItem->setEditable(false);
    auto* focalItem = new QStandardItem(DisplayVector(row.FocalPoint));
    focalItem->setEditable(false);
    auto* upItem = new QStandardItem(DisplayVector(row.ViewUp));
    upItem->setEditable(false);
    auto* lensItem =
      new QStandardItem(DisplayLens(row.ViewAngle, row.ParallelScale, row.ParallelProjection));
    lensItem->setEditable(false);
    this->Model->appendRow({ indexItem, nameItem, positionItem, focalItem, upItem, lensItem });
  }
}

//-----------------------------------------------------------------------------
QList<pqSHYXCameraAdjustWidget::CameraRow> pqSHYXCameraAdjustWidget::collectRows() const
{
  QList<CameraRow> rows;
  const int        n = this->Model->rowCount();
  rows.reserve(n);
  for (int r = 0; r < n; ++r)
  {
    CameraRow row;
    if (auto* nameItem = this->Model->item(r, kColName))
    {
      row.Name = nameItem->text();
    }
    if (auto* indexItem = this->Model->item(r, kColIndex))
    {
      ParseVector(indexItem->data(kRolePosition).toString(), row.Position);
      ParseVector(indexItem->data(kRoleFocalPoint).toString(), row.FocalPoint);
      ParseVector(indexItem->data(kRoleViewUp).toString(), row.ViewUp);
      ParseLens(indexItem->data(kRoleLens).toString(), row.ViewAngle, row.ParallelScale,
        row.ParallelProjection);
    }
    rows.push_back(row);
  }
  return rows;
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::writeBackProperty()
{
  QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
  const QList<CameraRow>     rows = this->collectRows();
  QStringList                names;
  QStringList                positions;
  QStringList                focals;
  QStringList                ups;
  QStringList                lenses;
  names.reserve(rows.size());
  positions.reserve(rows.size());
  focals.reserve(rows.size());
  ups.reserve(rows.size());
  lenses.reserve(rows.size());
  for (const CameraRow& row : rows)
  {
    names << row.Name;
    positions << FormatVector(row.Position);
    focals << FormatVector(row.FocalPoint);
    ups << FormatVector(row.ViewUp);
    lenses << FormatLens(row.ViewAngle, row.ParallelScale, row.ParallelProjection);
  }
  if (!this->NamesPropertyName.isEmpty())
  {
    this->setProperty(this->NamesPropertyName.toUtf8().constData(), names.join(QLatin1Char('\n')));
  }
  if (!this->PositionsPropertyName.isEmpty())
  {
    this->setProperty(
      this->PositionsPropertyName.toUtf8().constData(), positions.join(QLatin1Char('\n')));
  }
  if (!this->FocalPointsPropertyName.isEmpty())
  {
    this->setProperty(
      this->FocalPointsPropertyName.toUtf8().constData(), focals.join(QLatin1Char('\n')));
  }
  if (!this->ViewUpsPropertyName.isEmpty())
  {
    this->setProperty(this->ViewUpsPropertyName.toUtf8().constData(), ups.join(QLatin1Char('\n')));
  }
  if (!this->LensPropertyName.isEmpty())
  {
    this->setProperty(this->LensPropertyName.toUtf8().constData(), lenses.join(QLatin1Char('\n')));
  }
  if (this->Applying)
  {
    // Superclass::apply() copies the Qt properties through pqPropertyLinks.
    return;
  }
  Q_EMIT this->camerasChanged();
  // pqPropertiesPanel enables Apply after changeAvailable, then changeFinished.
  Q_EMIT this->changeAvailable();
  Q_EMIT this->changeFinished();
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::onItemChanged(QStandardItem*)
{
  if (this->UpdatingFromProperty)
  {
    return;
  }
  this->writeBackProperty();
}

//-----------------------------------------------------------------------------
int pqSHYXCameraAdjustWidget::selectedRow() const
{
  if (!this->View || !this->View->selectionModel())
  {
    return -1;
  }
  const QModelIndexList selected = this->View->selectionModel()->selectedRows();
  if (!selected.isEmpty())
  {
    return selected.first().row();
  }
  const QModelIndexList indexes = this->View->selectionModel()->selectedIndexes();
  return indexes.isEmpty() ? -1 : indexes.first().row();
}

//-----------------------------------------------------------------------------
int pqSHYXCameraAdjustWidget::nextCameraIndex() const
{
  int                      maxN = -1;
  const QRegularExpression re(QStringLiteral("^Camera_(\\d+)$"));
  const int                rows = this->Model->rowCount();
  for (int r = 0; r < rows; ++r)
  {
    const QString name =
      this->Model->item(r, kColName) ? this->Model->item(r, kColName)->text().trimmed() : QString();
    const QRegularExpressionMatch match = re.match(name);
    if (match.hasMatch())
    {
      maxN = std::max(maxN, match.captured(1).toInt());
    }
  }
  return maxN + 1;
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::setStatus(const QString& text, bool error)
{
  if (!this->Status)
  {
    return;
  }
  this->Status->setText(text);
  this->Status->setStyleSheet(error ? QStringLiteral("color: #b00020; font-size: 11px;")
                                    : QStringLiteral("color: gray; font-size: 11px;"));
}

//-----------------------------------------------------------------------------
bool pqSHYXCameraAdjustWidget::captureCamera(CameraRow& row, QString& error) const
{
  const ViewCamera camera = CameraOfActiveView();
  if (!camera.Valid())
  {
    error = tr("No active 3D RenderView. Activate a RenderView before adding or updating a "
               "preset.");
    return false;
  }
  if (!ReadVector(camera.Position, row.Position) ||
    !ReadVector(camera.FocalPoint, row.FocalPoint) || !ReadVector(camera.ViewUp, row.ViewUp))
  {
    error = tr("The active view did not report a complete camera.");
    return false;
  }
  if (camera.ViewAngle)
  {
    row.ViewAngle = vtkSMPropertyHelper(camera.ViewAngle).GetAsDouble();
  }
  if (camera.ParallelScale)
  {
    row.ParallelScale = vtkSMPropertyHelper(camera.ParallelScale).GetAsDouble();
  }
  if (camera.ParallelProjection)
  {
    row.ParallelProjection = vtkSMPropertyHelper(camera.ParallelProjection).GetAsInt() ? 1 : 0;
  }
  return true;
}

//-----------------------------------------------------------------------------
bool pqSHYXCameraAdjustWidget::applyCamera(const CameraRow& row, QString& error) const
{
  const ViewCamera camera = CameraOfActiveView();
  if (!camera.Valid())
  {
    error = tr("No active 3D RenderView. Activate a RenderView before applying a preset.");
    return false;
  }
  vtkSMPropertyHelper(camera.Position).Set(row.Position, 3);
  vtkSMPropertyHelper(camera.FocalPoint).Set(row.FocalPoint, 3);
  vtkSMPropertyHelper(camera.ViewUp).Set(row.ViewUp, 3);
  if (camera.ViewAngle)
  {
    vtkSMPropertyHelper(camera.ViewAngle).Set(row.ViewAngle);
  }
  if (camera.ParallelScale)
  {
    vtkSMPropertyHelper(camera.ParallelScale).Set(row.ParallelScale);
  }
  if (camera.ParallelProjection)
  {
    vtkSMPropertyHelper(camera.ParallelProjection).Set(row.ParallelProjection);
  }
  // CameraParallelScale / CameraParallelProjection are not immediate_update, so push the whole
  // camera block before rendering.
  camera.Proxy->UpdateVTKObjects();
  if (pqView* view = pqActiveObjects::instance().activeView())
  {
    view->render();
  }
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::onAdd()
{
  CameraRow row;
  QString   error;
  if (!this->captureCamera(row, error))
  {
    this->setStatus(error, true);
    return;
  }
  row.Name = QStringLiteral("Camera_%1").arg(this->nextCameraIndex());

  QList<CameraRow> rows = this->collectRows();
  rows.push_back(row);
  this->rebuildRows(rows);
  this->writeBackProperty();

  if (this->View && this->View->selectionModel())
  {
    const int lastRow = this->Model->rowCount() - 1;
    this->View->selectionModel()->select(this->Model->index(lastRow, 0),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    this->View->setCurrentIndex(this->Model->index(lastRow, kColName));
  }
  this->setStatus(tr("Added %1 at (%2).").arg(row.Name, DisplayVector(row.Position)), false);
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::onUpdateSelected()
{
  const int row = this->selectedRow();
  if (row < 0)
  {
    this->setStatus(tr("Select a row to update from the current camera."), true);
    return;
  }
  QList<CameraRow> rows = this->collectRows();
  if (row >= rows.size())
  {
    this->setStatus(tr("The selected row no longer exists."), true);
    return;
  }
  const QString name = rows[row].Name;
  CameraRow     updated;
  QString       error;
  if (!this->captureCamera(updated, error))
  {
    this->setStatus(error, true);
    return;
  }
  updated.Name = name;
  rows[row]    = updated;
  this->rebuildRows(rows);
  this->writeBackProperty();
  if (this->View && this->View->selectionModel())
  {
    this->View->selectionModel()->select(
      this->Model->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    this->View->setCurrentIndex(this->Model->index(row, kColName));
  }
  this->setStatus(
    tr("Updated %1 from the current camera (%2).").arg(name, DisplayVector(updated.Position)),
    false);
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::onRemoveSelected()
{
  if (!this->View || !this->View->selectionModel())
  {
    return;
  }
  QList<int>            indices;
  const QModelIndexList selected = this->View->selectionModel()->selectedIndexes();
  for (const QModelIndex& index : selected)
  {
    indices.push_back(index.row());
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  if (indices.isEmpty())
  {
    this->setStatus(tr("Select one or more rows to delete."), true);
    return;
  }

  QList<CameraRow> rows = this->collectRows();
  for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i)
  {
    if (indices[i] >= 0 && indices[i] < rows.size())
    {
      rows.removeAt(indices[i]);
    }
  }
  this->rebuildRows(rows);
  this->writeBackProperty();
  this->setStatus(
    tr("Deleted %1 preset(s). %2 remaining.").arg(indices.size()).arg(rows.size()), false);
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::onApplySelected()
{
  const int row = this->selectedRow();
  if (row < 0)
  {
    this->setStatus(tr("Select a row to apply to the active 3D view."), true);
    return;
  }
  const QList<CameraRow> rows = this->collectRows();
  if (row >= rows.size())
  {
    this->setStatus(tr("The selected row no longer exists."), true);
    return;
  }
  const CameraRow& preset = rows[row];
  QString          error;
  if (!this->applyCamera(preset, error))
  {
    this->setStatus(error, true);
    return;
  }
  this->setStatus(tr("Applied %1 to the active view (%2).")
                    .arg(preset.Name.isEmpty() ? QStringLiteral("Camera_%1").arg(row) : preset.Name,
                      DisplayVector(preset.Position)),
    false);
}

//-----------------------------------------------------------------------------
void pqSHYXCameraAdjustWidget::onRowDoubleClicked(const QModelIndex& index)
{
  // Double-clicking the Name cell is how the name is edited; every other cell applies.
  if (index.isValid() && index.column() == kColName)
  {
    return;
  }
  this->onApplySelected();
}
