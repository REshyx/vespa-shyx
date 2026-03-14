/**
 * @class   vtkCGALVesselEndClipper
 * @brief   Clips vessel tips at skeleton endpoints to create flat cross-sections.
 *
 * vtkCGALVesselEndClipper takes a vascular surface mesh and its extracted
 * centerline (e.g. from vtkCGALSkeletonExtraction) as inputs. It identifies
 * the leaf nodes (degree-1 vertices) of the centerline graph and clips the
 * vessel surface at each endpoint with a plane perpendicular to the local
 * centerline direction. The result is a surface mesh with flat-cut ends,
 * suitable for CFD inlet/outlet boundary conditions.
 *
 * Each discovered endpoint appears as a checkable item in the Properties
 * panel (via vtkDataArraySelection). Only checked endpoints are clipped.
 */

#ifndef vtkCGALVesselEndClipper_h
#define vtkCGALVesselEndClipper_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro
#include <vtkDataArraySelection.h>
#include <vtkSmartPointer.h>

class VTKCGALPMP_EXPORT vtkCGALVesselEndClipper : public vtkCGALPolyDataAlgorithm
{
public:
    static vtkCGALVesselEndClipper* New();
    vtkTypeMacro(vtkCGALVesselEndClipper, vtkCGALPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /**
     * Set the centerline input connection (port 1).
     * The centerline should be a vtkPolyData containing lines,
     * typically produced by vtkCGALSkeletonExtraction.
     */
    void SetCenterlineConnection(vtkAlgorithmOutput* algOutput);

    ///@{
    /**
     * Offset distance to shift each cutting plane along the centerline
     * tangent direction. Positive values move the plane outward (further
     * from the vessel body), negative values move it inward.
     * Default is 0.0.
     */
    vtkGetMacro(ClipOffset, double);
    vtkSetMacro(ClipOffset, double);
    ///@}

    ///@{
    /**
     * Number of skeleton edges to walk inward from each leaf node
     * when estimating the local tangent direction for the cutting
     * plane.  Larger values produce a smoother, more averaged
     * tangent; smaller values follow the skeleton more locally.
     * Default is 1.  Range [1, 10].
     */
    vtkGetMacro(TangentDepth, int);
    vtkSetClampMacro(TangentDepth, int, 1, 10);
    ///@}

    ///@{
    /**
     * If true, the resulting holes from clipping are capped (filled)
     * to produce a closed, watertight mesh suitable for CFD.
     * Requires the input vessel mesh to be closed.
     * Default is true.
     */
    vtkGetMacro(CapEndpoints, bool);
    vtkSetMacro(CapEndpoints, bool);
    vtkBooleanMacro(CapEndpoints, bool);
    ///@}

    /**
     * Returns the endpoint selection object. Each discovered leaf
     * endpoint is registered as a named entry that the user can
     * enable (clip) or disable (skip) in the ParaView UI.
     */
    vtkDataArraySelection* GetEndpointSelection();

    vtkMTimeType GetMTime() override;

protected:
    vtkCGALVesselEndClipper();
    ~vtkCGALVesselEndClipper() override = default;

    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    double ClipOffset   = 0.0;
    int    TangentDepth = 1;
    bool   CapEndpoints = true;

    vtkSmartPointer<vtkDataArraySelection> EndpointSelection;

private:
    vtkCGALVesselEndClipper(const vtkCGALVesselEndClipper&) = delete;
    void operator=(const vtkCGALVesselEndClipper&)          = delete;
};

#endif
