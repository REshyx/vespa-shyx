#ifndef vtkSHYXGrayHistogramPiecewiseFunctionItem_h
#define vtkSHYXGrayHistogramPiecewiseFunctionItem_h

#include "vtkPiecewiseFunctionItem.h"

class vtkContext2D;
class vtkTable;

/** Piecewise-function chart item with neutral gray histogram bars and physical Y values. */
class vtkSHYXGrayHistogramPiecewiseFunctionItem : public vtkPiecewiseFunctionItem
{
public:
    static vtkSHYXGrayHistogramPiecewiseFunctionItem* New();
    vtkTypeMacro(vtkSHYXGrayHistogramPiecewiseFunctionItem, vtkPiecewiseFunctionItem);

    void SetMappedHistogramTable(vtkTable* table);
    vtkGetObjectMacro(MappedHistogramTable, vtkTable);

    vtkSetMacro(HistogramHeightFraction, double);
    vtkGetMacro(HistogramHeightFraction, double);

protected:
    vtkSHYXGrayHistogramPiecewiseFunctionItem() = default;
    ~vtkSHYXGrayHistogramPiecewiseFunctionItem() override;

    bool Paint(vtkContext2D* painter) override;
    bool ConfigurePlotBar() override;
    void ComputeTexture() override;

private:
    void PaintInputHistogram(vtkContext2D* painter);
    void PaintMappedHistogram(vtkContext2D* painter);

    vtkTable* MappedHistogramTable = nullptr;
    double HistogramHeightFraction = 0.60;

    vtkSHYXGrayHistogramPiecewiseFunctionItem(
        const vtkSHYXGrayHistogramPiecewiseFunctionItem&) = delete;
    void operator=(const vtkSHYXGrayHistogramPiecewiseFunctionItem&) = delete;
};

#endif
