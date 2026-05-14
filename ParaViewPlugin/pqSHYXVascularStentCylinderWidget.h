#ifndef pqSHYXVascularStentCylinderWidget_h
#define pqSHYXVascularStentCylinderWidget_h

#include "pqInteractivePropertyWidget.h"
#include "pqPropertyLinks.h"

#include <QWidget>

class vtkPolyData;
class vtkPoints;

/**
 * ImplicitCylinder 3D widget for SHYX Vascular Stent Placement: finite-length bounds from
 * StentLength / StentRadius (no scene-wide reset), ParaView-style interactions (cylinder surface
 * radius, middle-button outline translation like the stock cylinder/box widgets, caps via outline
 * extent). On interaction end, StentRadius is pushed from the widget (StentLength is not: deriving
 * it from axis-aligned WidgetBounds inflates length for oblique axes). After an outline
 * (bounding-box) drag only, AnchorCenterlinePointId is set to the centerline vertex nearest to the
 * widget center (client-side centerline polydata when available), the widget axis is aligned to the
 * local centerline tangent (interior degree-2: chord between the two neighbors; degree-1: toward
 * the neighbor; higher degree: average outgoing unit directions, with chord fallback if that
 * cancels), and when CenterlineRadiusArrayName is non-empty (default MaximumInscribedSphereRadius),
 * StentRadius is taken from that point-data array at the snapped vertex. Radius / axis / center
 * handle drags do not re-snap center, tangent, or MISR. Axis line/cone handles adjust StentLength
 * along the cylinder axis (vtkSHYXImplicitCylinderRepresentation).
 */
class pqSHYXVascularStentCylinderWidget : public pqInteractivePropertyWidget
{
    Q_OBJECT
    typedef pqInteractivePropertyWidget Superclass;

public:
    pqSHYXVascularStentCylinderWidget(
        vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXVascularStentCylinderWidget() override;

    void select() override;

public Q_SLOTS:
    void useXAxis() { this->setAxis(1, 0, 0); }
    void useYAxis() { this->setAxis(0, 1, 0); }
    void useZAxis() { this->setAxis(0, 0, 1); }
    void resetCameraToAxis();
    void useCameraAxis();

protected Q_SLOTS:
    void placeWidget() override;
    void resetBounds();

private Q_SLOTS:
    void onCylinderEndInteraction();

protected:
    void updateWidget(bool showing_advanced_properties) override;

private:
    vtkPolyData* centerlineClientPoly() const;
    void syncStentWidgetFromAnchorOnSelect();
    static void tangentAtCenterlineVertex(vtkPolyData* cl, vtkIdType anchor, double axisOut[3]);
    static void finiteCylinderWorldAABB(
        const double center[3], const double axis[3], double radius, double length, double bds[6]);
    static vtkIdType nearestPointId(vtkPoints* pts, const double p[3]);
    void setAxis(double x, double y, double z);
    void syncFiniteLengthHintFromFilter();

    pqPropertyLinks WidgetLinks;
    QWidget* AdvancedPropertyWidgets[2] = { nullptr, nullptr };
};

#endif
