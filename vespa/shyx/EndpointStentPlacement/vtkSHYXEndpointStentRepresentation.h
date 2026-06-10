/**
 * @class   vtkSHYXEndpointStentRepresentation
 * @brief   Distance-widget representation with a centerline-following wireframe stent preview.
 *
 * Extends vtkDistanceRepresentation2D (ParaView distance widget overlay + handles) with 3D actors
 * that draw a finite-radius diamond-mesh stent tube along a supplied centerline path polyline.
 */

#ifndef vtkSHYXEndpointStentRepresentation_h
#define vtkSHYXEndpointStentRepresentation_h

#include "vtkDistanceRepresentation2D.h"
#include "vtkSHYXEndpointStentPlacementModule.h"

VTK_ABI_NAMESPACE_BEGIN

class vtkPolyData;
class vtkPolyDataMapper;
class vtkActor;

class VTKSHYXENDPOINTSTENTPLACEMENT_EXPORT vtkSHYXEndpointStentRepresentation
  : public vtkDistanceRepresentation2D
{
public:
    static vtkSHYXEndpointStentRepresentation* New();
    vtkTypeMacro(vtkSHYXEndpointStentRepresentation, vtkDistanceRepresentation2D);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    vtkSetMacro(StentRadius, double);
    vtkGetMacro(StentRadius, double);
    vtkSetMacro(ShowStentPreview, vtkTypeBool);
    vtkGetMacro(ShowStentPreview, vtkTypeBool);

    /** Copy path geometry (vtkPolyLine / vtkLine points in order). */
    void SetStentPathPolyData(vtkPolyData* path);

    void BuildRepresentation() override;

    void ReleaseGraphicsResources(vtkWindow* w) override;
    int RenderOpaqueGeometry(vtkViewport* viewport) override;
    int RenderTranslucentPolygonalGeometry(vtkViewport* viewport) override;
    double* GetBounds() override;

protected:
    vtkSHYXEndpointStentRepresentation();
    ~vtkSHYXEndpointStentRepresentation() override;

    void BuildStentPreviewMesh();

    double StentRadius = 0.0;
    vtkTypeBool ShowStentPreview = 1;

    vtkPolyData* PathPoly = nullptr;
    vtkPolyData* WirePoly = nullptr;
    vtkPolyData* ShellPoly = nullptr;
    vtkPolyDataMapper* WireMapper = nullptr;
    vtkPolyDataMapper* ShellMapper = nullptr;
    vtkActor* WireActor = nullptr;
    vtkActor* ShellActor = nullptr;

private:
    vtkSHYXEndpointStentRepresentation(const vtkSHYXEndpointStentRepresentation&) = delete;
    void operator=(const vtkSHYXEndpointStentRepresentation&) = delete;
};

VTK_ABI_NAMESPACE_END

#endif
