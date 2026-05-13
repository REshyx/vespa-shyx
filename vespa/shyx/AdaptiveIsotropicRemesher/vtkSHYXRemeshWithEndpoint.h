/**
 * @class   vtkSHYXRemeshWithEndpoint
 * @brief   Extract cells with first array component &lt; 0 using vtkThreshold, then isotropic remesh
 *          that patch alone (Vespa ICC sizing).
 *
 * Intended for surfaces tagged by vtkCGALVesselEndClipper (cell \c EndpointIndex: cap triangles
 * &gt; 0, bulk typically -1). **vtkThreshold** + **vtkGeometryFilter** yield a standalone surface of
 * the negative side only; CGAL remeshes that surface in full (no patch-on-full-mesh bookkeeping).
 * Output is **only** the remeshed extracted patch (not the full vessel with caps). Same ICC sizing
 * stack as vtkSHYXAdaptiveIsotropicRemesher, without selection, feature detection, or mask logic.
 * CGAL split/collapse/flip follow CGAL defaults (all enabled); only protect/collapse/relax
 * constraint flags are exposed besides sizing and iteration controls.
 */

#ifndef vtkSHYXRemeshWithEndpoint_h
#define vtkSHYXRemeshWithEndpoint_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXAdaptiveIsotropicRemesherModule.h"

class vtkInformation;

class VTKSHYXADAPTIVEISOTROPICREMESHER_EXPORT vtkSHYXRemeshWithEndpoint : public vtkCGALPolyDataAlgorithm
{
public:
    static vtkSHYXRemeshWithEndpoint* New();
    vtkTypeMacro(vtkSHYXRemeshWithEndpoint, vtkCGALPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /**
     * Endpoint / marker array (input-array slot 0). Default name \c EndpointIndex on cell data.
     * Same name resolution as the adaptive remesher masks: cell array preferred when both exist.
     */
    void SetEndpointIndexArrayName(const char* name);
    const char* GetEndpointIndexArrayName();

    //@{
    /**
     * When the chosen array is point-centered: if true, every corner must have first component &lt; 0;
     * if false, one corner &lt; 0 suffices. Ignored for cell-centered arrays.
     */
    vtkGetMacro(EndpointIndexAllScalars, bool);
    vtkSetMacro(EndpointIndexAllScalars, bool);
    vtkBooleanMacro(EndpointIndexAllScalars, bool);
    //@}

    vtkGetMacro(MinEdgeLength, double);
    vtkSetMacro(MinEdgeLength, double);
    vtkGetMacro(MaxEdgeLength, double);
    vtkSetMacro(MaxEdgeLength, double);
    vtkGetMacro(AdaptiveTolerance, double);
    vtkSetMacro(AdaptiveTolerance, double);
    vtkGetMacro(AdaptiveSizingNeighborMaxRatio, double);
    vtkSetClampMacro(AdaptiveSizingNeighborMaxRatio, double, 0.0, 1.0e6);
    vtkGetMacro(ScaleToRange, bool);
    vtkSetMacro(ScaleToRange, bool);
    vtkBooleanMacro(ScaleToRange, bool);
    vtkGetMacro(RemeshRecomputeCurvatureEachIteration, bool);
    vtkSetMacro(RemeshRecomputeCurvatureEachIteration, bool);
    vtkBooleanMacro(RemeshRecomputeCurvatureEachIteration, bool);
    vtkGetMacro(NumberOfIterations, int);
    vtkSetMacro(NumberOfIterations, int);
    vtkGetMacro(NumberOfRelaxationSteps, int);
    vtkSetMacro(NumberOfRelaxationSteps, int);

    vtkGetMacro(RemeshProtectConstraints, bool);
    vtkSetMacro(RemeshProtectConstraints, bool);
    vtkBooleanMacro(RemeshProtectConstraints, bool);
    vtkGetMacro(RemeshCollapseConstraints, bool);
    vtkSetMacro(RemeshCollapseConstraints, bool);
    vtkBooleanMacro(RemeshCollapseConstraints, bool);
    vtkGetMacro(RemeshRelaxConstraints, bool);
    vtkSetMacro(RemeshRelaxConstraints, bool);
    vtkBooleanMacro(RemeshRelaxConstraints, bool);

protected:
    vtkSHYXRemeshWithEndpoint();
    ~vtkSHYXRemeshWithEndpoint() override = default;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    bool EndpointIndexAllScalars = false;

    double MinEdgeLength = 0.0;
    double MaxEdgeLength = 0.0;
    double AdaptiveTolerance = 0.01;
    double AdaptiveSizingNeighborMaxRatio = 1.6;
    bool ScaleToRange = false;
    bool RemeshRecomputeCurvatureEachIteration = true;
    int NumberOfIterations = 3;
    int NumberOfRelaxationSteps = 3;

    bool RemeshProtectConstraints = false;
    bool RemeshCollapseConstraints = true;
    bool RemeshRelaxConstraints = false;

private:
    vtkSHYXRemeshWithEndpoint(const vtkSHYXRemeshWithEndpoint&) = delete;
    void operator=(const vtkSHYXRemeshWithEndpoint&) = delete;
};

#endif
