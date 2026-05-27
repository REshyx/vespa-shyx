#include "vtkSHYXImplicitCylinderRepresentation.h"

#include "vtkAbstractPicker.h"
#include "vtkActor.h"
#include "vtkAssemblyPath.h"
#include "vtkProp.h"
#include "vtkCellArray.h"
#include "vtkCellPicker.h"
#include "vtkConeSource.h"
#include "vtkCylinder.h"
#include "vtkDataArray.h"
#include "vtkFeatureEdges.h"
#include "vtkInformation.h"
#include "vtkLineSource.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkPolyDataMapper.h"
#include "vtkRenderWindow.h"
#include "vtkRenderer.h"
#include "vtkSphereSource.h"
#include "vtkTubeFilter.h"
#include "vtkVector.h"

#include <algorithm>
#include <cmath>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXImplicitCylinderRepresentation);

namespace
{
constexpr double kEps = 1e-12;
/** Inflate finite-cylinder widget bounds by 10% beyond analytic geometry + pad. */
constexpr double kWidgetBoundsScale = 1.1;
/** vtkCellPicker tolerance for the stent cylinder shell (fraction of window diagonal). */
constexpr double kCylinderPickTolerance = 0.001;
/** End-cap cone picker tolerance (fraction of window diagonal; same units as CylPicker). */
constexpr double kConeCapPickTolerance = 0.001;
}

//----------------------------------------------------------------------------
vtkSHYXImplicitCylinderRepresentation::vtkSHYXImplicitCylinderRepresentation()
{
    this->ConfigureStentPickers();
}

//----------------------------------------------------------------------------
vtkSHYXImplicitCylinderRepresentation::~vtkSHYXImplicitCylinderRepresentation()
{
    if (this->ConeCapPicker)
    {
        this->ConeCapPicker->Delete();
        this->ConeCapPicker = nullptr;
    }
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::ConfigureStentPickers()
{
    // Axis lines run through the cylinder center; with vtkCellPicker tolerance they steal
    // hits from a thin shell. End cones use ConeCapPicker; the shell adjusts radius.
    if (this->Picker)
    {
        this->Picker->DeletePickList(this->LineActor);
        this->Picker->DeletePickList(this->LineActor2);
        this->Picker->DeletePickList(this->ConeActor);
        this->Picker->DeletePickList(this->ConeActor2);
    }
    if (this->CylPicker)
    {
        this->CylPicker->SetTolerance(kCylinderPickTolerance);
    }
    if (!this->ConeCapPicker)
    {
        this->ConeCapPicker = vtkCellPicker::New();
        this->ConeCapPicker->PickFromListOn();
    }
    this->ConeCapPicker->InitializePickList();
    this->ConeCapPicker->AddPickList(this->ConeActor);
    this->ConeCapPicker->AddPickList(this->ConeActor2);
    this->ConeCapPicker->SetTolerance(kConeCapPickTolerance);
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::CreateDefaultProperties()
{
    // vtkImplicitCylinderRepresentation::CreateDefaultProperties(): CylinderProperty ambient
    // white (1,1,1), opacity 0.5; SelectedCylinderProperty green (0,1,0), opacity 0.25; axis white;
    // selected axis red; edges red; then vtkBoundedWidgetRepresentation outline white / selected green.
    this->Superclass::CreateDefaultProperties();
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "FiniteStentLength: " << this->FiniteStentLength << "\n";
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::SetFiniteStentLengthHint(double L)
{
    if (!std::isfinite(L) || L <= 0.0)
    {
        return;
    }
    if (this->FiniteStentLength == L)
    {
        return;
    }
    this->FiniteStentLength = L;
    this->Modified();
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::FiniteCylinderWorldAABB(
    const double center[3], const double axis[3], double radius, double length, double bds[6])
{
    double a[3] = { axis[0], axis[1], axis[2] };
    if (vtkMath::Normalize(a) < kEps)
    {
        a[0] = 0.0;
        a[1] = 0.0;
        a[2] = 1.0;
    }
    double ref[3] = { 0.0, 0.0, 1.0 };
    if (std::fabs(vtkMath::Dot(a, ref)) > 0.9)
    {
        ref[0] = 1.0;
        ref[1] = 0.0;
        ref[2] = 0.0;
    }
    double u[3], v[3];
    vtkMath::Cross(a, ref, u);
    vtkMath::Normalize(u);
    vtkMath::Cross(a, u, v);
    vtkMath::Normalize(v);

    const double halfL = 0.5 * std::max(length, 0.0) * kWidgetBoundsScale;
    const double R = std::max(radius, 0.0) * kWidgetBoundsScale;
    double minC[3] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MAX, VTK_DOUBLE_MAX };
    double maxC[3] = { -VTK_DOUBLE_MAX, -VTK_DOUBLE_MAX, -VTK_DOUBLE_MAX };
    for (int sA = -1; sA <= 1; sA += 2)
    {
        for (int su = -1; su <= 1; su += 2)
        {
            for (int sv = -1; sv <= 1; sv += 2)
            {
                double p[3];
                for (int i = 0; i < 3; ++i)
                {
                    p[i] = center[i] + halfL * sA * a[i] + R * su * u[i] + R * sv * v[i];
                    minC[i] = std::min(minC[i], p[i]);
                    maxC[i] = std::max(maxC[i], p[i]);
                }
            }
        }
    }
    const double pad = std::max(1e-3, 0.06 * std::max(R, halfL));
    for (int i = 0; i < 3; ++i)
    {
        minC[i] -= pad;
        maxC[i] += pad;
    }
    bds[0] = minC[0];
    bds[1] = maxC[0];
    bds[2] = minC[1];
    bds[3] = maxC[1];
    bds[4] = minC[2];
    bds[5] = maxC[2];
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::ApplyFiniteLengthToWidgetBounds()
{
    double c[3];
    this->GetCenter(c);
    double a[3];
    this->GetAxis(a);
    if (vtkMath::Normalize(a) < kEps)
    {
        a[0] = 0.0;
        a[1] = 0.0;
        a[2] = 1.0;
    }
    const double R = this->GetRadius();
    double bds[6];
    vtkSHYXImplicitCylinderRepresentation::FiniteCylinderWorldAABB(c, a, R, this->FiniteStentLength, bds);
    this->SetWidgetBounds(bds);
    this->BuildRepresentation();
}

//----------------------------------------------------------------------------
int vtkSHYXImplicitCylinderRepresentation::ComputeInteractionState(int X, int Y, int vtkNotUsed(modify))
{
    vtkProp* coneProp = nullptr;
    if (this->Renderer && this->ConeCapPicker && this->ConeCapPicker->Pick(X, Y, 0., this->Renderer))
    {
        if (vtkAssemblyPath* conePath = this->ConeCapPicker->GetPath())
        {
            coneProp = conePath->GetFirstNode()->GetViewProp();
        }
    }

    vtkAssemblyPath* handlePath = this->GetAssemblyPath(X, Y, 0., this->Picker);

    vtkAssemblyPath* cylPath = nullptr;
    if (this->Renderer && this->CylPicker->Pick(X, Y, 0., this->Renderer))
    {
        cylPath = this->CylPicker->GetPath();
    }

    if (!coneProp && !handlePath && !cylPath)
    {
        this->SetRepresentationState(vtkImplicitCylinderRepresentation::Outside);
        this->InteractionState = vtkImplicitCylinderRepresentation::Outside;
        return this->InteractionState;
    }

    this->ValidPick = 1;

    if (this->InteractionState == vtkImplicitCylinderRepresentation::Moving)
    {
        vtkProp* handleProp = handlePath ? handlePath->GetFirstNode()->GetViewProp() : nullptr;
        vtkProp* cylProp = cylPath ? cylPath->GetFirstNode()->GetViewProp() : nullptr;

        const bool cylHit = cylProp == this->CylActor || cylProp == this->EdgesActor;
        const bool coneHit = coneProp == this->ConeActor || coneProp == this->ConeActor2;
        const bool sphereHit = handleProp == this->SphereActor;
        const bool outlineHit = handleProp == this->GetOutlineActor();

        if (coneHit)
        {
            // ConeActor is on +axis from center; ConeActor2 on -axis. Length delta uses outward cap motion.
            this->FiniteStentLengthDragSign = (coneProp == this->ConeActor2) ? -1 : 1;
            this->InteractionState = vtkImplicitCylinderRepresentation::RotatingAxis;
            this->SetRepresentationState(vtkImplicitCylinderRepresentation::RotatingAxis);
        }
        else if (sphereHit)
        {
            this->InteractionState = vtkImplicitCylinderRepresentation::MovingCenter;
            this->SetRepresentationState(vtkImplicitCylinderRepresentation::MovingCenter);
        }
        else if (cylHit)
        {
            this->InteractionState = vtkImplicitCylinderRepresentation::AdjustingRadius;
            this->SetRepresentationState(vtkImplicitCylinderRepresentation::AdjustingRadius);
        }
        else if (outlineHit && this->GetOutlineTranslation())
        {
            this->InteractionState = vtkImplicitCylinderRepresentation::MovingOutline;
            this->SetRepresentationState(vtkImplicitCylinderRepresentation::MovingOutline);
        }
        else if (handlePath)
        {
            if (this->GetOutlineTranslation())
            {
                this->InteractionState = vtkImplicitCylinderRepresentation::MovingOutline;
                this->SetRepresentationState(vtkImplicitCylinderRepresentation::MovingOutline);
            }
            else
            {
                this->InteractionState = vtkImplicitCylinderRepresentation::Outside;
                this->SetRepresentationState(vtkImplicitCylinderRepresentation::Outside);
            }
        }
        else
        {
            this->InteractionState = vtkImplicitCylinderRepresentation::Outside;
            this->SetRepresentationState(vtkImplicitCylinderRepresentation::Outside);
        }
    }
    else if (this->InteractionState != vtkImplicitCylinderRepresentation::Scaling)
    {
        this->InteractionState = vtkImplicitCylinderRepresentation::Outside;
    }

    return this->InteractionState;
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::SetRepresentationState(int state)
{
    state = std::min(std::max(state, static_cast<int>(vtkImplicitCylinderRepresentation::Outside)),
        static_cast<int>(vtkImplicitCylinderRepresentation::Scaling));

    if (this->RepresentationState == state)
    {
        return;
    }

    this->RepresentationState = state;
    this->Modified();

    this->HighlightNormal(0);
    this->HighlightCylinder(0);
    this->HighlightOutline(0);
    if (state == vtkImplicitCylinderRepresentation::RotatingAxis)
    {
        // Length handles at end cones only; do not green-highlight the cylinder shell.
        this->HighlightNormal(1);
    }
    else if (state == vtkImplicitCylinderRepresentation::AdjustingRadius)
    {
        this->HighlightCylinder(1);
    }
    else if (state == vtkImplicitCylinderRepresentation::MovingCenter)
    {
        this->HighlightNormal(1);
    }
    else if (state == vtkImplicitCylinderRepresentation::MovingOutline)
    {
        this->HighlightOutline(1);
    }
    else if (state == vtkImplicitCylinderRepresentation::Scaling && this->ScaleEnabled)
    {
        this->HighlightNormal(1);
        this->HighlightCylinder(1);
        this->HighlightOutline(1);
    }
    else if (state == vtkImplicitCylinderRepresentation::TranslatingCenter)
    {
        this->HighlightNormal(1);
    }
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::WidgetInteraction(double e[2])
{
    if (this->InteractionState == vtkImplicitCylinderRepresentation::RotatingAxis)
    {
        vtkAbstractPicker* capPicker = static_cast<vtkAbstractPicker*>(this->ConeCapPicker);
        vtkVector3d prevPickPoint = this->GetWorldPoint(capPicker, this->LastEventPosition);
        vtkVector3d pickPoint = this->GetWorldPoint(capPicker, e);

        double a[3];
        this->GetAxis(a);
        if (vtkMath::Normalize(a) < kEps)
        {
            a[0] = 0.0;
            a[1] = 0.0;
            a[2] = 1.0;
        }

        const double dp[3] = { pickPoint[0] - prevPickPoint[0], pickPoint[1] - prevPickPoint[1],
            pickPoint[2] - prevPickPoint[2] };
        const double deltaL = 2.0 * this->FiniteStentLengthDragSign * vtkMath::Dot(dp, a);
        this->FiniteStentLength = std::max(1e-9, this->FiniteStentLength + deltaL);
        this->Modified();

        this->ApplyFiniteLengthToWidgetBounds();

        this->LastEventPosition[0] = e[0];
        this->LastEventPosition[1] = e[1];
        this->LastEventPosition[2] = 0.0;
        return;
    }

    this->Superclass::WidgetInteraction(e);
}

//----------------------------------------------------------------------------
// vtkImplicitCylinderRepresentation::BuildCylinder clips an *infinite* cylinder to the outline
// box using line segments of length 2*GetDiagonalLength() along the axis; axis handles use
// 0.30*diagonal along the axis. For a short finite stent that reads as "infinite" geometry.
// Here we draw a true finite side surface between +/- halfLength along the axis and shorten
// the axis line/cones to stay within that span (adapted from VTK vtkImplicitCylinderRepresentation).
void vtkSHYXImplicitCylinderRepresentation::BuildFiniteStentCylinder()
{
    this->Cyl->Reset();
    vtkPoints* pts = this->Cyl->GetPoints();
    vtkDataArray* normals = this->Cyl->GetPointData()->GetNormals();
    vtkCellArray* polys = this->Cyl->GetPolys();

    double* center = this->Cylinder->GetCenter();
    double* axis = this->Cylinder->GetAxis();
    const double radius = this->Cylinder->GetRadius();
    const int res = this->Resolution;

    double aunit[3] = { axis[0], axis[1], axis[2] };
    if (vtkMath::Normalize(aunit) < kEps)
    {
        aunit[0] = 0.0;
        aunit[1] = 0.0;
        aunit[2] = 1.0;
    }

    const double halfL = 0.5 * std::max(this->FiniteStentLength, 1e-12);
    const double v[3] = { halfL * aunit[0], halfL * aunit[1], halfL * aunit[2] };

    int i;
    double n1[3], n2[3];
    for (i = 0; i < 3; ++i)
    {
        if (axis[i] != 0.0)
        {
            n1[(i + 2) % 3] = 0.0;
            n1[(i + 1) % 3] = 1.0;
            n1[i] = -axis[(i + 1) % 3] / axis[i];
            break;
        }
    }
    if (i >= 3)
    {
        n1[0] = 1.0;
        n1[1] = 0.0;
        n1[2] = 0.0;
    }
    vtkMath::Normalize(n1);
    vtkMath::Cross(aunit, n1, n2);
    vtkMath::Normalize(n2);

    pts->SetNumberOfPoints(2 * res);
    normals->SetNumberOfTuples(2 * res);

    for (vtkIdType pid = 0; pid < res; ++pid)
    {
        const double theta = static_cast<double>(pid) / static_cast<double>(res) * 2.0 * vtkMath::Pi();
        double n[3];
        double x[3];
        for (i = 0; i < 3; ++i)
        {
            n[i] = n1[i] * cos(theta) + n2[i] * sin(theta);
            x[i] = center[i] + radius * n[i] + v[i];
        }
        pts->SetPoint(pid, x);
        normals->SetTuple(pid, n);

        for (i = 0; i < 3; ++i)
        {
            x[i] = center[i] + radius * n[i] - v[i];
        }
        pts->SetPoint(res + pid, x);
        normals->SetTuple(res + pid, n);
    }

    polys->Reset();
    vtkIdType ptIds[4];
    for (vtkIdType pid = 0; pid < res; ++pid)
    {
        ptIds[0] = pid;
        ptIds[3] = (pid + 1) % res;
        ptIds[1] = ptIds[0] + res;
        ptIds[2] = ptIds[3] + res;
        polys->InsertNextCell(4, ptIds);
    }
    polys->Modified();
}

//----------------------------------------------------------------------------
void vtkSHYXImplicitCylinderRepresentation::BuildRepresentation()
{
    if (!this->Renderer || !this->Renderer->GetRenderWindow())
    {
        return;
    }

    vtkInformation* info = this->GetPropertyKeys();
    this->GetOutlineActor()->SetPropertyKeys(info);
    this->CylActor->SetPropertyKeys(info);
    this->EdgesActor->SetPropertyKeys(info);
    this->ConeActor->SetPropertyKeys(info);
    this->LineActor->SetPropertyKeys(info);
    this->ConeActor2->SetPropertyKeys(info);
    this->LineActor2->SetPropertyKeys(info);
    this->SphereActor->SetPropertyKeys(info);

    if (this->GetMTime() > this->BuildTime || this->Cylinder->GetMTime() > this->BuildTime ||
        this->Renderer->GetRenderWindow()->GetMTime() > this->BuildTime)
    {
        double* center = this->Cylinder->GetCenter();
        double* axis = this->Cylinder->GetAxis();
        double p2[3];

        this->UpdateCenterAndBounds(center);

        double ax[3] = { axis[0], axis[1], axis[2] };
        if (vtkMath::Normalize(ax) < kEps)
        {
            ax[0] = 0.0;
            ax[1] = 0.0;
            ax[2] = 1.0;
        }
        const double diag = this->GetDiagonalLength();
        const double halfL = 0.5 * std::max(this->FiniteStentLength, 1e-12);
        const double axisHalfLenForHandles = std::min(0.30 * diag, halfL);

        p2[0] = center[0] + axisHalfLenForHandles * ax[0];
        p2[1] = center[1] + axisHalfLenForHandles * ax[1];
        p2[2] = center[2] + axisHalfLenForHandles * ax[2];

        this->LineSource->SetPoint1(center);
        this->LineSource->SetPoint2(p2);
        this->ConeSource->SetCenter(p2);
        this->ConeSource->SetDirection(axis);

        p2[0] = center[0] - axisHalfLenForHandles * ax[0];
        p2[1] = center[1] - axisHalfLenForHandles * ax[1];
        p2[2] = center[2] - axisHalfLenForHandles * ax[2];

        this->LineSource2->SetPoint1(center[0], center[1], center[2]);
        this->LineSource2->SetPoint2(p2);
        this->ConeSource2->SetCenter(p2);
        this->ConeSource2->SetDirection(axis[0], axis[1], axis[2]);

        this->Sphere->SetCenter(center[0], center[1], center[2]);

        if (this->Tubing)
        {
            this->EdgesMapper->SetInputConnection(this->EdgesTuber->GetOutputPort());
        }
        else
        {
            this->EdgesMapper->SetInputConnection(this->Edges->GetOutputPort());
        }

        this->BuildFiniteStentCylinder();

        this->SizeHandles();
        this->BuildTime.Modified();
    }
}

VTK_ABI_NAMESPACE_END
