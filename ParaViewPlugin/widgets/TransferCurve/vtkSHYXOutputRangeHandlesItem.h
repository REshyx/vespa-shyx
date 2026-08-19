#ifndef vtkSHYXOutputRangeHandlesItem_h
#define vtkSHYXOutputRangeHandlesItem_h

#include <vtkPlotRangeHandlesItem.h>

/** Horizontal range handles for min/max output range (Y axis). */
class vtkSHYXOutputRangeHandlesItem : public vtkPlotRangeHandlesItem
{
public:
    static vtkSHYXOutputRangeHandlesItem* New();
    vtkTypeMacro(vtkSHYXOutputRangeHandlesItem, vtkPlotRangeHandlesItem);

    void SetOutputRange(double ymin, double ymax);
    void GetOutputRange(double range[2]);

protected:
    vtkSHYXOutputRangeHandlesItem();
    ~vtkSHYXOutputRangeHandlesItem() override = default;

    void GetBounds(double bounds[4]) override;
    void SetActiveHandlePosition(double position) override;

private:
    vtkSHYXOutputRangeHandlesItem(const vtkSHYXOutputRangeHandlesItem&) = delete;
    void operator=(const vtkSHYXOutputRangeHandlesItem&) = delete;
};

#endif
