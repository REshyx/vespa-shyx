#include "pqArrayCurveMapperPanel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "pqTransferFunctionWidget.h"

#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkAlgorithm.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPointData.h>
#include <vtkPVDataInformation.h>
#include <vtkPVArrayInformation.h>
#include <vtkPVDataSetAttributesInformation.h>
#include <vtkSMDoubleVectorProperty.h>
#include <vtkSMInputProperty.h>
#include <vtkSMProperty.h>
#include <vtkSMProxy.h>
#include <vtkSMProxyProperty.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMSourceProxy.h>
#include <vtkSMStringVectorProperty.h>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
static QDoubleSpinBox* pqArrayCurveMapperPanel_createRangeSpinBox(QWidget* parent)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(6);
    spin->setRange(-1e20, 1e20);
    spin->setSingleStep(0.01);
    spin->setMinimumWidth(80);
    return spin;
}

// ---------------------------------------------------------------------------
class HistogramWidget : public QWidget
{
public:
    explicit HistogramWidget(QWidget* parent = nullptr) : QWidget(parent) {}

    void setData(const std::vector<double>& bins, double rangeMin, double rangeMax)
    {
        Bins = bins;
        RangeMin = rangeMin;
        RangeMax = rangeMax;
        MaxVal = 0.0;
        for (auto v : Bins)
            MaxVal = std::max(MaxVal, v);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const int w = width();
        const int h = height();
        const int n = static_cast<int>(Bins.size());
        if (n == 0 || MaxVal <= 0.0)
        {
            p.fillRect(rect(), QColor(245, 245, 245));
            p.setPen(QColor(180, 180, 180));
            p.drawText(rect(), Qt::AlignCenter, "No data");
            return;
        }

        p.fillRect(rect(), QColor(250, 250, 250));

        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        QFontMetrics fm(f);
        QString maxValStr = QString::number(static_cast<int>(MaxVal));
        const int yLabelGap = -10;
        const int margin = 4 + fm.height();
        const int plotL = margin;
        const int plotR = w - 10;
        const int plotT = 10;
        const int plotB = h - 25;
        const int plotW = plotR - plotL;
        const int plotH = plotB - plotT;
        if (plotW < 10 || plotH < 10)
            return;

        const double barW = static_cast<double>(plotW) / n;
        const QColor barColor(160, 160, 160);

        for (int i = 0; i < n; ++i)
        {
            double frac = Bins[i] / MaxVal;
            int barH = static_cast<int>(frac * plotH);
            int x = plotL + static_cast<int>(i * barW);
            int bw = std::max(1, static_cast<int>(barW) - 1);
            QRect bar(x, plotB - barH, bw, barH);
            p.fillRect(bar, barColor);
        }

        p.setPen(QColor(80, 80, 80));
        p.drawLine(plotL, plotB, plotR, plotB);
        p.drawLine(plotL, plotT, plotL, plotB);

        p.drawText(plotL, plotB + 14, QString::number(RangeMin, 'g', 4));
        QString maxStr = QString::number(RangeMax, 'g', 4);
        p.drawText(plotR - fm.horizontalAdvance(maxStr), plotB + 14, maxStr);

        auto drawRotatedLabel = [&p, &fm, plotL, yLabelGap](int y, const QString& text) {
            p.save();
            p.translate(plotL - yLabelGap - fm.height(), y);
            p.rotate(-90);
            p.drawText(0, 0, text);
            p.restore();
        };
        drawRotatedLabel(plotT + fm.horizontalAdvance(maxValStr), maxValStr);
        drawRotatedLabel(plotB - 2, QStringLiteral("0"));
    }

private:
    std::vector<double> Bins;
    double RangeMin = 0.0;
    double RangeMax = 1.0;
    double MaxVal   = 0.0;
};

// ---------------------------------------------------------------------------
pqArrayCurveMapperPanel::pqArrayCurveMapperPanel(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
    : Superclass(smproxy, parentObject)
{
    Q_UNUSED(smgroup);

    this->PiecewiseFunction = vtkPiecewiseFunction::New();

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(6);

    // ---- Input Range row ----
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);

        auto* label = new QLabel(tr("Input Range"), this);
        label->setMinimumWidth(80);
        row->addWidget(label);

        InputMinSpin = pqArrayCurveMapperPanel_createRangeSpinBox(this);
        row->addWidget(InputMinSpin);

        auto* tilde = new QLabel(QStringLiteral("~"), this);
        tilde->setFixedWidth(12);
        tilde->setAlignment(Qt::AlignCenter);
        row->addWidget(tilde);

        InputMaxSpin = pqArrayCurveMapperPanel_createRangeSpinBox(this);
        row->addWidget(InputMaxSpin);

        auto* refreshBtn = new QPushButton(QStringLiteral("\u21BB"), this);
        refreshBtn->setFixedWidth(30);
        refreshBtn->setToolTip(tr("Refresh to input array data range"));
        connect(refreshBtn, &QPushButton::clicked, this, &pqArrayCurveMapperPanel::onRefreshInputRange);
        row->addWidget(refreshBtn);

        vbox->addLayout(row);
    }

    // ---- Output Range row ----
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);

        auto* label = new QLabel(tr("Output Range"), this);
        label->setMinimumWidth(80);
        row->addWidget(label);

        OutputMinSpin = pqArrayCurveMapperPanel_createRangeSpinBox(this);
        row->addWidget(OutputMinSpin);

        auto* tilde = new QLabel(QStringLiteral("~"), this);
        tilde->setFixedWidth(12);
        tilde->setAlignment(Qt::AlignCenter);
        row->addWidget(tilde);

        OutputMaxSpin = pqArrayCurveMapperPanel_createRangeSpinBox(this);
        row->addWidget(OutputMaxSpin);

        auto* refreshBtn = new QPushButton(QStringLiteral("\u21BB"), this);
        refreshBtn->setFixedWidth(30);
        refreshBtn->setToolTip(tr("Reset output range to 0 ~ 1"));
        connect(refreshBtn, &QPushButton::clicked, this, &pqArrayCurveMapperPanel::onRefreshOutputRange);
        row->addWidget(refreshBtn);

        vbox->addLayout(row);
    }

    // ---- Transfer Curve with X axis labels ----
    {
        const QString labelStyle = QStringLiteral("color: #555; font-size: 10px;");

        CurveWidget = new pqTransferFunctionWidget(this);
        CurveWidget->setMinimumHeight(200);
        CurveWidget->setMaximumHeight(400);
        vbox->addWidget(CurveWidget);

        auto* xLabelRow = new QHBoxLayout;
        xLabelRow->setContentsMargins(0, 0, 0, 0);
        xLabelRow->setSpacing(0);

        XMinLabel = new QLabel(this);
        XMinLabel->setAlignment(Qt::AlignLeft);
        XMinLabel->setStyleSheet(labelStyle);
        xLabelRow->addWidget(XMinLabel);

        xLabelRow->addStretch(1);

        XMaxLabel = new QLabel(this);
        XMaxLabel->setAlignment(Qt::AlignRight);
        XMaxLabel->setStyleSheet(labelStyle);
        xLabelRow->addWidget(XMaxLabel);

        vbox->addLayout(xLabelRow);
    }

    // ---- Info label + Detail button ----
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);

        InfoLabel = new QLabel(this);
        InfoLabel->setText(tr("Drag points to shape the curve"));
        InfoLabel->setStyleSheet("color: gray; font-size: 11px;");
        row->addWidget(InfoLabel, 1);

        auto* detailBtn = new QPushButton(tr("Detail"), this);
        detailBtn->setToolTip(tr("Open curve editor with output distribution histogram"));
        connect(detailBtn, &QPushButton::clicked, this, &pqArrayCurveMapperPanel::onDetailButtonClicked);
        row->addWidget(detailBtn);

        vbox->addLayout(row);
    }

    // ---- Read initial values ----
    auto* inMinProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("InputRangeMin"));
    auto* inMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("InputRangeMax"));
    auto* outMinProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("OutputRangeMin"));
    auto* outMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(smproxy->GetProperty("OutputRangeMax"));

    if (inMinProp)
        InputMinSpin->setValue(inMinProp->GetElement(0));
    if (inMaxProp)
        InputMaxSpin->setValue(inMaxProp->GetElement(0));
    if (outMinProp)
        OutputMinSpin->setValue(outMinProp->GetElement(0));
    if (outMaxProp)
        OutputMaxSpin->setValue(outMaxProp->GetElement(0));

    PrevInMin = InputMinSpin->value();
    PrevInMax = InputMaxSpin->value();

    pullCurveFromProxy();

    if (this->PiecewiseFunction->GetSize() < 2)
    {
        this->PiecewiseFunction->RemoveAllPoints();
        this->PiecewiseFunction->AddPoint(PrevInMin, 0.0);
        this->PiecewiseFunction->AddPoint(PrevInMax, 1.0);
    }

    CurveWidget->initialize(nullptr, false, this->PiecewiseFunction, true);
    updateAxisLabels();

    connect(InputMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &pqArrayCurveMapperPanel::onInputRangeChanged);
    connect(InputMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &pqArrayCurveMapperPanel::onInputRangeChanged);
    connect(OutputMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &pqArrayCurveMapperPanel::onOutputRangeChanged);
    connect(OutputMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &pqArrayCurveMapperPanel::onOutputRangeChanged);
    connect(CurveWidget, SIGNAL(controlPointsModified()), this, SLOT(onCurveModified()));
}

// ---------------------------------------------------------------------------
pqArrayCurveMapperPanel::~pqArrayCurveMapperPanel()
{
    if (this->PiecewiseFunction)
    {
        this->PiecewiseFunction->Delete();
        this->PiecewiseFunction = nullptr;
    }
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onCurveModified()
{
    this->pushCurveToProxy();
    emit changeAvailable();
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onInputRangeChanged()
{
    if (UpdatingUI)
        return;

    auto* inMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin"));
    auto* inMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax"));
    if (inMinProp)
        inMinProp->SetElement(0, InputMinSpin->value());
    if (inMaxProp)
        inMaxProp->SetElement(0, InputMaxSpin->value());

    rescaleCurveX(PrevInMin, PrevInMax);
    updateAxisLabels();
    emit changeAvailable();
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onOutputRangeChanged()
{
    if (UpdatingUI)
        return;

    auto* outMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin"));
    auto* outMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax"));
    if (outMinProp)
        outMinProp->SetElement(0, OutputMinSpin->value());
    if (outMaxProp)
        outMaxProp->SetElement(0, OutputMaxSpin->value());

    emit changeAvailable();
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onRefreshInputRange()
{
    auto* inputProp = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("Input"));
    auto* arrayNameProp = vtkSMStringVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputArrayName"));

    if (!inputProp || inputProp->GetNumberOfProxies() == 0 || !arrayNameProp)
        return;

    auto* sourceProxy = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
    if (!sourceProxy)
        return;

    sourceProxy->UpdatePipeline();
    auto* dataInfo = sourceProxy->GetDataInformation();
    if (!dataInfo)
        return;

    const char* arrayName = arrayNameProp->GetElement(0);
    if (!arrayName || arrayName[0] == '\0')
        return;

    auto* pointInfo = dataInfo->GetPointDataInformation();
    if (!pointInfo)
        return;

    auto* arrayInfo = pointInfo->GetArrayInformation(arrayName);
    if (!arrayInfo)
        return;

    double range[2];
    int numComp = arrayInfo->GetNumberOfComponents();
    if (numComp > 1)
        arrayInfo->GetComponentRange(-1, range);
    else
        arrayInfo->GetComponentRange(0, range);

    double oldInMin = PrevInMin, oldInMax = PrevInMax;

    UpdatingUI = true;
    InputMinSpin->setValue(range[0]);
    InputMaxSpin->setValue(range[1]);
    UpdatingUI = false;

    auto* inMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin"));
    auto* inMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax"));
    if (inMinProp)
        inMinProp->SetElement(0, range[0]);
    if (inMaxProp)
        inMaxProp->SetElement(0, range[1]);

    rescaleCurveX(oldInMin, oldInMax);
    updateAxisLabels();
    emit changeAvailable();
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onRefreshOutputRange()
{
    UpdatingUI = true;
    OutputMinSpin->setValue(0.0);
    OutputMaxSpin->setValue(1.0);
    UpdatingUI = false;

    auto* outMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin"));
    auto* outMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax"));
    if (outMinProp)
        outMinProp->SetElement(0, 0.0);
    if (outMaxProp)
        outMaxProp->SetElement(0, 1.0);

    emit changeAvailable();
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::onDetailButtonClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Transfer Curve — Detail"));
    dialog.setMinimumWidth(700);

    auto* topLayout = new QVBoxLayout(&dialog);
    topLayout->setSpacing(12);
    topLayout->setContentsMargins(12, 12, 12, 12);

    auto* hbox = new QHBoxLayout;
    hbox->setSpacing(16);

    const QString sectionTitleStyle = QStringLiteral("font-weight: bold; font-size: 13px; padding: 2px 0;");
    const int headerHeight = 24;

    auto* curveFrame = new QWidget(&dialog);
    auto* curveBox = new QVBoxLayout(curveFrame);
    curveBox->setContentsMargins(0, 0, 0, 0);
    curveBox->setSpacing(6);
    auto* curveLabel = new QLabel(tr("Transfer Curve"), curveFrame);
    curveLabel->setStyleSheet(sectionTitleStyle);
    curveLabel->setFixedHeight(headerHeight);
    curveLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    curveBox->addWidget(curveLabel);

    auto* bigCurve = new pqTransferFunctionWidget(curveFrame);
    bigCurve->setMinimumSize(280, 280);
    bigCurve->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    bigCurve->initialize(nullptr, false, this->PiecewiseFunction, true);
    curveBox->addWidget(bigCurve, 1);
    hbox->addWidget(curveFrame, 1);

    auto* histFrame = new QWidget(&dialog);
    auto* histBox = new QVBoxLayout(histFrame);
    histBox->setContentsMargins(0, 0, 0, 0);
    histBox->setSpacing(6);

    auto* histTitleRow = new QHBoxLayout;
    histTitleRow->setContentsMargins(0, 0, 0, 0);
    histTitleRow->setSpacing(8);
    auto* histLabel = new QLabel(tr("Output Distribution"), histFrame);
    histLabel->setStyleSheet(sectionTitleStyle);
    histLabel->setFixedHeight(headerHeight);
    histLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    histTitleRow->addWidget(histLabel, 1, Qt::AlignVCenter);

    auto* refreshHistBtn = new QPushButton(QStringLiteral("\u21BB Refresh"), histFrame);
    refreshHistBtn->setToolTip(tr("Recompute output distribution with current curve"));
    refreshHistBtn->setFixedHeight(headerHeight);
    histTitleRow->addWidget(refreshHistBtn, 0, Qt::AlignVCenter);
    histBox->addLayout(histTitleRow);

    auto* histWidget = new HistogramWidget(histFrame);
    histWidget->setMinimumSize(280, 280);
    histWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    histBox->addWidget(histWidget, 1);
    hbox->addWidget(histFrame, 1);

    topLayout->addLayout(hbox, 1);

    double outMin = OutputMinSpin->value();
    double outMax = OutputMaxSpin->value();
    auto histBins = computeOutputHistogram(32);
    histWidget->setData(histBins, outMin, outMax);

    connect(refreshHistBtn, &QPushButton::clicked, [this, histWidget]() {
        auto bins = computeOutputHistogram(32);
        histWidget->setData(bins, OutputMinSpin->value(), OutputMaxSpin->value());
    });

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(btnBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    topLayout->addWidget(btnBox);

    dialog.adjustSize();
    dialog.resize(static_cast<int>(dialog.width() * 1.5), static_cast<int>(dialog.height() * 1.2));

    if (dialog.exec() == QDialog::Accepted)
    {
        CurveWidget->initialize(nullptr, false, this->PiecewiseFunction, true);
        emit changeAvailable();
    }
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::rescaleCurveX(double oldInMin, double oldInMax)
{
    if (!PiecewiseFunction)
        return;

    double newInMin = InputMinSpin->value();
    double newInMax = InputMaxSpin->value();

    double oldInSpan = oldInMax - oldInMin;
    if (std::abs(oldInSpan) < 1e-30)
        oldInSpan = 1.0;

    double newInSpan = newInMax - newInMin;

    int numPts = PiecewiseFunction->GetSize();
    if (numPts < 2)
    {
        PiecewiseFunction->RemoveAllPoints();
        PiecewiseFunction->AddPoint(newInMin, 0.0);
        PiecewiseFunction->AddPoint(newInMax, 1.0);
    }
    else
    {
        struct CP
        {
            double x, y, mid, sharp;
        };
        std::vector<CP> pts(numPts);
        for (int i = 0; i < numPts; ++i)
        {
            double val[4];
            PiecewiseFunction->GetNodeValue(i, val);
            double t = (val[0] - oldInMin) / oldInSpan;
            pts[i] = { newInMin + t * newInSpan, val[1], val[2], val[3] };
        }

        PiecewiseFunction->RemoveAllPoints();
        for (auto& cp : pts)
            PiecewiseFunction->AddPoint(cp.x, cp.y, cp.mid, cp.sharp);
    }

    PrevInMin = newInMin;
    PrevInMax = newInMax;

    CurveWidget->initialize(nullptr, false, this->PiecewiseFunction, true);
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::updateAxisLabels()
{
    XMinLabel->setText(QString::number(InputMinSpin->value(), 'g', 6));
    XMaxLabel->setText(QString::number(InputMaxSpin->value(), 'g', 6));
}

// ---------------------------------------------------------------------------
std::vector<double> pqArrayCurveMapperPanel::computeOutputHistogram(int numBins) const
{
    std::vector<double> bins(numBins, 0.0);

    if (!PiecewiseFunction || PiecewiseFunction->GetSize() < 2)
        return bins;

    auto* inputProp = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("Input"));
    auto* arrayNameProp = vtkSMStringVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputArrayName"));

    if (!inputProp || inputProp->GetNumberOfProxies() == 0 || !arrayNameProp)
        return bins;

    auto* sourceProxy = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
    if (!sourceProxy)
        return bins;

    sourceProxy->UpdatePipeline();

    auto* clientObj = sourceProxy->GetClientSideObject();
    auto* algo = vtkAlgorithm::SafeDownCast(clientObj);
    if (!algo)
        return bins;

    auto* ds = vtkDataSet::SafeDownCast(algo->GetOutputDataObject(0));
    if (!ds)
        return bins;

    const char* arrayName = arrayNameProp->GetElement(0);
    if (!arrayName || arrayName[0] == '\0')
        return bins;

    auto* arr = ds->GetPointData()->GetArray(arrayName);
    if (!arr)
        return bins;

    double inMin = InputMinSpin->value();
    double inMax = InputMaxSpin->value();
    double outMin = OutputMinSpin->value();
    double outMax = OutputMaxSpin->value();
    double outSpan = outMax - outMin;
    if (std::abs(outSpan) < 1e-30)
        return bins;

    int numComp = arr->GetNumberOfComponents();
    vtkIdType numTuples = arr->GetNumberOfTuples();
    std::vector<double> tuple(numComp);

    for (vtkIdType i = 0; i < numTuples; ++i)
    {
        double raw;
        if (numComp > 1)
        {
            arr->GetTuple(i, tuple.data());
            double sumSq = 0.0;
            for (int c = 0; c < numComp; ++c)
                sumSq += tuple[c] * tuple[c];
            raw = std::sqrt(sumSq);
        }
        else
        {
            raw = arr->GetTuple1(i);
        }

        raw = std::max(inMin, std::min(inMax, raw));
        double t = PiecewiseFunction->GetValue(raw);
        double mapped = outMin + t * outSpan;

        int idx = static_cast<int>((mapped - outMin) / outSpan * numBins);
        idx = std::max(0, std::min(numBins - 1, idx));
        bins[idx] += 1.0;
    }

    return bins;
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::apply()
{
    this->pushCurveToProxy();

    auto* inMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin"));
    auto* inMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax"));
    auto* outMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin"));
    auto* outMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax"));
    if (inMinProp)
        inMinProp->SetElement(0, InputMinSpin->value());
    if (inMaxProp)
        inMaxProp->SetElement(0, InputMaxSpin->value());
    if (outMinProp)
        outMinProp->SetElement(0, OutputMinSpin->value());
    if (outMaxProp)
        outMaxProp->SetElement(0, OutputMaxSpin->value());

    this->Superclass::apply();
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::reset()
{
    auto* inMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMin"));
    auto* inMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InputRangeMax"));
    auto* outMinProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMin"));
    auto* outMaxProp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("OutputRangeMax"));

    UpdatingUI = true;
    if (inMinProp)
        InputMinSpin->setValue(inMinProp->GetElement(0));
    if (inMaxProp)
        InputMaxSpin->setValue(inMaxProp->GetElement(0));
    if (outMinProp)
        OutputMinSpin->setValue(outMinProp->GetElement(0));
    if (outMaxProp)
        OutputMaxSpin->setValue(outMaxProp->GetElement(0));
    UpdatingUI = false;

    updateAxisLabels();

    this->pullCurveFromProxy();

    if (PiecewiseFunction && PiecewiseFunction->GetSize() < 2)
    {
        PiecewiseFunction->RemoveAllPoints();
        PiecewiseFunction->AddPoint(InputMinSpin->value(), 0.0);
        PiecewiseFunction->AddPoint(InputMaxSpin->value(), 1.0);
    }

    PrevInMin = InputMinSpin->value();
    PrevInMax = InputMaxSpin->value();

    CurveWidget->initialize(nullptr, false, this->PiecewiseFunction, true);
    this->Superclass::reset();
}

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::pushCurveToProxy()
{
    vtkSMProxy* smproxy = this->proxy();
    vtkSMProxyProperty* pp =
        vtkSMProxyProperty::SafeDownCast(smproxy->GetProperty("CurveTransferFunction"));
    if (!pp || pp->GetNumberOfProxies() == 0)
    {
        return;
    }

    vtkSMProxy* pwfProxy = pp->GetProxy(0);
    if (!pwfProxy)
    {
        return;
    }

    int numPts = this->PiecewiseFunction->GetSize();
    std::vector<double> values;
    values.reserve(static_cast<size_t>(numPts) * 4);

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

// ---------------------------------------------------------------------------
void pqArrayCurveMapperPanel::pullCurveFromProxy()
{
    vtkSMProxy* smproxy = this->proxy();
    vtkSMProxyProperty* pp =
        vtkSMProxyProperty::SafeDownCast(smproxy->GetProperty("CurveTransferFunction"));
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
    unsigned int numElems = helper.GetNumberOfElements();

    this->PiecewiseFunction->RemoveAllPoints();

    for (unsigned int i = 0; i + 3 < numElems; i += 4)
    {
        double x = helper.GetAsDouble(i);
        double y = helper.GetAsDouble(i + 1);
        double midpoint = helper.GetAsDouble(i + 2);
        double sharpness = helper.GetAsDouble(i + 3);
        this->PiecewiseFunction->AddPoint(x, y, midpoint, sharpness);
    }

    if (this->PiecewiseFunction->GetSize() < 2)
    {
        double inMin = InputMinSpin ? InputMinSpin->value() : 0.0;
        double inMax = InputMaxSpin ? InputMaxSpin->value() : 1.0;
        this->PiecewiseFunction->AddPoint(inMin, 0.0);
        this->PiecewiseFunction->AddPoint(inMax, 1.0);
    }
}
