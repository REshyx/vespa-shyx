#include "pqArrayCurveMapperPanel.h"

#include "pqSHYXTransferCurveWidget.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

#include <vtkAlgorithm.h>
#include <vtkCellData.h>
#include <vtkColorTransferFunction.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkNew.h>
#include <vtkPVArrayInformation.h>
#include <vtkPVDataInformation.h>
#include <vtkPVDataSetAttributesInformation.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPointData.h>
#include <vtkSMDoubleVectorProperty.h>
#include <vtkSMInputProperty.h>
#include <vtkSMProperty.h>
#include <vtkSMPropertyGroup.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMProxy.h>
#include <vtkSMProxyProperty.h>
#include <vtkSMSourceProxy.h>
#include <vtkSMStringVectorProperty.h>
#include <vtkTable.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr int kHistBins = 25;
constexpr int kDefaultChartHeightPx = 380;

static QSpinBox* createChartHeightSpinBox(QWidget* parent, int defaultValue)
{
    auto* spin = new QSpinBox(parent);
    spin->setRange(120, 800);
    spin->setSingleStep(20);
    spin->setSuffix(QStringLiteral(" px"));
    spin->setValue(defaultValue);
    spin->setMinimumWidth(72);
    return spin;
}

static QDoubleSpinBox* createPercentSpinBox(QWidget* parent, double defaultValue)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(0.0, 100.0);
    spin->setDecimals(0);
    spin->setSingleStep(5.0);
    spin->setSuffix(QStringLiteral("%"));
    spin->setValue(defaultValue);
    spin->setMinimumWidth(56);
    return spin;
}

static QDoubleSpinBox* createRangeSpinBox(QWidget* parent)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(6);
    spin->setRange(-1e20, 1e20);
    spin->setSingleStep(0.01);
    spin->setMinimumWidth(72);
    return spin;
}

bool collectFiniteScalars(vtkDataArray* arr, std::vector<double>& valuesOut)
{
    valuesOut.clear();
    if (!arr || arr->GetNumberOfComponents() < 1)
    {
        return false;
    }

    const int numComp = arr->GetNumberOfComponents();
    const vtkIdType n = arr->GetNumberOfTuples();
    valuesOut.reserve(static_cast<std::size_t>(n));
    std::vector<double> tuple(static_cast<std::size_t>(numComp));

    for (vtkIdType i = 0; i < n; ++i)
    {
        double raw = 0.0;
        if (numComp > 1)
        {
            arr->GetTuple(i, tuple.data());
            double sumSq = 0.0;
            for (int c = 0; c < numComp; ++c)
            {
                sumSq += tuple[static_cast<std::size_t>(c)] * tuple[static_cast<std::size_t>(c)];
            }
            raw = std::sqrt(sumSq);
        }
        else
        {
            raw = arr->GetTuple1(i);
        }
        if (std::isfinite(raw))
        {
            valuesOut.push_back(raw);
        }
    }
    return !valuesOut.empty();
}

std::vector<double> buildHistogramBins(
    const std::vector<double>& values, int numBins, double rangeMin, double rangeMax)
{
    std::vector<double> bins(static_cast<std::size_t>(numBins), 0.0);
    if (numBins < 1 || values.empty())
    {
        return bins;
    }
    const double span = rangeMax - rangeMin;
    if (span <= 0.0)
    {
        bins[0] = static_cast<double>(values.size());
        return bins;
    }
    for (double v : values)
    {
        double t = (v - rangeMin) / span;
        t = std::min(1.0, std::max(0.0, t));
        const int bin = static_cast<int>(t * (numBins - 1));
        bins[static_cast<std::size_t>(bin)] += 1.0;
    }
    return bins;
}

vtkTable* buildHistogramVtkTable(
    const std::vector<double>& values, int numBins, double rangeMin, double rangeMax)
{
    if (values.empty() || numBins < 1 || !(rangeMax > rangeMin))
    {
        return nullptr;
    }

    const std::vector<double> bins = buildHistogramBins(values, numBins, rangeMin, rangeMax);
    const double span = rangeMax - rangeMin;

    vtkNew<vtkTable> table;
    vtkNew<vtkDoubleArray> extents;
    extents->SetName("bin_extents");
    vtkNew<vtkDoubleArray> binValues;
    binValues->SetName("bin_values");

    for (int i = 0; i < numBins; ++i)
    {
        extents->InsertNextValue(rangeMin + (i + 0.5) * span / numBins);
        binValues->InsertNextValue(bins[static_cast<std::size_t>(i)]);
    }

    table->AddColumn(extents);
    table->AddColumn(binValues);
    // Caller owns the returned reference (refcount 1); Set*HistogramTable will Register.
    table->Register(nullptr);
    return table.Get();
}

void adoptHistogramTable(pqSHYXTransferCurveWidget* widget, vtkTable* table, bool mapped)
{
    if (!widget)
    {
        if (table)
        {
            table->Delete();
        }
        return;
    }
    if (mapped)
    {
        widget->setMappedHistogramTable(table);
    }
    else
    {
        widget->setHistogramTable(table);
    }
    if (table)
    {
        table->Delete(); // balance Register/New; widget holds its own ref
    }
}

std::vector<double> computeMappedValues(const std::vector<double>& values, vtkPiecewiseFunction* pwf,
    double inMin, double inMax, double outMin, double outMax)
{
    std::vector<double> mapped;
    if (!pwf || pwf->GetSize() < 2 || !(inMax > inMin) || !(outMax > outMin))
    {
        return mapped;
    }
    mapped.reserve(values.size());
    for (double raw : values)
    {
        const double clamped = std::max(inMin, std::min(inMax, raw));
        const double y = std::max(outMin, std::min(outMax, pwf->GetValue(clamped)));
        mapped.push_back(y);
    }
    return mapped;
}

/** Stacks curve editor + transparent clamp-range handle overlay. */
class CurveEditorHost : public QWidget
{
public:
    explicit CurveEditorHost(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        this->setChartPixelHeight(kDefaultChartHeightPx);
    }

    void setChartPixelHeight(int heightPx)
    {
        const int h = std::max(120, heightPx);
        setMinimumHeight(h);
        setMaximumHeight(h);
        updateGeometry();
    }

    void setCurveWidget(QWidget* curve) { CurveWidget = curve; }
    void setClampOverlay(QWidget* overlay) { ClampOverlay = overlay; }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        const QRect r = rect();
        if (CurveWidget)
        {
            CurveWidget->setGeometry(r);
        }
        if (ClampOverlay)
        {
            ClampOverlay->setGeometry(r);
            ClampOverlay->raise();
        }
    }

private:
    QWidget* CurveWidget = nullptr;
    QWidget* ClampOverlay = nullptr;
};

/** Paints clamp shading outside the active range; mouse-transparent overlay. */
class ClampRangeDecoration : public QWidget
{
public:
    explicit ClampRangeDecoration(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

    void setClampRange(double clampMin, double clampMax)
    {
        ClampMin = clampMin;
        ClampMax = clampMax;
        update();
    }

    void setViewRange(double viewMin, double viewMax)
    {
        ViewMin = viewMin;
        ViewMax = viewMax;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!(ViewMax > ViewMin))
        {
            return;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const QRect plot = rect().adjusted(4, 4, -4, -4);
        if (plot.width() < 10)
        {
            return;
        }

        const int xMin = this->xToPx(plot, ClampMin);
        const int xMax = this->xToPx(plot, ClampMax);

        p.fillRect(plot.left(), plot.top(), xMin - plot.left(), plot.height(),
            QColor(236, 236, 240, 120));
        p.fillRect(xMax, plot.top(), plot.right() - xMax + 1, plot.height(),
            QColor(236, 236, 240, 120));
    }

private:
    int xToPx(const QRect& plot, double x) const
    {
        const double span = ViewMax - ViewMin;
        if (span <= 0.0)
        {
            return plot.left();
        }
        return plot.left() + static_cast<int>(((x - ViewMin) / span) * plot.width());
    }

    double ClampMin = 0.0;
    double ClampMax = 1.0;
    double ViewMin = 0.0;
    double ViewMax = 1.0;
};

ClampRangeDecoration* clampDecoration(QWidget* w)
{
    return static_cast<ClampRangeDecoration*>(w);
}

} // namespace

//------------------------------------------------------------------------------
pqArrayCurveMapperPanel::pqArrayCurveMapperPanel(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
    : Superclass(smproxy, parentObject)
{
    Q_UNUSED(smgroup);

    this->PiecewiseFunction = vtkPiecewiseFunction::New();
    this->ClampRangeCTF = vtkColorTransferFunction::New();

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(6);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        auto* chartLabel = new QLabel(tr("Chart height"), this);
        chartLabel->setToolTip(tr("Height of the curve and histogram editor area"));
        row->addWidget(chartLabel);
        this->ChartHeightSpin = createChartHeightSpinBox(this, kDefaultChartHeightPx);
        row->addWidget(this->ChartHeightSpin);
        row->addSpacing(8);
        auto* histLabel = new QLabel(tr("Hist height"), this);
        histLabel->setToolTip(tr("Maximum histogram bar size as a fraction of the chart axis span"));
        row->addWidget(histLabel);
        this->HistogramHeightSpin = createPercentSpinBox(this, 60.0);
        row->addWidget(this->HistogramHeightSpin);
        row->addSpacing(8);
        auto* curveResetBtn = new QPushButton(tr("Reset curve"), this);
        curveResetBtn->setToolTip(
            tr("Set input/output range to array data min–max and clear to a linear identity curve"));
        connect(curveResetBtn, &QPushButton::clicked, this, &pqArrayCurveMapperPanel::onResetCurveClicked);
        row->addWidget(curveResetBtn);
        row->addStretch(1);
        vbox->addLayout(row);
    }

    this->EditorHost = new CurveEditorHost(this);
    this->CurveWidget = new pqSHYXTransferCurveWidget(this->EditorHost);
    this->ClampRangeOverlay = new ClampRangeDecoration(this->EditorHost);
    static_cast<CurveEditorHost*>(this->EditorHost)->setCurveWidget(this->CurveWidget);
    static_cast<CurveEditorHost*>(this->EditorHost)->setClampOverlay(this->ClampRangeOverlay);
    vbox->addWidget(this->EditorHost, 0);

    this->HintLabel = new QLabel(
        tr("X = input · Y = output · bottom bars = input distribution · "
           "left bars = mapped output distribution · click to add point · "
           "double-click point for midpoint/sharpness · "
           "drag side handles to clamp input · drag top/bottom handles for output range"),
        this);
    this->HintLabel->setStyleSheet(QStringLiteral("color: #666; font-size: 11px;"));
    this->HintLabel->setWordWrap(true);
    vbox->addWidget(this->HintLabel);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        auto* inLabel = new QLabel(tr("Input range"), this);
        inLabel->setMinimumWidth(72);
        row->addWidget(inLabel);
        this->InputMinSpin = createRangeSpinBox(this);
        row->addWidget(this->InputMinSpin);
        row->addWidget(new QLabel(QStringLiteral("—"), this));
        this->InputMaxSpin = createRangeSpinBox(this);
        row->addWidget(this->InputMaxSpin);
        auto* inputResetBtn = new QPushButton(tr("Reset"), this);
        inputResetBtn->setToolTip(tr("Reset to selected input array data range"));
        connect(inputResetBtn, &QPushButton::clicked, this, &pqArrayCurveMapperPanel::onResetInputRange);
        row->addWidget(inputResetBtn);
        row->addStretch(1);
        vbox->addLayout(row);
    }

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        auto* outLabel = new QLabel(tr("Output range"), this);
        outLabel->setMinimumWidth(72);
        row->addWidget(outLabel);
        this->OutputMinSpin = createRangeSpinBox(this);
        row->addWidget(this->OutputMinSpin);
        row->addWidget(new QLabel(QStringLiteral("—"), this));
        this->OutputMaxSpin = createRangeSpinBox(this);
        row->addWidget(this->OutputMaxSpin);
        auto* outResetBtn = new QPushButton(tr("Reset"), this);
        outResetBtn->setToolTip(tr("Reset to selected input array data range"));
        connect(outResetBtn, &QPushButton::clicked, this, &pqArrayCurveMapperPanel::onResetOutputRange);
        row->addWidget(outResetBtn);
        row->addStretch(1);
        vbox->addLayout(row);
    }

    this->StatsLabel = new QLabel(this);
    this->StatsLabel->setStyleSheet(QStringLiteral("color: #777; font-size: 11px;"));
    vbox->addWidget(this->StatsLabel);

    auto* inMinProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("InputRangeMin"));
    auto* inMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("InputRangeMax"));
    auto* outMinProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("OutputRangeMin"));
    auto* outMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("OutputRangeMax"));

    if (inMinProp)
        this->InputMinSpin->setValue(inMinProp->GetElement(0));
    if (inMaxProp)
        this->InputMaxSpin->setValue(inMaxProp->GetElement(0));
    if (outMinProp)
        this->OutputMinSpin->setValue(outMinProp->GetElement(0));
    if (outMaxProp)
        this->OutputMaxSpin->setValue(outMaxProp->GetElement(0));

    this->PrevInMin = this->InputMinSpin->value();
    this->PrevInMax = this->InputMaxSpin->value();
    this->PrevOutMin = this->OutputMinSpin->value();
    this->PrevOutMax = this->OutputMaxSpin->value();

    this->pullCurveFromProxy();
    this->migrateLegacyNormalizedCurve();
    this->applyDefaultRangesIfNeeded(true);
    if (this->PiecewiseFunction->GetSize() < 2)
    {
        this->setIdentityCurve();
    }
    this->pushCurveToProxy();
    this->setupCurveWidgetIfNeeded();
    this->syncChartOutputRange();
    this->syncClampRangeVisuals();
    this->syncChartHeightVisuals();

    connect(this->InputMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        &pqArrayCurveMapperPanel::onInputRangeChanged);
    connect(this->InputMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        &pqArrayCurveMapperPanel::onInputRangeChanged);
    connect(this->OutputMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        &pqArrayCurveMapperPanel::onOutputRangeChanged);
    connect(this->OutputMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        &pqArrayCurveMapperPanel::onOutputRangeChanged);
    connect(this->HistogramHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        &pqArrayCurveMapperPanel::onHistogramHeightChanged);
    connect(this->ChartHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
        &pqArrayCurveMapperPanel::onChartHeightChanged);

    this->refreshVisualization();
}

//------------------------------------------------------------------------------
pqArrayCurveMapperPanel::~pqArrayCurveMapperPanel()
{
    if (this->ClampRangeCTF)
    {
        this->ClampRangeCTF->Delete();
        this->ClampRangeCTF = nullptr;
    }
    if (this->PiecewiseFunction)
    {
        this->PiecewiseFunction->Delete();
        this->PiecewiseFunction = nullptr;
    }
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::setupCurveWidgetIfNeeded()
{
    if (this->CurveWidgetInitialized)
    {
        return;
    }
    this->CurveWidget->initialize(this->ClampRangeCTF, false, this->PiecewiseFunction, true);
    connect(this->CurveWidget, SIGNAL(controlPointsModified()), this, SLOT(onCurveModified()));
    connect(this->CurveWidget, SIGNAL(rangeHandlesRangeChanged(double, double)), this,
        SLOT(onRangeHandlesChanged(double, double)));
    connect(this->CurveWidget, SIGNAL(rangeHandlesDoubleClicked()), this,
        SLOT(onRangeHandlesDoubleClicked()));
    connect(this->CurveWidget, SIGNAL(outputRangeHandlesRangeChanged(double, double)), this,
        SLOT(onOutputRangeHandlesChanged(double, double)));

    this->CurveWidgetInitialized = true;
    this->syncChartOutputRange();
    this->syncHistogramHeightVisuals();
    this->syncChartHeightVisuals();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::syncChartHeightVisuals()
{
    if (this->EditorHost)
    {
        static_cast<CurveEditorHost*>(this->EditorHost)
            ->setChartPixelHeight(this->ChartHeightSpin->value());
    }
    if (this->CurveWidgetInitialized)
    {
        this->CurveWidget->render();
    }
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::syncHistogramHeightVisuals()
{
    if (!this->CurveWidgetInitialized)
    {
        return;
    }
    this->CurveWidget->setHistogramHeightFraction(this->HistogramHeightSpin->value() / 100.0);
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::syncClampRangeCTF()
{
    const double inMin = this->InputMinSpin->value();
    const double inMax = this->InputMaxSpin->value();
    if (!(inMax > inMin))
    {
        return;
    }

    this->ClampRangeCTF->RemoveAllPoints();
    this->ClampRangeCTF->AddRGBPoint(inMin, 0.45, 0.45, 0.45);
    this->ClampRangeCTF->AddRGBPoint(inMax, 0.55, 0.55, 0.55);

    if (this->CurveWidgetInitialized)
    {
        this->CurveWidget->render();
    }
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::syncChartOutputRange()
{
    const double outMin = this->OutputMinSpin->value();
    const double outMax = this->OutputMaxSpin->value();
    if (!(outMax > outMin))
    {
        return;
    }

    if (this->CurveWidgetInitialized)
    {
        this->CurveWidget->setOutputRange(outMin, outMax);
        const double inMin = this->InputMinSpin->value();
        const double inMax = this->InputMaxSpin->value();
        if (inMax > inMin)
        {
            this->CurveWidget->setCurveDataBounds(inMin, inMax, outMin, outMax);
        }
    }
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::syncClampRangeVisuals()
{
    this->syncClampRangeCTF();

    const double inMin = this->InputMinSpin->value();
    const double inMax = this->InputMaxSpin->value();
    if (!(inMax > inMin))
    {
        return;
    }

    double viewMin = inMin;
    double viewMax = inMax;
    if (this->PiecewiseFunction && this->PiecewiseFunction->GetSize() >= 2)
    {
        double pwfRange[2];
        this->PiecewiseFunction->GetRange(pwfRange);
        viewMin = pwfRange[0];
        viewMax = pwfRange[1];
    }

    auto* decoration = clampDecoration(this->ClampRangeOverlay);
    decoration->setViewRange(viewMin, viewMax);
    decoration->setClampRange(inMin, inMax);
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::updateHistogramTable(const std::vector<double>& values)
{
    if (!this->CurveWidgetInitialized)
    {
        return;
    }

    double histMin = this->InputMinSpin->value();
    double histMax = this->InputMaxSpin->value();
    if (this->PiecewiseFunction && this->PiecewiseFunction->GetSize() >= 2)
    {
        double pwfRange[2];
        this->PiecewiseFunction->GetRange(pwfRange);
        histMin = pwfRange[0];
        histMax = pwfRange[1];
    }

    adoptHistogramTable(
        this->CurveWidget, buildHistogramVtkTable(values, kHistBins, histMin, histMax), false);
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::updateMappedHistogramTable(const std::vector<double>& mappedValues)
{
    if (!this->CurveWidgetInitialized)
    {
        return;
    }

    adoptHistogramTable(this->CurveWidget,
        buildHistogramVtkTable(mappedValues, kHistBins, this->OutputMinSpin->value(),
            this->OutputMaxSpin->value()),
        true);
}

//------------------------------------------------------------------------------
bool pqArrayCurveMapperPanel::fetchInputValues(
    std::vector<double>& valuesOut, bool forcePipelineUpdate) const
{
    valuesOut.clear();

    auto* inputProp = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("Input"));
    auto* arrayNameProp =
        vtkSMStringVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputArrayName"));
    if (!inputProp || inputProp->GetNumberOfProxies() == 0 || !arrayNameProp)
    {
        return false;
    }

    auto* sourceProxy = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
    if (!sourceProxy)
    {
        return false;
    }

    if (forcePipelineUpdate)
    {
        sourceProxy->UpdatePipeline();
    }

    auto* algo = vtkAlgorithm::SafeDownCast(sourceProxy->GetClientSideObject());
    if (!algo)
    {
        return false;
    }

    auto* ds = vtkDataSet::SafeDownCast(algo->GetOutputDataObject(0));
    if (!ds)
    {
        return false;
    }

    const char* arrayName = arrayNameProp->GetElement(0);
    if (!arrayName || arrayName[0] == '\0')
    {
        return false;
    }

    vtkDataArray* arr = ds->GetCellData()->GetArray(arrayName);
    if (!arr)
    {
        arr = ds->GetPointData()->GetArray(arrayName);
    }
    return collectFiniteScalars(arr, valuesOut);
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::refreshVisualization(bool forcePipelineUpdate)
{
    std::vector<double> values;
    const bool hasData = this->fetchInputValues(values, forcePipelineUpdate);

    if (hasData)
    {
        this->DataExtentMin = *std::min_element(values.begin(), values.end());
        this->DataExtentMax = *std::max_element(values.begin(), values.end());
    }

    this->syncClampRangeVisuals();

    if (!hasData)
    {
        if (this->CurveWidgetInitialized)
        {
            this->CurveWidget->setHistogramTable(nullptr);
            this->CurveWidget->setMappedHistogramTable(nullptr);
        }
        this->StatsLabel->clear();
        return;
    }

    this->updateHistogramTable(values);
    this->updateMappedHistogramTable(computeMappedValues(values, this->PiecewiseFunction,
        this->InputMinSpin->value(), this->InputMaxSpin->value(), this->OutputMinSpin->value(),
        this->OutputMaxSpin->value()));
    if (this->CurveWidgetInitialized)
    {
        this->CurveWidget->render();
    }

    double mappedMin = std::numeric_limits<double>::max();
    double mappedMax = -std::numeric_limits<double>::max();
    const auto mapped = computeMappedValues(values, this->PiecewiseFunction,
        this->InputMinSpin->value(), this->InputMaxSpin->value(), this->OutputMinSpin->value(),
        this->OutputMaxSpin->value());
    for (double v : mapped)
    {
        mappedMin = std::min(mappedMin, v);
        mappedMax = std::max(mappedMax, v);
    }

    this->StatsLabel->setText(
        tr("Input  n=%1  [%2, %3]")
            .arg(static_cast<qlonglong>(values.size()))
            .arg(this->DataExtentMin, 0, 'g', 4)
            .arg(this->DataExtentMax, 0, 'g', 4) +
        (mappedMax >= mappedMin
                ? tr("   → mapped [%1, %2]")
                      .arg(mappedMin, 0, 'g', 4)
                      .arg(mappedMax, 0, 'g', 4)
                : QString()));
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onCurveModified()
{
    this->pushCurveToProxy();
    this->refreshVisualization();
    emit changeAvailable();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onRangeHandlesChanged(double rangeMin, double rangeMax)
{
    if (this->UpdatingUI || !(rangeMax > rangeMin))
    {
        return;
    }

    const double oldInMin = this->PrevInMin;
    const double oldInMax = this->PrevInMax;

    this->UpdatingUI = true;
    this->InputMinSpin->setValue(rangeMin);
    this->InputMaxSpin->setValue(rangeMax);
    this->UpdatingUI = false;

    if (auto* inMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin")))
    {
        inMinProp->SetElement(0, rangeMin);
    }
    if (auto* inMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax")))
    {
        inMaxProp->SetElement(0, rangeMax);
    }

    this->rescaleCurveX(oldInMin, oldInMax);
    this->syncClampRangeVisuals();
    this->refreshVisualization();
    emit changeAvailable();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onRangeHandlesDoubleClicked()
{
    this->onResetInputRange();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onOutputRangeHandlesChanged(double rangeMin, double rangeMax)
{
    if (this->UpdatingUI || !(rangeMax > rangeMin))
    {
        return;
    }

    this->UpdatingUI = true;
    this->OutputMinSpin->setValue(rangeMin);
    this->OutputMaxSpin->setValue(rangeMax);
    this->UpdatingUI = false;

    if (auto* outMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin")))
    {
        outMinProp->SetElement(0, rangeMin);
    }
    if (auto* outMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax")))
    {
        outMaxProp->SetElement(0, rangeMax);
    }

    this->onOutputRangeChanged();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onInputRangeChanged()
{
    if (this->UpdatingUI)
    {
        return;
    }

    if (auto* inMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin")))
    {
        inMinProp->SetElement(0, this->InputMinSpin->value());
    }
    if (auto* inMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax")))
    {
        inMaxProp->SetElement(0, this->InputMaxSpin->value());
    }

    this->rescaleCurveX(this->PrevInMin, this->PrevInMax);
    this->syncClampRangeVisuals();
    this->syncChartOutputRange();
    this->refreshVisualization();
    emit changeAvailable();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onOutputRangeChanged()
{
    if (this->UpdatingUI)
    {
        return;
    }

    const double outMin = this->OutputMinSpin->value();
    const double outMax = this->OutputMaxSpin->value();
    if (!(outMax > outMin))
    {
        return;
    }

    if (auto* outMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin")))
    {
        outMinProp->SetElement(0, outMin);
    }
    if (auto* outMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax")))
    {
        outMaxProp->SetElement(0, outMax);
    }

    this->rescaleCurveY(this->PrevOutMin, this->PrevOutMax);
    this->pushCurveToProxy();
    this->syncChartOutputRange();
    this->syncClampRangeVisuals();
    this->refreshVisualization();
    emit changeAvailable();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onHistogramHeightChanged()
{
    this->syncHistogramHeightVisuals();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onChartHeightChanged()
{
    this->syncChartHeightVisuals();
}

//------------------------------------------------------------------------------
bool pqArrayCurveMapperPanel::fetchInputArrayRange(double range[2]) const
{
    range[0] = 0.0;
    range[1] = 1.0;

    auto* inputProp = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("Input"));
    auto* arrayNameProp =
        vtkSMStringVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputArrayName"));
    if (!inputProp || inputProp->GetNumberOfProxies() == 0 || !arrayNameProp)
    {
        return false;
    }

    auto* sourceProxy = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
    if (!sourceProxy)
    {
        return false;
    }

    sourceProxy->UpdatePipeline();
    auto* dataInfo = sourceProxy->GetDataInformation();
    if (!dataInfo)
    {
        return false;
    }

    const char* arrayName = arrayNameProp->GetElement(0);
    if (!arrayName || arrayName[0] == '\0')
    {
        return false;
    }

    vtkPVArrayInformation* arrayInfo = nullptr;
    if (auto* cellInfo = dataInfo->GetCellDataInformation())
    {
        arrayInfo = cellInfo->GetArrayInformation(arrayName);
    }
    if (!arrayInfo)
    {
        if (auto* pointInfo = dataInfo->GetPointDataInformation())
        {
            arrayInfo = pointInfo->GetArrayInformation(arrayName);
        }
    }
    if (!arrayInfo)
    {
        return false;
    }

    if (arrayInfo->GetNumberOfComponents() > 1)
    {
        arrayInfo->GetComponentRange(-1, range);
    }
    else
    {
        arrayInfo->GetComponentRange(0, range);
    }
    return range[1] >= range[0];
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onResetInputRange()
{
    double range[2];
    if (!this->fetchInputArrayRange(range))
    {
        return;
    }

    const double oldInMin = this->PrevInMin;
    const double oldInMax = this->PrevInMax;

    this->UpdatingUI = true;
    this->InputMinSpin->setValue(range[0]);
    this->InputMaxSpin->setValue(range[1]);
    this->UpdatingUI = false;

    if (auto* inMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin")))
    {
        inMinProp->SetElement(0, range[0]);
    }
    if (auto* inMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax")))
    {
        inMaxProp->SetElement(0, range[1]);
    }

    this->rescaleCurveX(oldInMin, oldInMax);
    this->syncClampRangeVisuals();
    this->syncChartOutputRange();
    this->refreshVisualization(true);
    emit changeAvailable();
}

//------------------------------------------------------------------------------
bool pqArrayCurveMapperPanel::applyDefaultRangesIfNeeded(bool /*forcePipelineUpdate*/)
{
    if (!this->AutoDefaultsPending)
    {
        return false;
    }

    // XML placeholders are [0, 1]. Treat that as "not yet initialized from data".
    const bool inputIsPlaceholder =
        this->InputMinSpin->value() == 0.0 && this->InputMaxSpin->value() == 1.0;
    const bool outputIsPlaceholder =
        this->OutputMinSpin->value() == 0.0 && this->OutputMaxSpin->value() == 1.0;
    if (!inputIsPlaceholder && !outputIsPlaceholder)
    {
        this->AutoDefaultsPending = false;
        return false;
    }

    double range[2];
    if (!this->fetchInputArrayRange(range))
    {
        return false;
    }

    this->AutoDefaultsPending = false;

    this->UpdatingUI = true;
    if (inputIsPlaceholder)
    {
        this->InputMinSpin->setValue(range[0]);
        this->InputMaxSpin->setValue(range[1]);
    }
    if (outputIsPlaceholder)
    {
        this->OutputMinSpin->setValue(range[0]);
        this->OutputMaxSpin->setValue(range[1]);
    }
    this->UpdatingUI = false;

    if (inputIsPlaceholder)
    {
        if (auto* inMinProp = vtkSMDoubleVectorProperty::SafeDownCast(
                this->proxy()->GetProperty("InputRangeMin")))
        {
            inMinProp->SetElement(0, range[0]);
        }
        if (auto* inMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(
                this->proxy()->GetProperty("InputRangeMax")))
        {
            inMaxProp->SetElement(0, range[1]);
        }
    }
    if (outputIsPlaceholder)
    {
        if (auto* outMinProp = vtkSMDoubleVectorProperty::SafeDownCast(
                this->proxy()->GetProperty("OutputRangeMin")))
        {
            outMinProp->SetElement(0, range[0]);
        }
        if (auto* outMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(
                this->proxy()->GetProperty("OutputRangeMax")))
        {
            outMaxProp->SetElement(0, range[1]);
        }
    }

    this->setIdentityCurve();
    this->pushCurveToProxy();
    this->syncChartOutputRange();
    this->syncClampRangeVisuals();
    return inputIsPlaceholder || outputIsPlaceholder;
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onResetOutputRange()
{
    double range[2];
    if (!this->fetchInputArrayRange(range))
    {
        return;
    }

    this->UpdatingUI = true;
    this->OutputMinSpin->setValue(range[0]);
    this->OutputMaxSpin->setValue(range[1]);
    this->UpdatingUI = false;

    if (auto* outMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin")))
    {
        outMinProp->SetElement(0, range[0]);
    }
    if (auto* outMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax")))
    {
        outMaxProp->SetElement(0, range[1]);
    }

    this->onOutputRangeChanged();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onResetCurveClicked()
{
    double range[2];
    if (!this->fetchInputArrayRange(range))
    {
        return;
    }

    this->UpdatingUI = true;
    this->InputMinSpin->setValue(range[0]);
    this->InputMaxSpin->setValue(range[1]);
    this->OutputMinSpin->setValue(range[0]);
    this->OutputMaxSpin->setValue(range[1]);
    this->UpdatingUI = false;

    if (auto* inMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin")))
    {
        inMinProp->SetElement(0, range[0]);
    }
    if (auto* inMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax")))
    {
        inMaxProp->SetElement(0, range[1]);
    }
    if (auto* outMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin")))
    {
        outMinProp->SetElement(0, range[0]);
    }
    if (auto* outMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax")))
    {
        outMaxProp->SetElement(0, range[1]);
    }

    this->setIdentityCurve();
    this->pushCurveToProxy();
    this->syncChartOutputRange();
    this->syncClampRangeVisuals();
    if (this->CurveWidgetInitialized)
    {
        this->CurveWidget->render();
    }
    this->refreshVisualization(true);
    emit changeAvailable();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::select()
{
    if (this->applyDefaultRangesIfNeeded(true))
    {
        emit changeAvailable();
    }
    this->syncChartOutputRange();
    this->refreshVisualization(true);
    this->Superclass::select();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::apply()
{
    this->pushCurveToProxy();

    if (auto* inMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin")))
    {
        inMinProp->SetElement(0, this->InputMinSpin->value());
    }
    if (auto* inMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax")))
    {
        inMaxProp->SetElement(0, this->InputMaxSpin->value());
    }
    if (auto* outMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin")))
    {
        outMinProp->SetElement(0, this->OutputMinSpin->value());
    }
    if (auto* outMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax")))
    {
        outMaxProp->SetElement(0, this->OutputMaxSpin->value());
    }

    this->Superclass::apply();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::reset()
{
    this->UpdatingUI = true;
    if (auto* inMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin")))
    {
        this->InputMinSpin->setValue(inMinProp->GetElement(0));
    }
    if (auto* inMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax")))
    {
        this->InputMaxSpin->setValue(inMaxProp->GetElement(0));
    }
    if (auto* outMinProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin")))
    {
        this->OutputMinSpin->setValue(outMinProp->GetElement(0));
    }
    if (auto* outMaxProp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax")))
    {
        this->OutputMaxSpin->setValue(outMaxProp->GetElement(0));
    }
    this->UpdatingUI = false;

    this->pullCurveFromProxy();
    this->migrateLegacyNormalizedCurve();
    if (this->PiecewiseFunction && this->PiecewiseFunction->GetSize() < 2)
    {
        this->setIdentityCurve();
    }

    this->PrevInMin = this->InputMinSpin->value();
    this->PrevInMax = this->InputMaxSpin->value();
    this->PrevOutMin = this->OutputMinSpin->value();
    this->PrevOutMax = this->OutputMaxSpin->value();

    this->syncChartOutputRange();
    this->syncClampRangeVisuals();
    this->refreshVisualization();
    this->Superclass::reset();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::pushCurveToProxy()
{
    vtkSMProxyProperty* pp =
        vtkSMProxyProperty::SafeDownCast(this->proxy()->GetProperty("CurveTransferFunction"));
    if (!pp || pp->GetNumberOfProxies() == 0)
    {
        return;
    }
    vtkSMProxy* pwfProxy = pp->GetProxy(0);
    if (!pwfProxy)
    {
        return;
    }

    const int numPts = this->PiecewiseFunction->GetSize();
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(numPts) * 4);
    for (int i = 0; i < numPts; ++i)
    {
        double node[4];
        this->PiecewiseFunction->GetNodeValue(i, node);
        values.push_back(node[0]);
        values.push_back(node[1]);
        values.push_back(node[2]);
        values.push_back(node[3]);
    }
    vtkSMPropertyHelper(pwfProxy, "Points")
        .Set(values.data(), static_cast<unsigned int>(values.size()));
    pwfProxy->UpdateVTKObjects();
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::pullCurveFromProxy()
{
    vtkSMProxyProperty* pp =
        vtkSMProxyProperty::SafeDownCast(this->proxy()->GetProperty("CurveTransferFunction"));
    if (!pp || pp->GetNumberOfProxies() == 0)
    {
        return;
    }
    vtkSMProxy* pwfProxy = pp->GetProxy(0);
    if (!pwfProxy)
    {
        return;
    }

    pwfProxy->UpdatePropertyInformation();
    vtkSMPropertyHelper helper(pwfProxy, "Points");
    const unsigned int numElems = helper.GetNumberOfElements();

    this->PiecewiseFunction->RemoveAllPoints();
    for (unsigned int i = 0; i + 3 < numElems; i += 4)
    {
        this->PiecewiseFunction->AddPoint(helper.GetAsDouble(i), helper.GetAsDouble(i + 1),
            helper.GetAsDouble(i + 2), helper.GetAsDouble(i + 3));
    }
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::migrateLegacyNormalizedCurve()
{
    if (!this->PiecewiseFunction)
    {
        return;
    }

    const int numPts = this->PiecewiseFunction->GetSize();
    if (numPts < 2)
    {
        return;
    }

    double yMin = std::numeric_limits<double>::max();
    double yMax = -std::numeric_limits<double>::max();
    for (int i = 0; i < numPts; ++i)
    {
        double val[4];
        this->PiecewiseFunction->GetNodeValue(i, val);
        yMin = std::min(yMin, val[1]);
        yMax = std::max(yMax, val[1]);
    }

    constexpr double eps = 1e-6;
    if (yMin < -eps || yMax > 1.0 + eps)
    {
        return;
    }

    const double outMin = this->OutputMinSpin->value();
    const double outMax = this->OutputMaxSpin->value();
    const double outSpan = outMax - outMin;
    if (!(outSpan > 0.0))
    {
        return;
    }

    // If output already lies in [0,1], physical Y and legacy normalized Y coincide.
    // Only remap when output escapes the unit interval (legacy ACM stored Y in [0,1]).
    if (outMin >= -eps && outMax <= 1.0 + eps)
    {
        return;
    }

    for (int i = 0; i < numPts; ++i)
    {
        double val[4];
        this->PiecewiseFunction->GetNodeValue(i, val);
        val[1] = outMin + val[1] * outSpan;
        this->PiecewiseFunction->SetNodeValue(i, val);
    }
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::rescaleCurveX(double oldInMin, double oldInMax)
{
    if (!this->PiecewiseFunction)
    {
        return;
    }

    const double newInMin = this->InputMinSpin->value();
    const double newInMax = this->InputMaxSpin->value();

    double oldInSpan = oldInMax - oldInMin;
    if (std::abs(oldInSpan) < 1e-30)
    {
        oldInSpan = 1.0;
    }

    const int numPts = this->PiecewiseFunction->GetSize();
    if (numPts < 2)
    {
        this->setIdentityCurve();
    }
    else
    {
        struct CP
        {
            double x, y, mid, sharp;
        };
        std::vector<CP> pts(static_cast<std::size_t>(numPts));
        for (int i = 0; i < numPts; ++i)
        {
            double val[4];
            this->PiecewiseFunction->GetNodeValue(i, val);
            const double t = (val[0] - oldInMin) / oldInSpan;
            pts[static_cast<std::size_t>(i)] = {
                newInMin + t * (newInMax - newInMin), val[1], val[2], val[3] };
        }
        this->PiecewiseFunction->RemoveAllPoints();
        for (const auto& cp : pts)
        {
            this->PiecewiseFunction->AddPoint(cp.x, cp.y, cp.mid, cp.sharp);
        }
    }

    this->PrevInMin = newInMin;
    this->PrevInMax = newInMax;
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::rescaleCurveY(double oldOutMin, double oldOutMax)
{
    if (!this->PiecewiseFunction)
    {
        return;
    }

    const double newOutMin = this->OutputMinSpin->value();
    const double newOutMax = this->OutputMaxSpin->value();

    double oldOutSpan = oldOutMax - oldOutMin;
    if (std::abs(oldOutSpan) < 1e-30)
    {
        oldOutSpan = 1.0;
    }

    const int numPts = this->PiecewiseFunction->GetSize();
    if (numPts < 2)
    {
        this->setIdentityCurve();
    }
    else
    {
        struct CP
        {
            double x, y, mid, sharp;
        };
        std::vector<CP> pts(static_cast<std::size_t>(numPts));
        for (int i = 0; i < numPts; ++i)
        {
            double val[4];
            this->PiecewiseFunction->GetNodeValue(i, val);
            const double t = (val[1] - oldOutMin) / oldOutSpan;
            pts[static_cast<std::size_t>(i)] = {
                val[0], newOutMin + t * (newOutMax - newOutMin), val[2], val[3] };
        }
        this->PiecewiseFunction->RemoveAllPoints();
        for (const auto& cp : pts)
        {
            this->PiecewiseFunction->AddPoint(cp.x, cp.y, cp.mid, cp.sharp);
        }
    }

    this->PrevOutMin = newOutMin;
    this->PrevOutMax = newOutMax;
}

//------------------------------------------------------------------------------
void pqArrayCurveMapperPanel::setIdentityCurve()
{
    if (!this->PiecewiseFunction)
    {
        return;
    }
    const double inMin = this->InputMinSpin->value();
    const double inMax = this->InputMaxSpin->value();
    const double outMin = this->OutputMinSpin->value();
    const double outMax = this->OutputMaxSpin->value();
    this->PiecewiseFunction->RemoveAllPoints();
    this->PiecewiseFunction->AddPoint(inMin, outMin);
    this->PiecewiseFunction->AddPoint(inMax, outMax);
    this->PrevInMin = inMin;
    this->PrevInMax = inMax;
    this->PrevOutMin = outMin;
    this->PrevOutMax = outMax;
}
