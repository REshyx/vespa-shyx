#include "vtkSHYXImplicitCylinderRepresentation.h"

#include "vtkAbstractPicker.h"
#include "vtkCellPicker.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkVector.h"

#include <algorithm>
#include <cmath>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXImplicitCylinderRepresentation);

namespace
{
constexpr double kEps = 1e-12;
}

//----------------------------------------------------------------------------
vtkSHYXImplicitCylinderRepresentation::vtkSHYXImplicitCylinderRepresentation() = default;

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
    this->FiniteStentLength = L;
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

    const double halfL = 0.5 * std::max(length, 0.0);
    const double R = std::max(radius, 0.0);
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
    const double pad = std::max(1e-3, 0.02 * std::max(R, halfL));
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
void vtkSHYXImplicitCylinderRepresentation::WidgetInteraction(double e[2])
{
    if (this->InteractionState == vtkImplicitCylinderRepresentation::RotatingAxis)
    {
        vtkVector3d prevPickPoint =
            this->GetWorldPoint(static_cast<vtkAbstractPicker*>(this->Picker), this->LastEventPosition);
        vtkVector3d pickPoint = this->GetWorldPoint(static_cast<vtkAbstractPicker*>(this->Picker), e);

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
        const double deltaL = 2.0 * vtkMath::Dot(dp, a);
        this->FiniteStentLength = std::max(1e-9, this->FiniteStentLength + deltaL);

        this->ApplyFiniteLengthToWidgetBounds();

        this->LastEventPosition[0] = e[0];
        this->LastEventPosition[1] = e[1];
        this->LastEventPosition[2] = 0.0;
        return;
    }

    this->Superclass::WidgetInteraction(e);
}

VTK_ABI_NAMESPACE_END
