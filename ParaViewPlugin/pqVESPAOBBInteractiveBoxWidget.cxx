#include "pqVESPAOBBInteractiveBoxWidget.h"

#include "pqApplicationCore.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqRepresentation.h"
#include "pqServerManagerModel.h"
#include "pqUndoStack.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkCommand.h"
#include "vtkDataArray.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkFieldData.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkPVDataInformation.h"
#include "vtkPolyData.h"
#include "vtkSMNewWidgetRepresentationProxy.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMUncheckedPropertyHelper.h"
#include "vtkTransform.h"

#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>

namespace
{
bool ReadTuple3(vtkFieldData* fd, const char* name, double out[3])
{
    if (!fd || !name)
    {
        return false;
    }
    auto* arr = vtkDataArray::SafeDownCast(fd->GetAbstractArray(name));
    if (!arr || arr->GetNumberOfTuples() < 1 || arr->GetNumberOfComponents() != 3)
    {
        return false;
    }
    arr->GetTuple(0, out);
    return true;
}

bool ReadObbField(vtkPolyData* pd, double center[3], double half[3], double u0[3], double u1[3], double u2[3])
{
    if (!pd)
    {
        return false;
    }
    vtkFieldData* fd = pd->GetFieldData();
    if (!fd)
    {
        return false;
    }
    if (!ReadTuple3(fd, "OBB.Center", center) || !ReadTuple3(fd, "OBB.HalfLengths", half) ||
        !ReadTuple3(fd, "OBB.Axis0", u0) || !ReadTuple3(fd, "OBB.Axis1", u1) || !ReadTuple3(fd, "OBB.Axis2", u2))
    {
        return false;
    }
    vtkMath::Normalize(u0);
    vtkMath::Normalize(u1);
    vtkMath::Normalize(u2);
    return true;
}

std::uint64_t HashObbField(const double C[3], const double h[3], const double u0[3], const double u1[3],
    const double u2[3])
{
    std::uint64_t x = 14695981039346656037ULL;
    auto mix = [&](double d) {
        x ^= static_cast<std::uint64_t>(std::llround(d * 1e6));
        x *= 1099511628211ULL;
    };
    for (int i = 0; i < 3; ++i)
    {
        mix(C[i]);
    }
    for (int i = 0; i < 3; ++i)
    {
        mix(h[i]);
    }
    for (int i = 0; i < 3; ++i)
    {
        mix(u0[i]);
        mix(u1[i]);
        mix(u2[i]);
    }
    return x == 0 ? 1ULL : x;
}

std::uint64_t HashBounds(const double b[6])
{
    std::uint64_t x = 14695981039346656037ULL;
    for (int i = 0; i < 6; ++i)
    {
        x ^= static_cast<std::uint64_t>(std::llround(b[i] * 1e6));
        x *= 1099511628211ULL;
    }
    return x == 0 ? 1ULL : x;
}

/** Box widget links listen for UncheckedPropertyModifiedEvent on the filter (vtkSMNewWidgetRepresentationProxy). */
void SetUncheckedDoubles(vtkSMProxy* proxy, const char* name, const double* v, int n)
{
    if (vtkSMProperty* prop = proxy->GetProperty(name))
    {
        vtkSMUncheckedPropertyHelper h(prop);
        h.Set(v, n);
    }
}

void SetUncheckedInt(vtkSMProxy* proxy, const char* name, int v)
{
    if (vtkSMProperty* prop = proxy->GetProperty(name))
    {
        vtkSMUncheckedPropertyHelper h(prop);
        h.Set(0, v);
    }
}

void SetCheckedDoubles(vtkSMProxy* proxy, const char* name, const double* v, int n)
{
    if (vtkSMProperty* prop = proxy->GetProperty(name))
    {
        vtkSMPropertyHelper(prop).Set(v, n);
    }
}

void SetCheckedInt(vtkSMProxy* proxy, const char* name, int v)
{
    if (vtkSMProperty* prop = proxy->GetProperty(name))
    {
        vtkSMPropertyHelper(prop).Set(0, v);
    }
}

void SetWidgetDoubles(vtkSMProxy* wdg, const char* name, const double* v, int n)
{
    if (wdg->GetProperty(name))
    {
        vtkSMPropertyHelper(wdg, name, /*quiet=*/true).Set(v, n);
    }
}

void SetWidgetInt(vtkSMProxy* wdg, const char* name, int v)
{
    if (wdg->GetProperty(name))
    {
        vtkSMPropertyHelper(wdg, name, /*quiet=*/true).Set(0, v);
    }
}

/** Push fitted-box PRS onto filter (checked + unchecked) and the 3D widget representation. */
void PushPrs(vtkSMProxy* src, vtkSMProxy* wdg, const double* pos, const double* rotDeg, const double* scale,
    const double* refBounds, int nBounds)
{
    SetCheckedInt(src, "UseReferenceBounds", 1);
    SetCheckedDoubles(src, "ReferenceBounds", refBounds, nBounds);
    SetCheckedDoubles(src, "Position", pos, 3);
    SetCheckedDoubles(src, "Rotation", rotDeg, 3);
    SetCheckedDoubles(src, "Scale", scale, 3);

    SetUncheckedInt(src, "UseReferenceBounds", 1);
    SetUncheckedDoubles(src, "ReferenceBounds", refBounds, nBounds);
    SetUncheckedDoubles(src, "Position", pos, 3);
    SetUncheckedDoubles(src, "Rotation", rotDeg, 3);
    SetUncheckedDoubles(src, "Scale", scale, 3);

    SetWidgetInt(wdg, "UseReferenceBounds", 1);
    SetWidgetDoubles(wdg, "ReferenceBounds", refBounds, nBounds);
    SetWidgetDoubles(wdg, "Position", pos, 3);
    SetWidgetDoubles(wdg, "Rotation", rotDeg, 3);
    SetWidgetDoubles(wdg, "Scale", scale, 3);
}

void SetWidgetPlaceFactorOne(vtkSMProxy* wdg)
{
    // vtkWidgetRepresentation defaults PlaceFactor to 0.5, shrinking [0,1]^3 to [0.25,0.75]^3
    // before vtkBoxRepresentation applies PRS — our filter assumes a full unit reference box.
    if (wdg && wdg->GetProperty("PlaceFactor"))
    {
        vtkSMPropertyHelper(wdg, "PlaceFactor", /*quiet=*/true).Set(0, 1.0);
    }
}

} // namespace

//-----------------------------------------------------------------------------
pqVESPAOBBInteractiveBoxWidget::pqVESPAOBBInteractiveBoxWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
    : pqBoxPropertyWidget(proxy, smgroup, parent, true)
{
    if (auto* vbox = qobject_cast<QVBoxLayout*>(this->layout()))
    {
        auto* resetBtn = new QPushButton(tr("Reset to Fitted Box"), this);
        resetBtn->setToolTip(
            tr("Restore Translate / Rotate / Scale to the fitted OBB or AABB (before interactive adjustments)."));
        vbox->addWidget(resetBtn);
        QObject::connect(resetBtn, &QPushButton::clicked, this, &pqVESPAOBBInteractiveBoxWidget::resetToFittedBox);
    }

    // Minimum OBB: when BoxType changes, force re-place PRS to the new fitted box and push
    // properties (avoids one-Apply lag from stale Position/Rotation/Scale). Defer so we run
    // after the BoxType property value has fully settled on the proxy.
    if (vtkSMProperty* boxType = proxy->GetProperty("BoxType"))
    {
        this->BoxTypeConnect = vtkEventQtSlotConnect::New();
        this->BoxTypeConnect->Connect(boxType, vtkCommand::ModifiedEvent, this, SLOT(onBoxTypeChanged()));
    }
}

//-----------------------------------------------------------------------------
pqVESPAOBBInteractiveBoxWidget::~pqVESPAOBBInteractiveBoxWidget()
{
    this->disconnectViewVisibilityLinks();
    if (this->BoxTypeConnect)
    {
        this->BoxTypeConnect->Delete();
        this->BoxTypeConnect = nullptr;
    }
}

//-----------------------------------------------------------------------------
void pqVESPAOBBInteractiveBoxWidget::resetToFittedBox()
{
    BEGIN_UNDO_SET(tr("Reset OBB Interactive Box"));
    this->LastObbFieldFingerprint = 0ULL;
    this->placeWidget();
    Q_EMIT this->changeAvailable();
    Q_EMIT this->changeFinished();
    END_UNDO_SET();
}

//-----------------------------------------------------------------------------
void pqVESPAOBBInteractiveBoxWidget::onBoxTypeChanged()
{
    // Run after the current event so BoxType on the proxy is the value the user just chose.
    QTimer::singleShot(0, this, [this]() { this->resetToFittedBox(); });
}

//-----------------------------------------------------------------------------
void pqVESPAOBBInteractiveBoxWidget::disconnectViewVisibilityLinks()
{
    for (const QMetaObject::Connection& c : this->ViewVisibilityConnections)
    {
        QObject::disconnect(c);
    }
    this->ViewVisibilityConnections.clear();
}

//-----------------------------------------------------------------------------
int pqVESPAOBBInteractiveBoxWidget::obbOutputPort() const
{
    return 0;
}

//-----------------------------------------------------------------------------
bool pqVESPAOBBInteractiveBoxWidget::isObbOutputVisibleInView(pqView* view) const
{
    if (!view)
    {
        return true;
    }
    auto* smm = pqApplicationCore::instance()->getServerManagerModel();
    auto* src = smm->findItem<pqPipelineSource*>(this->proxy());
    if (!src)
    {
        return true;
    }
    const int port = this->obbOutputPort();
    bool foundObbRepr = false;
    for (pqRepresentation* repr : view->getRepresentations())
    {
        auto* dr = qobject_cast<pqDataRepresentation*>(repr);
        if (!dr || dr->getInput() != src)
        {
            continue;
        }
        pqOutputPort* op = dr->getOutputPortFromInput();
        if (!op || op->getPortNumber() != port)
        {
            continue;
        }
        foundObbRepr = true;
        if (dr->isVisible())
        {
            return true;
        }
    }
    if (!foundObbRepr)
    {
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
void pqVESPAOBBInteractiveBoxWidget::setView(pqView* view)
{
    this->disconnectViewVisibilityLinks();
    if (view)
    {
        this->ViewVisibilityConnections.push_back(QObject::connect(view,
            &pqView::representationVisibilityChanged, this,
            [this](pqRepresentation* /*repr*/, bool /*visible*/) { this->updateWidgetVisibility(); }));
    }
    this->Superclass::setView(view);
}

//-----------------------------------------------------------------------------
void pqVESPAOBBInteractiveBoxWidget::updateWidgetVisibility()
{
    bool visible = this->isSelected() && this->isWidgetVisible() && this->view();
    if (visible && !this->isObbOutputVisibleInView(this->view()))
    {
        visible = false;
    }
    vtkSMProxy* wdgProxy = this->widgetProxy();
    if (!wdgProxy)
    {
        return;
    }
    vtkSMPropertyHelper(wdgProxy, "Visibility", true).Set(visible ? 1 : 0);
    vtkSMPropertyHelper(wdgProxy, "Enabled", true).Set(visible ? 1 : 0);
    wdgProxy->UpdateVTKObjects();
    this->render();
    Q_EMIT this->widgetVisibilityUpdated(visible);
}

//-----------------------------------------------------------------------------
void pqVESPAOBBInteractiveBoxWidget::placeWidget()
{
    auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
    auto* wdg = this->widgetProxy();
    if (!src || !wdg)
    {
        return;
    }

    src->UpdatePipeline();
    src->UpdatePipelineInformation();

    constexpr double ub[6] = { 0, 1, 0, 1, 0, 1 };
    const int port = this->obbOutputPort();

    vtkPolyData* pd = nullptr;
    if (vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject()))
    {
        pd = vtkPolyData::SafeDownCast(alg->GetOutputDataObject(port));
    }

    vtkPVDataInformation* di = src->GetDataInformation(port);
    if (!di || di->GetNumberOfPoints() < 1)
    {
        this->LastObbFieldFingerprint = 0ULL;
        const double zub[6] = { 0, 1, 0, 1, 0, 1 };
        SetWidgetPlaceFactorOne(wdg);
        vtkSMPropertyHelper(wdg, "PlaceWidget").Set(zub, 6);
        wdg->UpdateVTKObjects();
        this->render();
        return;
    }

    double C[3], h[3], u0[3], u1[3], u2[3];
    const bool haveObbField = pd ? ReadObbField(pd, C, h, u0, u1, u2) : false;
    if (haveObbField)
    {
        std::uint64_t fp = HashObbField(C, h, u0, u1, u2);
        if (src->GetProperty("BoxType"))
        {
            const int boxType = vtkSMPropertyHelper(src, "BoxType").GetAsInt();
            fp ^= (static_cast<std::uint64_t>(boxType) + 1ULL) * 0x9e3779b97f4a7c15ULL;
        }
        if (fp != this->LastObbFieldFingerprint)
        {
            this->LastObbFieldFingerprint = fp;

            vtkNew<vtkMatrix4x4> rm;
            rm->Identity();
            for (int col = 0; col < 3; ++col)
            {
                const double* u = (col == 0) ? u0 : (col == 1) ? u1 : u2;
                for (int row = 0; row < 3; ++row)
                {
                    rm->SetElement(row, col, u[row]);
                }
            }

            double rotDeg[3];
            vtkTransform::GetOrientation(rotDeg, rm);

            const double scale[3] = { 2.0 * h[0], 2.0 * h[1], 2.0 * h[2] };
            // Same as vtkPVTransform: ref (0,0,0) -> Position (world min corner for this OBB).
            const double pos[3] = { C[0] - h[0] * u0[0] - h[1] * u1[0] - h[2] * u2[0],
                C[1] - h[0] * u0[1] - h[1] * u1[1] - h[2] * u2[1],
                C[2] - h[0] * u0[2] - h[1] * u1[2] - h[2] * u2[2] };

            PushPrs(src, wdg, pos, rotDeg, scale, ub, 6);
        }
    }
    else
    {
        double b[6];
        if (pd)
        {
            pd->GetBounds(b);
        }
        else
        {
            di->GetBounds(b);
        }
        const std::uint64_t fp = HashBounds(b);
        if (fp != this->LastObbFieldFingerprint)
        {
            this->LastObbFieldFingerprint = fp;
            const double zero[3] = { 0, 0, 0 };
            const double one[3] = { 1, 1, 1 };
            PushPrs(src, wdg, zero, zero, one, b, 6);
        }
    }

    // Always keep reference mode consistent for the OBB field case.
    if (haveObbField)
    {
        SetUncheckedInt(src, "UseReferenceBounds", 1);
        SetUncheckedDoubles(src, "ReferenceBounds", ub, 6);
        SetWidgetInt(wdg, "UseReferenceBounds", 1);
        SetWidgetDoubles(wdg, "ReferenceBounds", ub, 6);
    }

    SetWidgetPlaceFactorOne(wdg);
    vtkSMPropertyHelper(wdg, "PlaceWidget").Set(ub, 6);

    wdg->UpdateVTKObjects();
    src->UpdateVTKObjects();
    this->render();
}
