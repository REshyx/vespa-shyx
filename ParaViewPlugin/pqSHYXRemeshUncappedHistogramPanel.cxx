#include "pqSHYXRemeshUncappedHistogramPanel.h"

#include "pqApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"

#include "vtkSHYXRemeshWithEndpoint.h"

#include "vtkAlgorithm.h"
#include "vtkBoundingBox.h"
#include "vtkCommand.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkNew.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkSMInputProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"

#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
  const double v = helper.GetAsDouble();
  if (std::isfinite(v))
  {
    return v;
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

bool buildHistogramFromValues(const std::vector<double>& values, int nBins, QVector<double>& countsOut,
  double& rangeMinOut, double& rangeMaxOut, int& sampleCountOut)
{
  countsOut.fill(0.0, nBins);
  rangeMinOut = 0.0;
  rangeMaxOut = 0.0;
  sampleCountOut = 0;
  if (nBins < 1 || values.empty())
  {
    return false;
  }

  double vmin = std::numeric_limits<double>::infinity();
  double vmax = -std::numeric_limits<double>::infinity();
  int nValid = 0;
  for (double v : values)
  {
    if (!std::isfinite(v) || !(v > 0.0))
    {
      continue;
    }
    vmin = (std::min)(vmin, v);
    vmax = (std::max)(vmax, v);
    ++nValid;
  }
  if (nValid < 1 || !std::isfinite(vmin) || !std::isfinite(vmax))
  {
    return false;
  }
  if (!(vmax > vmin))
  {
    vmax = vmin + (std::max)(1.0e-12, std::abs(vmin) * 1.0e-9);
  }

  const double span = vmax - vmin;
  for (double v : values)
  {
    if (!std::isfinite(v) || !(v > 0.0))
    {
      continue;
    }
    double t = (v - vmin) / span;
    t = (std::min)(1.0, (std::max)(0.0, t));
    int bin = static_cast<int>(t * (nBins - 1));
    if (bin < 0)
    {
      bin = 0;
    }
    else if (bin >= nBins)
    {
      bin = nBins - 1;
    }
    countsOut[bin] += 1.0;
  }
  rangeMinOut = vmin;
  rangeMaxOut = vmax;
  sampleCountOut = nValid;
  return true;
}

vtkPolyData* fetchInputPolyData(vtkSMProxy* filterProxy, bool updateUpstream)
{
  auto* inputProp = vtkSMInputProperty::SafeDownCast(filterProxy->GetProperty("Input"));
  if (!inputProp || inputProp->GetNumberOfProxies() == 0)
  {
    return nullptr;
  }
  auto* sourceProxy = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
  if (!sourceProxy)
  {
    return nullptr;
  }
  if (updateUpstream)
  {
    sourceProxy->UpdatePipeline();
  }
  auto* algo = vtkAlgorithm::SafeDownCast(sourceProxy->GetClientSideObject());
  if (!algo)
  {
    return nullptr;
  }
  const unsigned int port = inputProp->GetOutputPortForConnection(0);
  return vtkPolyData::SafeDownCast(algo->GetOutputDataObject(static_cast<int>(port)));
}

} // namespace

//------------------------------------------------------------------------------
class pqSHYXRemeshUncappedHistogramPanel::Canvas : public QWidget
{
public:
  explicit Canvas(QWidget* parent = nullptr)
    : QWidget(parent)
  {
    this->setMinimumHeight(140);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void setHistogram(const QVector<double>& counts, double rangeMin, double rangeMax, int sampleCount)
  {
    this->Counts = counts;
    this->RangeMin = rangeMin;
    this->RangeMax = rangeMax;
    this->SampleCount = sampleCount;
    this->update();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRectF r = this->rect().adjusted(8, 8, -8, -22);
    p.fillRect(this->rect(), QColor(250, 250, 250));
    p.setPen(QColor(200, 200, 200));
    p.drawRect(r);

    if (this->Counts.isEmpty() || this->SampleCount < 1)
    {
      p.setPen(QColor(120, 120, 120));
      p.drawText(r, Qt::AlignCenter, tr("Waiting for Input wall mesh…"));
      return;
    }

    double maxCount = 0.0;
    for (double c : this->Counts)
    {
      maxCount = (std::max)(maxCount, c);
    }
    if (!(maxCount > 0.0))
    {
      p.setPen(QColor(120, 120, 120));
      p.drawText(r, Qt::AlignCenter, tr("No finite ICC sizes"));
      return;
    }

    const int n = this->Counts.size();
    const double barW = r.width() / static_cast<double>(n);
    p.setBrush(QColor(120, 140, 170));
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i)
    {
      const double h = (this->Counts[i] / maxCount) * r.height();
      const QRectF bar(r.left() + i * barW, r.bottom() - h, (std::max)(1.0, barW - 1.0), h);
      p.drawRect(bar);
    }

    p.setPen(QColor(60, 60, 60));
    const QString minText = QString::number(this->RangeMin, 'g', 4);
    const QString maxText = QString::number(this->RangeMax, 'g', 4);
    const QRectF labelBand(this->rect().adjusted(8, 0, -8, -2));
    p.drawText(labelBand, Qt::AlignLeft | Qt::AlignBottom, minText);
    p.drawText(labelBand, Qt::AlignRight | Qt::AlignBottom, maxText);
  }

private:
  QVector<double> Counts;
  double RangeMin = 0.0;
  double RangeMax = 0.0;
  int SampleCount = 0;
};

//------------------------------------------------------------------------------
pqSHYXRemeshUncappedHistogramPanel::pqSHYXRemeshUncappedHistogramPanel(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* /*smgroup*/, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  this->SummaryLabel = new QLabel(this);
  this->SummaryLabel->setWordWrap(true);
  this->SummaryLabel->setText(tr("ICC size after Min/Max clamp (pre-remesh preview)"));
  vbox->addWidget(this->SummaryLabel);

  this->Chart = new Canvas(this);
  vbox->addWidget(this->Chart);

  this->DebounceTimer = new QTimer(this);
  this->DebounceTimer->setSingleShot(true);
  this->DebounceTimer->setInterval(350);
  QObject::connect(this->DebounceTimer, &QTimer::timeout, this,
    &pqSHYXRemeshUncappedHistogramPanel::runPreview);

  this->PropertyConnect = vtkEventQtSlotConnect::New();
  for (const char* name :
    { "AdaptiveTolerance", "MinEdgeLength", "MaxEdgeLength", "ScaleToRange",
      "AdaptiveSizingNeighborMaxRatio" })
  {
    if (vtkSMProperty* p = smproxy->GetProperty(name))
    {
      this->PropertyConnect->Connect(
        p, vtkCommand::UncheckedPropertyModifiedEvent, this, SLOT(schedulePreviewDebounced()));
      this->PropertyConnect->Connect(
        p, vtkCommand::ModifiedEvent, this, SLOT(schedulePreviewDebounced()));
    }
  }
  for (const char* name :
    { "Input", "EndpointIndexArrayName", "LargestConnectedRegionOnly", "EndpointIndexAllScalars" })
  {
    if (vtkSMProperty* p = smproxy->GetProperty(name))
    {
      this->PropertyConnect->Connect(
        p, vtkCommand::ModifiedEvent, this, SLOT(schedulePreview()));
      this->PropertyConnect->Connect(
        p, vtkCommand::UncheckedPropertyModifiedEvent, this, SLOT(schedulePreview()));
    }
  }

  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    this->PipelineSource = smm->findItem<pqPipelineSource*>(smproxy);
    if (this->PipelineSource)
    {
      QObject::connect(this->PipelineSource.data(),
        static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(&pqPipelineSource::dataUpdated),
        this, &pqSHYXRemeshUncappedHistogramPanel::onPipelineDataUpdated);
    }
  }

  this->schedulePreview();
}

//------------------------------------------------------------------------------
pqSHYXRemeshUncappedHistogramPanel::~pqSHYXRemeshUncappedHistogramPanel()
{
  if (this->PropertyConnect)
  {
    this->PropertyConnect->Delete();
    this->PropertyConnect = nullptr;
  }
}

//------------------------------------------------------------------------------
void pqSHYXRemeshUncappedHistogramPanel::schedulePreview()
{
  if (this->DebounceTimer && this->DebounceTimer->isActive())
  {
    this->DebounceTimer->stop();
  }
  if (this->PreviewPending)
  {
    return;
  }
  this->PreviewPending = true;
  QTimer::singleShot(0, this, [this]() {
    this->PreviewPending = false;
    this->runPreview();
  });
}

//------------------------------------------------------------------------------
void pqSHYXRemeshUncappedHistogramPanel::schedulePreviewDebounced()
{
  // Coalesce rapid Tolerance / Min / Max spinbox edits; ICC preview is not free.
  if (this->DebounceTimer)
  {
    this->DebounceTimer->start();
  }
  else
  {
    this->schedulePreview();
  }
}

//------------------------------------------------------------------------------
void pqSHYXRemeshUncappedHistogramPanel::onPipelineDataUpdated()
{
  // Recompute from current Input / proxy settings (same path as pre-Apply preview).
  this->schedulePreview();
}

//------------------------------------------------------------------------------
void pqSHYXRemeshUncappedHistogramPanel::runPreview()
{
  if (!this->computePreviewFromInput())
  {
    this->setHistogramDisplay(QVector<double>(), 0.0, 0.0, 0, 0.0, 0.0, false);
  }
}

//------------------------------------------------------------------------------
void pqSHYXRemeshUncappedHistogramPanel::setHistogramDisplay(const QVector<double>& counts,
  double rangeMin, double rangeMax, int sampleCount, double uncappedMin, double uncappedMax,
  bool hasUncappedRange)
{
  if (this->SummaryLabel)
  {
    if (sampleCount > 0)
    {
      QString text = tr("ICC size after Min/Max clamp: [%1, %2], %3 vertices")
                       .arg(rangeMin, 0, 'g', 4)
                       .arg(rangeMax, 0, 'g', 4)
                       .arg(sampleCount);
      if (hasUncappedRange)
      {
        text += tr("; uncapped: [%1, %2]")
                  .arg(uncappedMin, 0, 'g', 4)
                  .arg(uncappedMax, 0, 'g', 4);
      }
      this->SummaryLabel->setText(text);
    }
    else
    {
      this->SummaryLabel->setText(tr("ICC size after Min/Max clamp (pre-remesh preview)"));
    }
  }
  if (this->Chart)
  {
    this->Chart->setHistogram(counts, rangeMin, rangeMax, sampleCount);
  }
}

//------------------------------------------------------------------------------
bool pqSHYXRemeshUncappedHistogramPanel::computePreviewFromInput()
{
  vtkSMProxy* filterProxy = this->proxy();
  if (!filterProxy)
  {
    return false;
  }

  vtkPolyData* inputPd = fetchInputPolyData(filterProxy, /*updateUpstream=*/true);
  if (!inputPd || inputPd->GetNumberOfPoints() < 1 || inputPd->GetNumberOfCells() < 1)
  {
    return false;
  }

  double tol = readDoubleUnchecked(filterProxy->GetProperty("AdaptiveTolerance"), 0.01);
  if (!(tol > 0.0))
  {
    return false;
  }

  const bool largestOnly =
    readIntUnchecked(filterProxy->GetProperty("LargestConnectedRegionOnly"), 1) != 0;
  const bool allScalars =
    readIntUnchecked(filterProxy->GetProperty("EndpointIndexAllScalars"), 0) != 0;
  const bool scaleToRange =
    readIntUnchecked(filterProxy->GetProperty("ScaleToRange"), 0) != 0;
  const double neighborRatio =
    readDoubleUnchecked(filterProxy->GetProperty("AdaptiveSizingNeighborMaxRatio"), 1.5);

  double b[6];
  inputPd->GetBounds(b);
  vtkBoundingBox box;
  box.SetBounds(b);
  const double L = box.GetMaxLength();
  double fallbackMin = 1.0e-6;
  double fallbackMax = 1.0e6;
  if (L > 0.0)
  {
    fallbackMin = 0.001 * L;
    fallbackMax = 0.05 * L;
    if (!(fallbackMax > fallbackMin))
    {
      fallbackMax = fallbackMin * 10.0;
    }
  }

  double minLen = readDoubleUnchecked(filterProxy->GetProperty("MinEdgeLength"), fallbackMin);
  double maxLen = readDoubleUnchecked(filterProxy->GetProperty("MaxEdgeLength"), fallbackMax);
  if (!(minLen > 0.0 && maxLen > minLen))
  {
    minLen = fallbackMin;
    maxLen = fallbackMax;
  }

  vtkNew<vtkSHYXRemeshWithEndpoint> preview;
  preview->SetInputData(inputPd);
  preview->SetAdaptiveTolerance(tol);
  preview->SetMinEdgeLength(minLen);
  preview->SetMaxEdgeLength(maxLen);
  preview->SetAdaptiveSizingNeighborMaxRatio(neighborRatio);
  preview->SetLargestConnectedRegionOnly(largestOnly);
  preview->SetEndpointIndexAllScalars(allScalars);
  preview->SetScaleToRange(scaleToRange);
  preview->EnableWallRemeshOff();
  preview->EnableCapRemeshOff();

  if (auto* arrProp =
        vtkSMStringVectorProperty::SafeDownCast(filterProxy->GetProperty("EndpointIndexArrayName")))
  {
    if (arrProp->GetNumberOfElements() >= 5)
    {
      const char* assocStr = arrProp->GetElement(3);
      const char* name = arrProp->GetElement(4);
      const int assoc = (assocStr && assocStr[0] != '\0')
        ? std::atoi(assocStr)
        : vtkDataObject::FIELD_ASSOCIATION_CELLS;
      if (name && name[0] != '\0')
      {
        preview->SetInputArrayToProcess(0, 0, 0, assoc, name);
      }
    }
  }

  preview->Update();

  vtkPolyData* outPd = vtkPolyData::SafeDownCast(preview->GetOutputDataObject(0));
  if (!outPd)
  {
    return false;
  }
  vtkDataArray* sizeArr = outPd->GetPointData()->GetArray("VespaSizeGlobal");
  if (!sizeArr || sizeArr->GetNumberOfTuples() < 1)
  {
    return false;
  }

  std::vector<double> sizes;
  sizes.reserve(static_cast<std::size_t>(sizeArr->GetNumberOfTuples()));
  for (vtkIdType i = 0; i < sizeArr->GetNumberOfTuples(); ++i)
  {
    sizes.push_back(sizeArr->GetComponent(i, 0));
  }

  constexpr int nBins = vtkSHYXRemeshWithEndpoint::UncappedSizeHistBinCount;
  QVector<double> countVec;
  double rangeMin = 0.0;
  double rangeMax = 0.0;
  int sampleCount = 0;
  if (!buildHistogramFromValues(sizes, nBins, countVec, rangeMin, rangeMax, sampleCount))
  {
    return false;
  }

  double uncappedMin = 0.0;
  double uncappedMax = 0.0;
  bool hasUncapped = false;
  if (preview->GetUncappedSizeHistSampleCount() > 0)
  {
    if (const double* ur = preview->GetUncappedSizeHistRange())
    {
      uncappedMin = ur[0];
      uncappedMax = ur[1];
      hasUncapped = std::isfinite(uncappedMin) && std::isfinite(uncappedMax) &&
        (uncappedMax >= uncappedMin) && (uncappedMax > 0.0 || uncappedMin > 0.0);
    }
  }

  this->setHistogramDisplay(
    countVec, rangeMin, rangeMax, sampleCount, uncappedMin, uncappedMax, hasUncapped);
  return true;
}
