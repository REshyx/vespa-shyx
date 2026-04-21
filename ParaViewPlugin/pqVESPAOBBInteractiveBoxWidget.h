#ifndef pqVESPAOBBInteractiveBoxWidget_h
#define pqVESPAOBBInteractiveBoxWidget_h

#include "pqBoxPropertyWidget.h"

#include <QMetaObject>
#include <vector>

class pqView;

/**
 * Interactive box for SHYX Selection OBB Boolean Subtract: uses a unit PlaceWidget bounds and keeps
 * ReferenceBounds in sync with the filter (fitted OBB orientation from field data on the server),
 * instead of driving the widget from the world-axis-aligned bounds of port 1.
 *
 * When the "OBB Box" output (port 1) has a display in the active view, the 3D box widget stays in
 * sync with that representation's Visibility (eye icon), similar to hiding geometry that carries a
 * transform gizmo.
 */
class pqVESPAOBBInteractiveBoxWidget : public pqBoxPropertyWidget
{
    Q_OBJECT
    typedef pqBoxPropertyWidget Superclass;

public:
    pqVESPAOBBInteractiveBoxWidget(vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqVESPAOBBInteractiveBoxWidget() override;

public Q_SLOTS:
    void setView(pqView* view) override;

protected Q_SLOTS:
    void placeWidget() override;
    void updateWidgetVisibility() override;

private:
    void disconnectViewVisibilityLinks();
    /** Port 1 ("OBB Box") is shown in \a view; true if there is no port-1 display in that view yet. */
    bool isObbPort1VisibleInView(pqView* view) const;

    /** Fingerprint of OBB field data last used to push Position/Rotation/Scale onto the filter proxy. */
    unsigned long long LastObbFieldFingerprint = 0ULL;
    std::vector<QMetaObject::Connection> ViewVisibilityConnections;
};

#endif
