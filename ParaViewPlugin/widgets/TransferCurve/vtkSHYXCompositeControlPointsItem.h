#ifndef vtkSHYXCompositeControlPointsItem_h
#define vtkSHYXCompositeControlPointsItem_h

#include <vtkCompositeControlPointsItem.h>

/** Control points with Y clamping to physical output axis (not vtkPlot's [0,1]). */
class vtkSHYXCompositeControlPointsItem : public vtkCompositeControlPointsItem
{
public:
    static vtkSHYXCompositeControlPointsItem* New();
    vtkTypeMacro(vtkSHYXCompositeControlPointsItem, vtkCompositeControlPointsItem);

    void SetCurveDataBounds(double xmin, double xmax, double ymin, double ymax);

    bool Hit(const vtkContextMouseEvent& mouse) override;
    bool MouseButtonPressEvent(const vtkContextMouseEvent& mouse) override;
    bool MouseMoveEvent(const vtkContextMouseEvent& mouse) override;

protected:
    vtkSHYXCompositeControlPointsItem() = default;
    ~vtkSHYXCompositeControlPointsItem() override = default;

    bool IsPosInPlotArea(const double pos[2]);
    void SnapPosToCurve(double pos[2]);

    void ClampCurveDataPos(double pos[2]) const;
    void ClampPlotDataPos(double pos[2]);
    vtkIdType SetPointPosCurve(vtkIdType point, const vtkVector2f& newPos);
    void SetCurrentPointPosCurve(const vtkVector2f& newPos);
    void StrokeCurve(const vtkVector2f& newPos);

    double CurveDataBounds[4] = { 0.0, -1.0, 0.0, -1.0 };
    bool UseCurveDataBounds = false;

private:
    vtkSHYXCompositeControlPointsItem(const vtkSHYXCompositeControlPointsItem&) = delete;
    void operator=(const vtkSHYXCompositeControlPointsItem&) = delete;
};

#endif
