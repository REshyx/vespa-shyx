#ifndef pqArrayCurveMapperPanel_h
#define pqArrayCurveMapperPanel_h

#include "pqPropertyWidget.h"
#include <vector>

class QDoubleSpinBox;
class QLabel;
class vtkPiecewiseFunction;
class pqTransferFunctionWidget;

class pqArrayCurveMapperPanel : public pqPropertyWidget
{
    Q_OBJECT
    typedef pqPropertyWidget Superclass;

public:
    pqArrayCurveMapperPanel(vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqArrayCurveMapperPanel() override;

    void apply() override;
    void reset() override;

private Q_SLOTS:
    void onCurveModified();
    void onInputRangeChanged();
    void onOutputRangeChanged();
    void onRefreshInputRange();
    void onRefreshOutputRange();
    void onDetailButtonClicked();

private:
    void pushCurveToProxy();
    void pullCurveFromProxy();
    void rescaleCurveX(double oldInMin, double oldInMax);
    void updateAxisLabels();
    std::vector<double> computeOutputHistogram(int numBins) const;

    QDoubleSpinBox* InputMinSpin  = nullptr;
    QDoubleSpinBox* InputMaxSpin  = nullptr;
    QDoubleSpinBox* OutputMinSpin = nullptr;
    QDoubleSpinBox* OutputMaxSpin = nullptr;

    vtkPiecewiseFunction*      PiecewiseFunction = nullptr;
    pqTransferFunctionWidget*  CurveWidget       = nullptr;
    QLabel*                    XMinLabel         = nullptr;
    QLabel*                    XMaxLabel         = nullptr;
    QLabel*                    InfoLabel         = nullptr;

    double PrevInMin = 0.0;
    double PrevInMax = 1.0;
    bool   UpdatingUI = false;
};

#endif
