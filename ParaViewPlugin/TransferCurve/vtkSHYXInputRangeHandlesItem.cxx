#include "vtkSHYXInputRangeHandlesItem.h"

#include <vtkAxis.h>
#include <vtkColorTransferFunction.h>
#include <vtkObjectFactory.h>

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkSHYXInputRangeHandlesItem);

//------------------------------------------------------------------------------
vtkSHYXInputRangeHandlesItem::vtkSHYXInputRangeHandlesItem()
{
    this->ExtentToAxisRangeOn();
}

//------------------------------------------------------------------------------
void vtkSHYXInputRangeHandlesItem::GetBounds(double* bounds)
{
    vtkColorTransferFunction* ctf = this->GetColorTransferFunction();
    if (!ctf)
    {
        vtkErrorMacro("vtkSHYXInputRangeHandlesItem requires a ColorTransferFunction");
        return;
    }

    double tfRange[2];
    ctf->GetRange(tfRange);

    double yRange[2] = { 0.0, 1.0 };
    if (this->GetYAxis())
    {
        this->GetYAxis()->GetUnscaledRange(yRange);
    }

    this->TransformDataToScreen(tfRange[0], yRange[0], bounds[0], bounds[2]);
    this->TransformDataToScreen(tfRange[1], yRange[1], bounds[1], bounds[3]);
}