/**
 * @class   vtkSHYXImplicitCylinderRepresentation
 * @brief   Finite-length stent cylinder: axis handles change length (not axis rotation), and the
 *          shaded cylinder mesh is a true finite tube along the axis (not an infinite cylinder
 *          clipped by the outline box like vtkImplicitCylinderRepresentation::BuildCylinder).
 */

#ifndef vtkSHYXImplicitCylinderRepresentation_h
#define vtkSHYXImplicitCylinderRepresentation_h

#include "vtkImplicitCylinderRepresentation.h"
#include "vtkSHYXVascularStentPlacementModule.h"

class VTKSHYXVASCULARSTENTPLACEMENT_EXPORT vtkSHYXImplicitCylinderRepresentation
  : public vtkImplicitCylinderRepresentation
{
public:
    static vtkSHYXImplicitCylinderRepresentation* New();
    vtkTypeMacro(vtkSHYXImplicitCylinderRepresentation, vtkImplicitCylinderRepresentation);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /**
     * World-space finite cylinder length used for WidgetBounds (symmetric about Center along Axis).
     * Updated when dragging axis handles and when SetFiniteStentLengthHint is called from the UI layer.
     */
    vtkGetMacro(FiniteStentLength, double);
    void SetFiniteStentLengthHint(double L);

    void WidgetInteraction(double newEventPos[2]) override;
    void BuildRepresentation() override;

protected:
    vtkSHYXImplicitCylinderRepresentation();
    ~vtkSHYXImplicitCylinderRepresentation() override = default;

    /** Forward to vtkImplicitCylinderRepresentation so material defaults stay VTK-identical. */
    void CreateDefaultProperties() override;

    static void FiniteCylinderWorldAABB(
        const double center[3], const double axis[3], double radius, double length, double bds[6]);

    void ApplyFiniteLengthToWidgetBounds();

private:
    vtkSHYXImplicitCylinderRepresentation(const vtkSHYXImplicitCylinderRepresentation&) = delete;
    void operator=(const vtkSHYXImplicitCylinderRepresentation&) = delete;

    /** Side quads of a finite right circular cylinder [center-halfL, center+halfL] along axis (no box clip). */
    void BuildFiniteStentCylinder();

    double FiniteStentLength = 10.0;
};

#endif
