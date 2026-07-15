#ifndef vtkSHYXInputRangeHandlesItem_h
#define vtkSHYXInputRangeHandlesItem_h

#include <vtkRangeHandlesItem.h>

/** Range handles spanning the chart Y axis (output units), not [0,1]. */
class vtkSHYXInputRangeHandlesItem : public vtkRangeHandlesItem
{
public:
    static vtkSHYXInputRangeHandlesItem* New();
    vtkTypeMacro(vtkSHYXInputRangeHandlesItem, vtkRangeHandlesItem);

    void GetBounds(double bounds[4]) override;

protected:
    vtkSHYXInputRangeHandlesItem();
    ~vtkSHYXInputRangeHandlesItem() override = default;

private:
    vtkSHYXInputRangeHandlesItem(const vtkSHYXInputRangeHandlesItem&) = delete;
    void operator=(const vtkSHYXInputRangeHandlesItem&) = delete;
};

#endif
