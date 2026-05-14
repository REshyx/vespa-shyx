/**
 * @class   vtkSHYXImplicitCylinderRepresentation
 * @brief   Implicit cylinder representation where axis handles adjust finite length instead of rotating the axis.
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

protected:
    vtkSHYXImplicitCylinderRepresentation();
    ~vtkSHYXImplicitCylinderRepresentation() override = default;

    static void FiniteCylinderWorldAABB(
        const double center[3], const double axis[3], double radius, double length, double bds[6]);

    void ApplyFiniteLengthToWidgetBounds();

private:
    vtkSHYXImplicitCylinderRepresentation(const vtkSHYXImplicitCylinderRepresentation&) = delete;
    void operator=(const vtkSHYXImplicitCylinderRepresentation&) = delete;

    double FiniteStentLength = 10.0;
};

#endif
