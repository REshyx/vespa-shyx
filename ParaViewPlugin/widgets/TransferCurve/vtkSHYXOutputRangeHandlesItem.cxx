#include "vtkSHYXOutputRangeHandlesItem.h"

#include <vtkAxis.h>
#include <vtkObjectFactory.h>

namespace
{
/** Screen past-edge drag maps to a fraction of the current data span per chart height. */
constexpr double kOutwardExpansionRate = 0.1;
}

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkSHYXOutputRangeHandlesItem);

//------------------------------------------------------------------------------
vtkSHYXOutputRangeHandlesItem::vtkSHYXOutputRangeHandlesItem()
{
    this->SetHandleOrientationToHorizontal();
    this->ExtentToAxisRangeOn();
    this->SetHandleWidth(2.0f);
    this->LockTooltipToMouseOff();
}

//------------------------------------------------------------------------------
void vtkSHYXOutputRangeHandlesItem::SetOutputRange(double ymin, double ymax)
{
    if (!(ymax > ymin))
    {
        return;
    }
    this->Extent[0] = ymin;
    this->Extent[1] = ymax;
    this->Modified();
}

//------------------------------------------------------------------------------
void vtkSHYXOutputRangeHandlesItem::GetOutputRange(double range[2])
{
    range[0] = this->Extent[0];
    range[1] = this->Extent[1];
}

//------------------------------------------------------------------------------
void vtkSHYXOutputRangeHandlesItem::GetBounds(double bounds[4])
{
    double yRange[2] = { 0.0, 1.0 };
    double xRange[2] = { 0.0, 1.0 };
    if (this->GetYAxis())
    {
        this->GetYAxis()->GetUnscaledRange(yRange);
    }
    if (this->GetXAxis())
    {
        this->GetXAxis()->GetUnscaledRange(xRange);
    }

    this->TransformDataToScreen(yRange[0], xRange[0], bounds[0], bounds[2]);
    this->TransformDataToScreen(yRange[1], xRange[1], bounds[1], bounds[3]);
}

//------------------------------------------------------------------------------
void vtkSHYXOutputRangeHandlesItem::SetActiveHandlePosition(double position)
{
    if (this->ActiveHandle == vtkPlotRangeHandlesItem::NO_HANDLE)
    {
        return;
    }

    double bounds[4];
    this->GetBounds(bounds);

    const double minScreen = bounds[0];
    const double maxScreen = bounds[1];
    const double screenSpan = maxScreen - minScreen;

    this->ActiveHandlePosition = position;

    double screenPos = position;
    if (this->ActiveHandle == vtkPlotRangeHandlesItem::LEFT_HANDLE)
    {
        screenPos -= this->HandleDelta;
    }
    else
    {
        screenPos += this->HandleDelta;
    }

    double yRange[2] = { this->Extent[0], this->Extent[1] };
    if (this->GetYAxis())
    {
        this->GetYAxis()->GetUnscaledRange(yRange);
    }
    const double dataSpan = yRange[1] - yRange[0];

    if (screenSpan > 0.0)
    {
        if (this->ActiveHandle == vtkPlotRangeHandlesItem::LEFT_HANDLE && screenPos < minScreen)
        {
            const double past = (minScreen - screenPos) / screenSpan;
            this->ActiveHandleRangeValue =
                yRange[0] - past * dataSpan * kOutwardExpansionRate;
        }
        else if (this->ActiveHandle == vtkPlotRangeHandlesItem::RIGHT_HANDLE && screenPos > maxScreen)
        {
            const double past = (screenPos - maxScreen) / screenSpan;
            this->ActiveHandleRangeValue =
                yRange[1] + past * dataSpan * kOutwardExpansionRate;
        }
        else
        {
            double unused = 0.0;
            this->TransformScreenToData(screenPos, 1.0, this->ActiveHandleRangeValue, unused);
        }
    }
    else
    {
        double unused = 0.0;
        this->TransformScreenToData(screenPos, 1.0, this->ActiveHandleRangeValue, unused);
    }

    if (this->ActiveHandle == vtkPlotRangeHandlesItem::LEFT_HANDLE &&
        this->ActiveHandleRangeValue >= this->Extent[1])
    {
        this->ActiveHandleRangeValue = this->Extent[1];
    }
    if (this->ActiveHandle == vtkPlotRangeHandlesItem::RIGHT_HANDLE &&
        this->ActiveHandleRangeValue <= this->Extent[0])
    {
        this->ActiveHandleRangeValue = this->Extent[0];
    }
}
