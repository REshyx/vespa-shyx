#ifndef pqVESPAOBBInteractiveBoxWidget_h
#define pqVESPAOBBInteractiveBoxWidget_h

#include "pqBoxPropertyWidget.h"

#include <QMetaObject>
#include <vector>

class pqView;
class vtkEventQtSlotConnect;

/**
 * Interactive box for SHYX filters that expose Position / Rotation / Scale with OBB field data:
 * SHYX Selection OBB Boolean Subtract (OBB on port 1) and SHYX Minimum OBB (OBB on port 0).
 * Uses a unit PlaceWidget bounds and keeps ReferenceBounds in sync with the filter (fitted OBB
 * orientation from field data), instead of driving the widget from the world-axis-aligned AABB.
 *
 * When the OBB output has a display in the active view, the 3D box widget stays in sync with that
 * representation's Visibility (eye icon). Switching BoxType (min-volume / PCA / AABB) on
 * Minimum OBB re-places the widget to the newly fitted box.
 *
 * "Reset to Fitted Box" restores Position / Rotation / Scale to the values that match the current
 * fitted OBB field data (state before interactive adjustments).
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
    /** Restore interactive PRS to the fitted OBB / AABB (before user adjustments). */
    void resetToFittedBox();

protected Q_SLOTS:
    void placeWidget() override;
    void updateWidgetVisibility() override;
    void onBoxTypeChanged();

private:
    void disconnectViewVisibilityLinks();
    /** Output port that carries the OBB mesh / field data (1 if multi-port, else 0). */
    int obbOutputPort() const;
    /** OBB output is shown in \a view; true if there is no OBB display in that view yet. */
    bool isObbOutputVisibleInView(pqView* view) const;

    /** Fingerprint of OBB field data last used to push Position/Rotation/Scale onto the filter proxy. */
    unsigned long long LastObbFieldFingerprint = 0ULL;
    std::vector<QMetaObject::Connection> ViewVisibilityConnections;
    vtkEventQtSlotConnect* BoxTypeConnect = nullptr;
};

#endif
