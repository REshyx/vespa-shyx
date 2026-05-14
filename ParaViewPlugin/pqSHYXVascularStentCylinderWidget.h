#ifndef pqSHYXVascularStentCylinderWidget_h
#define pqSHYXVascularStentCylinderWidget_h

#include "pqCylinderPropertyWidget.h"

class vtkPolyData;
class vtkPoints;

/**
 * ImplicitCylinder 3D widget for SHYX Vascular Stent Placement: WidgetBounds are sized from
 * StentLength / StentRadius (single source of truth on the filter), not the other way around.
 * ParaView-style interactions: cylinder surface radius, middle-button outline translation, cap
 * handles (outline) for visual adjustment only. On interaction end: AnchorCenterlinePointId from
 * nearest centerline vertex to the widget center, StentRadius from widget radius, StentWidgetAxis
 * from widget axis; StentLength is never derived from WidgetBounds (avoids truncated-cylinder
 * length error).
 */
class pqSHYXVascularStentCylinderWidget : public pqCylinderPropertyWidget
{
    Q_OBJECT
    typedef pqCylinderPropertyWidget Superclass;

public:
    pqSHYXVascularStentCylinderWidget(
        vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXVascularStentCylinderWidget() override;

    void select() override;

protected Q_SLOTS:
    void placeWidget() override;

private Q_SLOTS:
    void onCylinderEndInteraction();

private:
    vtkPolyData* centerlineClientPoly() const;
    void syncStentWidgetFromAnchorOnSelect();
    static void tangentAtCenterlineVertex(vtkPolyData* cl, vtkIdType anchor, double axisOut[3]);
    static void finiteCylinderWorldAABB(
        const double center[3], const double axis[3], double radius, double length, double bds[6]);
    static vtkIdType nearestPointId(vtkPoints* pts, const double p[3]);
};

#endif
