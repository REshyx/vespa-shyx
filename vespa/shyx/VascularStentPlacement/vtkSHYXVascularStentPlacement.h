/**
 * @class   vtkSHYXVascularStentPlacement
 * @brief   Moves vessel surface points to a target tubular radius along a centerline window.
 *
 * Port 0 is the vessel surface (vtkPolyData). Port 1 is a polyline centerline (vtkPolyData with
 * vtkLine / vtkPolyLine cells). Given an anchor vertex id on the centerline, the filter walks
 * along the line graph for half the stent length on each side of the anchor (when two branches
 * exist), builds a polyline segment, then for each input mesh point whose closest point lies on
 * that segment and within an internal influence band (multiple of StentRadius), replaces the
 * point with \f$ c + R \hat{r}_\perp \f$ where \f$c\f$ is the closest point on the axis,
 * \f$R\f$ is StentRadius, and \f$\hat{r}_\perp\f$ is the unit vector obtained by removing the
 * local tangent component from \f$(x-c)\f$.
 */

#ifndef vtkSHYXVascularStentPlacement_h
#define vtkSHYXVascularStentPlacement_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXVascularStentPlacementModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXVASCULARSTENTPLACEMENT_EXPORT vtkSHYXVascularStentPlacement : public vtkPolyDataAlgorithm
{
public:
    static vtkSHYXVascularStentPlacement* New();
    vtkTypeMacro(vtkSHYXVascularStentPlacement, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /** Centerline input (port 1). */
    void SetCenterlineConnection(vtkAlgorithmOutput* algOutput);

    vtkGetMacro(AnchorCenterlinePointId, vtkIdType);
    vtkSetClampMacro(AnchorCenterlinePointId, vtkIdType, 0, VTK_ID_MAX);

    vtkGetMacro(StentLength, double);
    vtkSetClampMacro(StentLength, double, 0.0, VTK_DOUBLE_MAX);

    vtkGetMacro(StentRadius, double);
    vtkSetClampMacro(StentRadius, double, 0.0, VTK_DOUBLE_MAX);

    /** Linked to the ImplicitCylinder 3D widget center (ParaView UI). */
    vtkGetVector3Macro(StentWidgetCenter, double);
    vtkSetVector3Macro(StentWidgetCenter, double);
    /** Linked to the ImplicitCylinder 3D widget axis (unit vector recommended). */
    vtkGetVector3Macro(StentWidgetAxis, double);
    vtkSetVector3Macro(StentWidgetAxis, double);

protected:
    vtkSHYXVascularStentPlacement();
    ~vtkSHYXVascularStentPlacement() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    vtkIdType AnchorCenterlinePointId = 0;
    double StentLength = 10.0;
    double StentRadius = 1.0;
    double StentWidgetCenter[3] = { 0.0, 0.0, 0.0 };
    double StentWidgetAxis[3] = { 0.0, 0.0, 1.0 };

private:
    vtkSHYXVascularStentPlacement(const vtkSHYXVascularStentPlacement&) = delete;
    void operator=(const vtkSHYXVascularStentPlacement&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
