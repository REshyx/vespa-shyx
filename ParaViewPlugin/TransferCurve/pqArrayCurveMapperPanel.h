#ifndef pqArrayCurveMapperPanel_h
#define pqArrayCurveMapperPanel_h

#include "pqPropertyWidget.h"

#include <vector>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QWidget;
class pqSHYXTransferCurveWidget;
class vtkColorTransferFunction;
class vtkPiecewiseFunction;

/** Property-group panel: editable transfer curve with live input/output histograms. */
class pqArrayCurveMapperPanel : public pqPropertyWidget
{
    Q_OBJECT
    typedef pqPropertyWidget Superclass;

public:
    pqArrayCurveMapperPanel(vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqArrayCurveMapperPanel() override;

    void apply() override;
    void reset() override;
    void select() override;

private Q_SLOTS:
    void onCurveModified();
    void onInputRangeChanged();
    void onOutputRangeChanged();
    void onRangeHandlesChanged(double rangeMin, double rangeMax);
    void onRangeHandlesDoubleClicked();
    void onOutputRangeHandlesChanged(double rangeMin, double rangeMax);
    void onResetInputRange();
    void onResetOutputRange();
    void onResetCurveClicked();
    void onHistogramHeightChanged();
    void onChartHeightChanged();

private:
    bool fetchInputValues(std::vector<double>& valuesOut, bool forcePipelineUpdate = false) const;
    bool fetchInputArrayRange(double range[2]) const;
    bool applyDefaultRangesIfNeeded(bool forcePipelineUpdate = false);
    void pushCurveToProxy();
    void pullCurveFromProxy();
    void rescaleCurveX(double oldInMin, double oldInMax);
    void rescaleCurveY(double oldOutMin, double oldOutMax);
    void migrateLegacyNormalizedCurve();
    void setIdentityCurve();
    void setupCurveWidgetIfNeeded();
    void refreshVisualization(bool forcePipelineUpdate = false);
    void updateHistogramTable(const std::vector<double>& values);
    void updateMappedHistogramTable(const std::vector<double>& mappedValues);
    void syncClampRangeCTF();
    void syncClampRangeVisuals();
    void syncChartOutputRange();
    void syncHistogramHeightVisuals();
    void syncChartHeightVisuals();

    QSpinBox* ChartHeightSpin = nullptr;
    QDoubleSpinBox* HistogramHeightSpin = nullptr;
    QDoubleSpinBox* InputMinSpin = nullptr;
    QDoubleSpinBox* InputMaxSpin = nullptr;
    QDoubleSpinBox* OutputMinSpin = nullptr;
    QDoubleSpinBox* OutputMaxSpin = nullptr;
    QLabel* StatsLabel = nullptr;
    QLabel* HintLabel = nullptr;
    QWidget* EditorHost = nullptr;
    QWidget* ClampRangeOverlay = nullptr;
    pqSHYXTransferCurveWidget* CurveWidget = nullptr;

    vtkPiecewiseFunction* PiecewiseFunction = nullptr;
    vtkColorTransferFunction* ClampRangeCTF = nullptr;

    bool CurveWidgetInitialized = false;
    bool AutoDefaultsPending = true;
    double PrevInMin = 0.0;
    double PrevInMax = 1.0;
    double PrevOutMin = 0.0;
    double PrevOutMax = 1.0;
    double DataExtentMin = 0.0;
    double DataExtentMax = 1.0;
    bool UpdatingUI = false;
};

#endif
