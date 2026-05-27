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

class vtkCellPicker;

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
    int ComputeInteractionState(int X, int Y, int modify = 0) override;
    void SetRepresentationState(int state) override;
    void BuildRepresentation() override;

protected:
    vtkSHYXImplicitCylinderRepresentation();
    ~vtkSHYXImplicitCylinderRepresentation() override;

    /** Forward to vtkImplicitCylinderRepresentation so material defaults stay VTK-identical. */
    void CreateDefaultProperties() override;

    static void FiniteCylinderWorldAABB(
        const double center[3], const double axis[3], double radius, double length, double bds[6]);

    void ApplyFiniteLengthToWidgetBounds();

    /** Drop axis lines from the handle picker; length uses end cones only. */
    void ConfigureStentPickers();

private:
    vtkSHYXImplicitCylinderRepresentation(const vtkSHYXImplicitCylinderRepresentation&) = delete;
    void operator=(const vtkSHYXImplicitCylinderRepresentation&) = delete;

    /** Side quads of a finite right circular cylinder [center-halfL, center+halfL] along axis (no box clip). */
    void BuildFiniteStentCylinder();

    double FiniteStentLength = 10.0;

    /** +1 when dragging ConeActor (+axis cap), -1 for ConeActor2 (-axis cap). */
    int FiniteStentLengthDragSign = 1;

    /** End-cap cones only; picked before the cylinder shell (pixel-sized handles). */
    vtkCellPicker* ConeCapPicker = nullptr;
};

#endif
