#include "vtkSHYXCompositeControlPointsItem.h"

#include <vtkAxis.h>
#include <vtkContextMouseEvent.h>
#include <vtkObjectFactory.h>
#include <vtkPiecewiseFunction.h>
#include <vtkVector.h>

#include <algorithm>
#include <limits>

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkSHYXCompositeControlPointsItem);

//------------------------------------------------------------------------------
void vtkSHYXCompositeControlPointsItem::SetCurveDataBounds(
    double xmin, double xmax, double ymin, double ymax)
{
    this->CurveDataBounds[0] = xmin;
    this->CurveDataBounds[1] = xmax;
    this->CurveDataBounds[2] = ymin;
    this->CurveDataBounds[3] = ymax;
    this->UseCurveDataBounds = (xmax > xmin) && (ymax > ymin);
    this->SetValidBounds(xmin, xmax, ymin, ymax);
    // Do not call SetUserBounds: vtkControlPointsItem::GetBounds would return data
    // coordinates and break AddPointItem hit-testing (which expects screen coords).
    this->Modified();
}

//------------------------------------------------------------------------------
bool vtkSHYXCompositeControlPointsItem::IsPosInPlotArea(const double pos[2])
{
    if (this->UseCurveDataBounds)
    {
        return pos[0] >= this->CurveDataBounds[0] && pos[0] <= this->CurveDataBounds[1] &&
            pos[1] >= this->CurveDataBounds[2] && pos[1] <= this->CurveDataBounds[3];
    }

    double xRange[2] = { -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max() };
    double yRange[2] = { -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max() };
    if (this->GetXAxis())
    {
        this->GetXAxis()->GetUnscaledRange(xRange);
    }
    if (this->GetYAxis())
    {
        this->GetYAxis()->GetUnscaledRange(yRange);
    }

    return pos[0] >= xRange[0] && pos[0] <= xRange[1] && pos[1] >= yRange[0] &&
        pos[1] <= yRange[1];
}

//------------------------------------------------------------------------------
void vtkSHYXCompositeControlPointsItem::SnapPosToCurve(double pos[2])
{
    if (vtkPiecewiseFunction* pwf = this->GetOpacityFunction())
    {
        pos[1] = pwf->GetValue(pos[0]);
    }
}

//------------------------------------------------------------------------------
bool vtkSHYXCompositeControlPointsItem::Hit(const vtkContextMouseEvent& mouse)
{
    const vtkVector2f vpos = mouse.GetPos();
    double pos[2] = { vpos.GetX(), vpos.GetY() };
    this->TransformScreenToData(pos[0], pos[1], pos[0], pos[1]);

    if (this->FindPoint(pos) != -1)
    {
        return true;
    }

    return this->IsPosInPlotArea(pos);
}

//------------------------------------------------------------------------------
void vtkSHYXCompositeControlPointsItem::ClampCurveDataPos(double pos[2]) const
{
    if (!this->UseCurveDataBounds)
    {
        return;
    }
    pos[0] = std::min(this->CurveDataBounds[1], std::max(this->CurveDataBounds[0], pos[0]));
    pos[1] = std::min(this->CurveDataBounds[3], std::max(this->CurveDataBounds[2], pos[1]));
}

//------------------------------------------------------------------------------
void vtkSHYXCompositeControlPointsItem::ClampPlotDataPos(double pos[2])
{
    if (this->UseCurveDataBounds)
    {
        this->ClampCurveDataPos(pos);
        return;
    }

    double xRange[2] = { -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max() };
    double yRange[2] = { -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max() };
    if (this->GetXAxis())
    {
        this->GetXAxis()->GetUnscaledRange(xRange);
    }
    if (this->GetYAxis())
    {
        this->GetYAxis()->GetUnscaledRange(yRange);
    }

    if (xRange[1] > xRange[0])
    {
        pos[0] = std::min(xRange[1], std::max(xRange[0], pos[0]));
    }
    if (yRange[1] > yRange[0])
    {
        pos[1] = std::min(yRange[1], std::max(yRange[0], pos[1]));
    }
}

//------------------------------------------------------------------------------
vtkIdType vtkSHYXCompositeControlPointsItem::SetPointPosCurve(
    vtkIdType point, const vtkVector2f& newPos)
{
    if (point == -1)
    {
        return point;
    }

    double boundedPos[2];
    boundedPos[0] = newPos[0];
    boundedPos[1] = newPos[1];
    this->ClampPlotDataPos(boundedPos);

    if (!this->SwitchPointsMode)
    {
        if (point > 0)
        {
            double previousPoint[4] = { 0.0, 0.0, 0.0, 0.0 };
            this->GetControlPoint(point - 1, previousPoint);
            boundedPos[0] = std::max(previousPoint[0], boundedPos[0]);
        }
        if (point < this->GetNumberOfPoints() - 1)
        {
            double nextPoint[4] = { 0.0, 0.0, 0.0, 0.0 };
            this->GetControlPoint(point + 1, nextPoint);
            boundedPos[0] = std::min(nextPoint[0], boundedPos[0]);
        }
    }
    else
    {
        if (point > 0)
        {
            double previousPoint[4] = { 0.0, 0.0, 0.0, 0.0 };
            this->GetControlPoint(point - 1, previousPoint);
            while (boundedPos[0] < previousPoint[0])
            {
                point = point - 1;
                if (point == 0)
                {
                    break;
                }
                this->GetControlPoint(point - 1, previousPoint);
            }
        }
        if (point < this->GetNumberOfPoints() - 1)
        {
            double nextPoint[4] = { 0.0, 0.0, 0.0, 0.0 };
            this->GetControlPoint(point + 1, nextPoint);
            while (boundedPos[0] > nextPoint[0])
            {
                point = point + 1;
                if (point == this->GetNumberOfPoints() - 1)
                {
                    break;
                }
                this->GetControlPoint(point + 1, nextPoint);
            }
        }
    }

    double pointValues[4] = { 0.0, 0.0, 0.0, 0.0 };
    this->GetControlPoint(point, pointValues);
    if (pointValues[0] != boundedPos[0] || pointValues[1] != boundedPos[1])
    {
        pointValues[0] = boundedPos[0];
        pointValues[1] = boundedPos[1];
        this->SetControlPoint(point, pointValues);
    }
    return point;
}

//------------------------------------------------------------------------------
void vtkSHYXCompositeControlPointsItem::SetCurrentPointPosCurve(const vtkVector2f& newPos)
{
    const vtkIdType movedPoint = this->SetPointPosCurve(this->CurrentPoint, newPos);
    this->SetCurrentPoint(movedPoint);
}

//------------------------------------------------------------------------------
void vtkSHYXCompositeControlPointsItem::StrokeCurve(const vtkVector2f& newPos)
{
    double pos[2];
    pos[0] = newPos[0];
    pos[1] = newPos[1];
    this->ClampPlotDataPos(pos);
    this->SnapPosToCurve(pos);
    this->ClampPlotDataPos(pos);

    const vtkIdType pointId = this->AddPoint(pos);
    if (pointId >= 0)
    {
        this->SetCurrentPoint(pointId);
    }
}

//------------------------------------------------------------------------------
bool vtkSHYXCompositeControlPointsItem::MouseButtonPressEvent(
    const vtkContextMouseEvent& mouse)
{
    if (mouse.GetButton() != vtkContextMouseEvent::LEFT_BUTTON)
    {
        return this->Superclass::MouseButtonPressEvent(mouse);
    }

    this->MouseMoved = false;
    this->PointToToggle = -1;
    this->PointToDelete = -1;

    double pos[2];
    {
        const vtkVector2f vpos = mouse.GetPos();
        pos[0] = vpos.GetX();
        pos[1] = vpos.GetY();
    }
    this->TransformScreenToData(pos[0], pos[1], pos[0], pos[1]);

    const vtkIdType pointUnderMouse = this->FindPoint(pos);

    if (pointUnderMouse != -1)
    {
        this->SetCurrentPoint(pointUnderMouse);
        return true;
    }

    if (!this->StrokeMode)
    {
        this->ClampPlotDataPos(pos);
        this->SnapPosToCurve(pos);
        this->ClampPlotDataPos(pos);
        const vtkIdType addedPoint = this->AddPoint(pos);
        this->SetCurrentPoint(addedPoint);
        return true;
    }

    this->SetCurrentPoint(-1);
    return true;
}

//------------------------------------------------------------------------------
bool vtkSHYXCompositeControlPointsItem::MouseMoveEvent(const vtkContextMouseEvent& mouse)
{
    vtkVector2f mousePos = mouse.GetPos();
    this->TransformScreenToData(mousePos, mousePos);

    if (mouse.GetButton() == vtkContextMouseEvent::LEFT_BUTTON)
    {
        if (this->StrokeMode)
        {
            this->StartInteractionIfNotStarted();
            this->StrokeCurve(mousePos);
            this->Interaction();
        }
        else if (this->CurrentPoint != -1)
        {
            vtkVector2f curPos(mousePos);
            if (this->IsEndPointPicked())
            {
                double currentPoint[4] = { 0.0, 0.0, 0.0, 0.0 };
                this->GetControlPoint(this->CurrentPoint, currentPoint);
                if (!this->GetEndPointsMovable())
                {
                    return false;
                }
                if (!this->GetEndPointsYMovable())
                {
                    curPos.SetY(static_cast<float>(currentPoint[1]));
                }
                if (!this->GetEndPointsXMovable())
                {
                    curPos.SetX(static_cast<float>(currentPoint[0]));
                }
            }
            this->StartInteractionIfNotStarted();
            this->SetCurrentPointPosCurve(curPos);
            this->Interaction();
        }
        else
        {
            return this->Superclass::MouseMoveEvent(mouse);
        }
    }
    else
    {
        return this->Superclass::MouseMoveEvent(mouse);
    }

    this->MouseMoved = true;
    return true;
}
