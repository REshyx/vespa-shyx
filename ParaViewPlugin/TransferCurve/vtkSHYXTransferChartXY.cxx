#include "vtkSHYXTransferChartXY.h"

#include <vtkAxis.h>
#include <vtkContext2D.h>
#include <vtkContextMouseEvent.h>
#include <vtkObjectFactory.h>
#include <vtkSMCoreUtilities.h>

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkSHYXTransferChartXY);

//------------------------------------------------------------------------------
vtkSHYXTransferChartXY::vtkSHYXTransferChartXY()
{
    this->XRange = vtkVector2d(0.0, 0.0);
    this->YRange = vtkVector2d(0.0, 1.0);
    this->NeedUpdate = false;
    this->DataValid = false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::IsDataRangeValid(const double r[2]) const
{
    double mr[2] = { r[0], r[1] };
    return r[1] < r[0] ? false : (vtkSMCoreUtilities::AdjustRange(mr) == false);
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::SetTFRange(const vtkVector2d& range)
{
    if (range != this->XRange)
    {
        this->XRange = range;
        this->NeedUpdate = true;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::SetOutputRange(const vtkVector2d& range)
{
    if (range != this->YRange)
    {
        this->YRange = range;
        this->NeedUpdate = true;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
void vtkSHYXTransferChartXY::Update()
{
    this->Superclass::Update();
    if (this->NeedUpdate)
    {
        this->DataValid = this->IsDataRangeValid(this->XRange.GetData()) &&
            this->IsDataRangeValid(this->YRange.GetData());
        this->AdjustAxes();
        this->NeedUpdate = false;
    }
}

//------------------------------------------------------------------------------
void vtkSHYXTransferChartXY::AdjustAxes()
{
    this->GetAxis(vtkAxis::BOTTOM)->SetUnscaledRange(this->XRange[0], this->XRange[1]);
    this->GetAxis(vtkAxis::LEFT)->SetRange(this->YRange[0], this->YRange[1]);
    this->RecalculatePlotTransforms();
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::PaintChildren(vtkContext2D* painter)
{
    if (this->DataValid)
    {
        return this->Superclass::PaintChildren(painter);
    }
    painter->DrawString(5, 5, "Data range too small to render.");
    return true;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::MouseEnterEvent(const vtkContextMouseEvent& mouse)
{
    return this->DataValid ? this->Superclass::MouseEnterEvent(mouse) : false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::MouseMoveEvent(const vtkContextMouseEvent& mouse)
{
    return this->DataValid ? this->Superclass::MouseMoveEvent(mouse) : false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::MouseLeaveEvent(const vtkContextMouseEvent& mouse)
{
    return this->DataValid ? this->Superclass::MouseLeaveEvent(mouse) : false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::MouseButtonPressEvent(const vtkContextMouseEvent& mouse)
{
    return this->DataValid ? this->Superclass::MouseButtonPressEvent(mouse) : false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::MouseButtonReleaseEvent(const vtkContextMouseEvent& mouse)
{
    return this->DataValid ? this->Superclass::MouseButtonReleaseEvent(mouse) : false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::MouseWheelEvent(const vtkContextMouseEvent& mouse, int delta)
{
    return this->DataValid ? this->Superclass::MouseWheelEvent(mouse, delta) : false;
}

//------------------------------------------------------------------------------
bool vtkSHYXTransferChartXY::KeyPressEvent(const vtkContextKeyEvent& key)
{
    return this->DataValid ? this->Superclass::KeyPressEvent(key) : false;
}
