#ifndef vtkSHYXTransferChartXY_h
#define vtkSHYXTransferChartXY_h

#include <vtkChartXY.h>
#include <vtkVector.h>

/** Chart with independent X (input) and Y (output) axis ranges. */
class vtkSHYXTransferChartXY : public vtkChartXY
{
public:
    static vtkSHYXTransferChartXY* New();
    vtkTypeMacro(vtkSHYXTransferChartXY, vtkChartXY);

    bool SetTFRange(const vtkVector2d& range);
    bool SetOutputRange(const vtkVector2d& range);

    void Update() override;
    bool PaintChildren(vtkContext2D* painter) override;

    bool MouseEnterEvent(const vtkContextMouseEvent& mouse) override;
    bool MouseMoveEvent(const vtkContextMouseEvent& mouse) override;
    bool MouseLeaveEvent(const vtkContextMouseEvent& mouse) override;
    bool MouseButtonPressEvent(const vtkContextMouseEvent& mouse) override;
    bool MouseButtonReleaseEvent(const vtkContextMouseEvent& mouse) override;
    bool MouseWheelEvent(const vtkContextMouseEvent& mouse, int delta) override;
    bool KeyPressEvent(const vtkContextKeyEvent& key) override;

protected:
    vtkSHYXTransferChartXY();
    ~vtkSHYXTransferChartXY() override = default;

    void AdjustAxes();

    vtkVector2d XRange;
    vtkVector2d YRange;
    bool DataValid;
    bool NeedUpdate;

    bool IsDataRangeValid(const double r[2]) const;

private:
    vtkSHYXTransferChartXY(const vtkSHYXTransferChartXY&) = delete;
    void operator=(const vtkSHYXTransferChartXY&) = delete;
};

#endif
