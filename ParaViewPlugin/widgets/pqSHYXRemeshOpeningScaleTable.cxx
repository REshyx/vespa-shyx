#include "pqSHYXRemeshOpeningScaleTable.h"

#include "pqApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"

#include "vtkSHYXRemeshWithEndpoint.h"

#include "vtkAlgorithm.h"
#include "vtkCommand.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkNew.h"
#include "vtkPolyData.h"
#include "vtkSMDoubleVectorProperty.h"
#include "vtkSMInputProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"

#include <QAbstractItemView>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QShowEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{

double readDoubleUnchecked(vtkSMProperty* prop, double fallback)
{
  if (!prop)
  {
    return fallback;
  }
  vtkSMPropertyHelper helper(prop);
  helper.SetUseUnchecked(true);
  const double value = helper.GetAsDouble();
  if (std::isfinite(value))
  {
    return value;
  }
  helper.SetUseUnchecked(false);
  const double checked = helper.GetAsDouble();
  return std::isfinite(checked) ? checked : fallback;
}

int readIntUnchecked(vtkSMProperty* prop, int fallback)
{
  if (!prop)
  {
    return fallback;
  }
  vtkSMPropertyHelper helper(prop);
  helper.SetUseUnchecked(true);
  return helper.GetAsInt();
}

const char* firstStringUnchecked(vtkSMProperty* prop)
{
  if (!prop)
  {
    return nullptr;
  }
  vtkSMPropertyHelper helper(prop);
  helper.SetUseUnchecked(true);
  if (helper.GetNumberOfElements() < 1)
  {
    helper.SetUseUnchecked(false);
    if (helper.GetNumberOfElements() < 1)
    {
      return nullptr;
    }
  }
  return helper.GetAsString(0);
}

vtkPolyData* fetchInputPolyData(vtkSMProxy* filterProxy)
{
  auto* inputProp = vtkSMInputProperty::SafeDownCast(filterProxy->GetProperty("Input"));
  if (!inputProp || inputProp->GetNumberOfProxies() < 1)
  {
    return nullptr;
  }
  auto* sourceProxy = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
  if (!sourceProxy)
  {
    return nullptr;
  }
  auto* algo = vtkAlgorithm::SafeDownCast(sourceProxy->GetClientSideObject());
  if (!algo)
  {
    return nullptr;
  }
  const unsigned int port = inputProp->GetOutputPortForConnection(0);
  return vtkPolyData::SafeDownCast(algo->GetOutputDataObject(static_cast<int>(port)));
}

double dist2(const double a[3], double x, double y, double z)
{
  const double dx = a[0] - x;
  const double dy = a[1] - y;
  const double dz = a[2] - z;
  return dx * dx + dy * dy + dz * dz;
}

} // namespace

pqSHYXRemeshOpeningScaleTable::pqSHYXRemeshOpeningScaleTable(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  Q_UNUSED(smgroup);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  this->Status = new QLabel(this);
  this->Status->setWordWrap(true);
  this->Status->setText(tr("Each row is one opening."));
  layout->addWidget(this->Status);

  this->Table = new QTableWidget(0, 5, this);
  this->Table->setObjectName("SHYXRemeshOpeningScaleTable");
  this->Table->setHorizontalHeaderLabels(
    { tr("Opening"), tr("Point"), tr("Points"), tr("Mean edge"), tr("Scale") });
  this->Table->verticalHeader()->setVisible(false);
  this->Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  this->Table->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->Table->setSelectionMode(QAbstractItemView::SingleSelection);
  this->Table->setMinimumHeight(120);
  this->Table->setMaximumHeight(280);
  this->Table->horizontalHeader()->setStretchLastSection(false);
  this->Table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  layout->addWidget(this->Table);

  this->Debounce = new QTimer(this);
  this->Debounce->setSingleShot(true);
  this->Debounce->setInterval(200);
  QObject::connect(this->Debounce, &QTimer::timeout, this, &pqSHYXRemeshOpeningScaleTable::refresh);

  this->PropertyConnect = vtkEventQtSlotConnect::New();
  for (const char* name : { "Input", "EnableEndpointCull", "EndpointIndexArrayName",
         "EndpointIndexAllScalars", "LargestConnectedRegionOnly", "BoundarySizeScale" })
  {
    if (vtkSMProperty* prop = smproxy->GetProperty(name))
    {
      this->PropertyConnect->Connect(
        prop, vtkCommand::ModifiedEvent, this, SLOT(scheduleRefresh()));
      this->PropertyConnect->Connect(
        prop, vtkCommand::UncheckedPropertyModifiedEvent, this, SLOT(scheduleRefresh()));
    }
  }

  this->watchInput();
  this->scheduleRefresh();
}

pqSHYXRemeshOpeningScaleTable::~pqSHYXRemeshOpeningScaleTable()
{
  if (this->PropertyConnect)
  {
    this->PropertyConnect->Delete();
    this->PropertyConnect = nullptr;
  }
}

void pqSHYXRemeshOpeningScaleTable::showEvent(QShowEvent* event)
{
  this->Superclass::showEvent(event);
  this->scheduleRefresh();
}

void pqSHYXRemeshOpeningScaleTable::scheduleRefresh()
{
  if (this->Updating || !this->Debounce)
  {
    return;
  }
  this->Debounce->start();
}

double pqSHYXRemeshOpeningScaleTable::boundaryScale() const
{
  return readDoubleUnchecked(this->proxy()->GetProperty("BoundarySizeScale"), 1.0);
}

void pqSHYXRemeshOpeningScaleTable::watchInput()
{
  QObject::disconnect(this->InputDataConnection);
  this->InputDataConnection = {};
  auto* core = pqApplicationCore::instance();
  if (!core || !core->getServerManagerModel() || !this->proxy())
  {
    return;
  }
  auto* inputProp = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("Input"));
  if (!inputProp || inputProp->GetNumberOfProxies() < 1)
  {
    return;
  }
  auto* pqSource = core->getServerManagerModel()->findItem<pqPipelineSource*>(inputProp->GetProxy(0));
  if (!pqSource)
  {
    return;
  }
  this->InputDataConnection = QObject::connect(pqSource,
    QOverload<pqPipelineSource*>::of(&pqPipelineSource::dataUpdated), this,
    &pqSHYXRemeshOpeningScaleTable::scheduleRefresh);
}

std::vector<pqSHYXRemeshOpeningScaleTable::SavedScale> pqSHYXRemeshOpeningScaleTable::readSaved() const
{
  std::vector<SavedScale> saved;
  auto* dvp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OpeningBoundaryScales"));
  if (!dvp)
  {
    return saved;
  }
  unsigned int count = dvp->GetNumberOfUncheckedElements();
  const bool useUnchecked = count > 0;
  if (!useUnchecked)
  {
    count = dvp->GetNumberOfElements();
  }
  count = (count / 4) * 4;
  saved.reserve(count / 4);
  for (unsigned int i = 0; i < count; i += 4)
  {
    SavedScale item;
    if (useUnchecked)
    {
      item.X = dvp->GetUncheckedElement(i);
      item.Y = dvp->GetUncheckedElement(i + 1);
      item.Z = dvp->GetUncheckedElement(i + 2);
      item.Scale = dvp->GetUncheckedElement(i + 3);
    }
    else
    {
      item.X = dvp->GetElement(i);
      item.Y = dvp->GetElement(i + 1);
      item.Z = dvp->GetElement(i + 2);
      item.Scale = dvp->GetElement(i + 3);
    }
    saved.push_back(item);
  }
  return saved;
}

void pqSHYXRemeshOpeningScaleTable::fillRows(
  const std::vector<Row>& discovered, const std::vector<SavedScale>& saved)
{
  this->Rows = discovered;
  const double fallback = this->boundaryScale();
  for (Row& row : this->Rows)
  {
    row.Scale = fallback;
  }
  if (saved.empty() || this->Rows.empty())
  {
    return;
  }

  double minSep2 = std::numeric_limits<double>::infinity();
  if (this->Rows.size() >= 2)
  {
    for (std::size_t i = 0; i < this->Rows.size(); ++i)
    {
      for (std::size_t j = i + 1; j < this->Rows.size(); ++j)
      {
        const double d2 = dist2(this->Rows[i].Centroid, this->Rows[j].Centroid[0],
          this->Rows[j].Centroid[1], this->Rows[j].Centroid[2]);
        minSep2 = (std::min)(minSep2, d2);
      }
    }
  }

  struct Cand
  {
    double D2;
    std::size_t Row;
    std::size_t Key;
  };
  std::vector<Cand> cands;
  cands.reserve(this->Rows.size() * saved.size());
  for (std::size_t row = 0; row < this->Rows.size(); ++row)
  {
    for (std::size_t key = 0; key < saved.size(); ++key)
    {
      cands.push_back(Cand{ dist2(this->Rows[row].Centroid, saved[key].X, saved[key].Y, saved[key].Z),
        row, key });
    }
  }
  std::sort(cands.begin(), cands.end(),
    [](const Cand& a, const Cand& b) { return a.D2 < b.D2; });
  std::vector<char> usedRow(this->Rows.size(), 0);
  std::vector<char> usedKey(saved.size(), 0);
  for (const Cand& cand : cands)
  {
    if (usedRow[cand.Row] || usedKey[cand.Key])
    {
      continue;
    }
    const Row& row = this->Rows[cand.Row];
    const double perimeter = row.MeanEdge * static_cast<double>(row.PointCount);
    double maxD2 = perimeter * perimeter;
    if (minSep2 < std::numeric_limits<double>::infinity() && minSep2 > 1.0e-18)
    {
      maxD2 = (std::min)(maxD2, 0.2401 * minSep2);
    }
    if (cand.D2 > maxD2)
    {
      continue;
    }
    usedRow[cand.Row] = 1;
    usedKey[cand.Key] = 1;
    double scale = saved[cand.Key].Scale;
    if (!(scale > 0.0))
    {
      scale = fallback;
    }
    this->Rows[cand.Row].Scale = scale;
  }
}

void pqSHYXRemeshOpeningScaleTable::rebuildTable()
{
  this->Updating = true;
  this->Table->setRowCount(static_cast<int>(this->Rows.size()));
  for (int row = 0; row < static_cast<int>(this->Rows.size()); ++row)
  {
    const Row& item = this->Rows[static_cast<std::size_t>(row)];
    auto* opening = new QTableWidgetItem(QString::number(row + 1));
    auto* point = new QTableWidgetItem(tr("%1, %2, %3")
                                         .arg(item.Sample[0], 0, 'g', 6)
                                         .arg(item.Sample[1], 0, 'g', 6)
                                         .arg(item.Sample[2], 0, 'g', 6));
    auto* count = new QTableWidgetItem(QString::number(item.PointCount));
    auto* mean = new QTableWidgetItem(QString::number(item.MeanEdge, 'g', 5));
    point->setToolTip(tr("Boundary vertex nearest the opening center (%1, %2, %3). "
                         "Scale applies to every vertex on this opening.")
                        .arg(item.Centroid[0], 0, 'g', 6)
                        .arg(item.Centroid[1], 0, 'g', 6)
                        .arg(item.Centroid[2], 0, 'g', 6));
    this->Table->setItem(row, 0, opening);
    this->Table->setItem(row, 1, point);
    this->Table->setItem(row, 2, count);
    this->Table->setItem(row, 3, mean);

    auto* scale = new QDoubleSpinBox(this->Table);
    scale->setRange(1.0e-6, 1.0e6);
    scale->setDecimals(4);
    scale->setSingleStep(0.1);
    scale->setValue(item.Scale);
    QObject::connect(scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
      &pqSHYXRemeshOpeningScaleTable::onScaleEdited);
    this->Table->setCellWidget(row, 4, scale);
  }
  this->Table->resizeColumnToContents(0);
  this->Table->resizeColumnToContents(2);
  this->Table->resizeColumnToContents(3);
  this->Updating = false;
}

void pqSHYXRemeshOpeningScaleTable::refresh()
{
  if (auto* core = pqApplicationCore::instance())
  {
    if (core->isLoadingState())
    {
      if (this->isVisible() && this->Debounce)
      {
        this->Debounce->start();
      }
      return;
    }
  }
  if (!this->isVisible())
  {
    return;
  }
  this->watchInput();

  vtkPolyData* input = this->proxy() ? fetchInputPolyData(this->proxy()) : nullptr;
  if (!input || input->GetNumberOfPoints() < 1 || input->GetNumberOfCells() < 1)
  {
    this->Rows.clear();
    this->rebuildTable();
    this->Status->setText(tr("No input surface."));
    return;
  }

  vtkNew<vtkSHYXRemeshWithEndpoint> probe;
  probe->SetEnableEndpointCull(readIntUnchecked(this->proxy()->GetProperty("EnableEndpointCull"), 1) != 0);
  probe->SetEndpointIndexAllScalars(
    readIntUnchecked(this->proxy()->GetProperty("EndpointIndexAllScalars"), 0) != 0);
  probe->SetLargestConnectedRegionOnly(
    readIntUnchecked(this->proxy()->GetProperty("LargestConnectedRegionOnly"), 1) != 0);
  if (const char* name = firstStringUnchecked(this->proxy()->GetProperty("EndpointIndexArrayName")))
  {
    probe->SetEndpointIndexArrayName(name);
  }

  std::vector<vtkSHYXRemeshWithEndpoint::OpeningLoop> loops;
  if (!probe->CollectOpeningLoops(input, loops))
  {
    this->Rows.clear();
    this->rebuildTable();
    this->Status->setText(tr("Could not extract a wall surface."));
    return;
  }

  std::vector<Row> discovered;
  discovered.reserve(loops.size());
  for (const auto& loop : loops)
  {
    Row row;
    row.Centroid[0] = loop.Centroid[0];
    row.Centroid[1] = loop.Centroid[1];
    row.Centroid[2] = loop.Centroid[2];
    row.Sample[0] = loop.SamplePoint[0];
    row.Sample[1] = loop.SamplePoint[1];
    row.Sample[2] = loop.SamplePoint[2];
    row.MeanEdge = loop.MeanEdgeLength;
    row.PointCount = loop.PointCount;
    discovered.push_back(row);
  }
  this->fillRows(discovered, this->readSaved());
  this->rebuildTable();
  if (this->Rows.empty())
  {
    this->Status->setText(tr("No open boundary on the extracted wall."));
  }
  else
  {
    this->Status->setText(tr("One row per opening. Point is a boundary vertex of that opening. "
                             "Scale multiplies the whole opening's mean edge length. "
                             ">1 coarsens, <1 refines."));
  }
}

void pqSHYXRemeshOpeningScaleTable::onScaleEdited()
{
  if (this->Updating || this->Rows.empty())
  {
    return;
  }
  this->writeRows(false);
}

void pqSHYXRemeshOpeningScaleTable::writeRows(bool pushChecked)
{
  auto* dvp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OpeningBoundaryScales"));
  if (!dvp || this->Rows.empty())
  {
    return;
  }
  std::vector<double> packed;
  packed.reserve(this->Rows.size() * 4);
  for (int row = 0; row < static_cast<int>(this->Rows.size()); ++row)
  {
    const Row& item = this->Rows[static_cast<std::size_t>(row)];
    double scale = item.Scale;
    if (auto* spin = qobject_cast<QDoubleSpinBox*>(this->Table->cellWidget(row, 4)))
    {
      scale = spin->value();
      this->Rows[static_cast<std::size_t>(row)].Scale = scale;
    }
    packed.push_back(item.Centroid[0]);
    packed.push_back(item.Centroid[1]);
    packed.push_back(item.Centroid[2]);
    packed.push_back(scale);
  }
  this->Updating = true;
  if (pushChecked)
  {
    dvp->SetElements(packed.data(), static_cast<unsigned int>(packed.size()));
  }
  else
  {
    dvp->SetUncheckedElements(packed.data(), static_cast<unsigned int>(packed.size()));
  }
  this->Updating = false;
  if (!pushChecked)
  {
    Q_EMIT this->changeAvailable();
    Q_EMIT this->changeFinished();
  }
}

void pqSHYXRemeshOpeningScaleTable::apply()
{
  auto* dvp =
    vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OpeningBoundaryScales"));
  // Only push a list the user edited (or a state file already stored). An empty list keeps
  // Boundary size scale as the shared factor, including when this widget is hidden.
  if (dvp && dvp->GetNumberOfUncheckedElements() > 0)
  {
    this->writeRows(true);
  }
  this->Superclass::apply();
  if (this->proxy())
  {
    this->proxy()->UpdateVTKObjects();
  }
}

void pqSHYXRemeshOpeningScaleTable::reset()
{
  this->Superclass::reset();
  if (auto* dvp =
        vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OpeningBoundaryScales")))
  {
    this->Updating = true;
    dvp->ClearUncheckedElements();
    this->Updating = false;
  }
  this->refresh();
}
