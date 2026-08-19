#ifndef pqSHYXTransferCurveWidget_h
#define pqSHYXTransferCurveWidget_h

#include "vtkType.h"

#include <QWidget>

class vtkPiecewiseFunction;
class vtkScalarsToColors;
class vtkTable;

/** Transfer-function editor with Y axis in physical output units. */
class pqSHYXTransferCurveWidget : public QWidget
{
    Q_OBJECT
    typedef QWidget Superclass;

public:
    pqSHYXTransferCurveWidget(QWidget* parent = nullptr);
    ~pqSHYXTransferCurveWidget() override;

    void initialize(
        vtkScalarsToColors* stc, bool stc_editable, vtkPiecewiseFunction* pwf, bool pwf_editable);

    vtkIdType currentPoint() const;
    vtkIdType numberOfControlPoints() const;

    void SetLogScaleXAxis(bool logScale);
    bool GetLogScaleXAxis() const;

    vtkScalarsToColors* scalarsToColors() const;
    vtkPiecewiseFunction* piecewiseFunction() const;

    void SetControlPointsFreehandDrawing(bool use);
    bool GetControlPointsFreehandDrawing() const;

    /** Sets the chart Y axis to [ymin, ymax] (output range min/max). */
    void setOutputRange(double ymin, double ymax);

    /** Clamps draggable control points to the curve input/output box. */
    void setCurveDataBounds(double xmin, double xmax, double ymin, double ymax);

public Q_SLOTS:
    void setCurrentPoint(vtkIdType index);
    void setCurrentPointPosition(double xpos);
    void render();
    void setHistogramTable(vtkTable* table);
    void setMappedHistogramTable(vtkTable* table);
    void setHistogramHeightFraction(double fraction);

Q_SIGNALS:
    void currentPointChanged(vtkIdType index);
    void controlPointsModified();
    void chartRangeModified();
    void rangeHandlesRangeChanged(double rangeMin, double rangeMax);
    void rangeHandlesDoubleClicked();
    void outputRangeHandlesRangeChanged(double rangeMin, double rangeMax);

protected Q_SLOTS:
    void onCurrentChangedEvent();
    void onRangeHandlesRangeChanged();
    void onOutputRangeHandlesRangeChanged();
    void onOutputRangeHandlesInteraction();
    void showUsageStatus();
    void editColorAtCurrentControlPoint();

protected:
    void onCurrentPointEditEvent();

private:
    Q_DISABLE_COPY(pqSHYXTransferCurveWidget)

    class pqInternals;
    pqInternals* Internals;
};

#endif
