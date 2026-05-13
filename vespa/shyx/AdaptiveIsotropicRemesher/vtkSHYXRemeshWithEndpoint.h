/**
 * @class   vtkSHYXRemeshWithEndpoint
 * @brief   Extract cells with first array component &lt; 0 using vtkThreshold, then isotropic remesh
 *          that patch alone (Vespa ICC sizing).
 *
 * Intended for surfaces tagged by vtkCGALVesselEndClipper (cell \c EndpointIndex: cap triangles
 * &gt; 0, bulk typically -1). **vtkThreshold** + **vtkGeometryFilter** yield a standalone surface of
 * the negative side only; optionally only the **largest** triangle-connected patch is kept.
 * CGAL remeshes that surface in full (no patch-on-full-mesh bookkeeping), unless wall remesh is
 * disabled to export the ICC sizing field on vertices only.
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

    //@{
    /**
     * When ON, after threshold + geometry + triangulation, keep only the largest surface-connected
     * component (vtkPolyDataConnectivityFilter, largest region). Default ON.
     */
    vtkGetMacro(LargestConnectedRegionOnly, bool);
    vtkSetMacro(LargestConnectedRegionOnly, bool);
    vtkBooleanMacro(LargestConnectedRegionOnly, bool);
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

    //@{
    /**
     * When ON (default), run CGAL isotropic remesh on the extracted wall patch; optional cap remesh
     * follows when EnableCapRemesh is ON. When OFF, skip wall remesh and all cap/hole-fill stages;
     * the ICC sizing field is still computed and written to output point data as **VespaSizeGlobal**
     * (same as CGAL `v:vespa_size_global`) on the unchanged extracted geometry for inspection.
     */
    vtkGetMacro(EnableWallRemesh, bool);
    vtkSetMacro(EnableWallRemesh, bool);
    vtkBooleanMacro(EnableWallRemesh, bool);
    //@}

    vtkGetMacro(RemeshProtectConstraints, bool);
    vtkSetMacro(RemeshProtectConstraints, bool);
    vtkBooleanMacro(RemeshProtectConstraints, bool);
    vtkGetMacro(RemeshCollapseConstraints, bool);
    vtkSetMacro(RemeshCollapseConstraints, bool);
    vtkBooleanMacro(RemeshCollapseConstraints, bool);
    vtkGetMacro(RemeshRelaxConstraints, bool);
    vtkSetMacro(RemeshRelaxConstraints, bool);
    vtkBooleanMacro(RemeshRelaxConstraints, bool);

    //@{
    /**
     * When ON (default), after wall remesh the open boundary loops are filled with
     * triangulate_refine_and_fair_hole (FairingContinuity = 0, C0), then the filled
     * cap patch is isotropic-remeshed with a uniform target edge length.
     */
    vtkGetMacro(EnableCapRemesh, bool);
    vtkSetMacro(EnableCapRemesh, bool);
    vtkBooleanMacro(EnableCapRemesh, bool);
    //@}

    /**
     * Expansion ratio per BFS hop from the seam for cap remesh sizing. At the seam the target
     * size equals the adjacent wall edge length; each hop into the cap multiplies by this factor.
     * Values > 1 produce a coarser mesh towards the cap centre; 1.0 gives uniform sizing.
     */
    vtkGetMacro(CapExpansionRatio, double);
    vtkSetClampMacro(CapExpansionRatio, double, 1.0, 1.0e6);

    vtkGetMacro(CapNumberOfIterations, int);
    vtkSetMacro(CapNumberOfIterations, int);
    vtkGetMacro(CapNumberOfRelaxationSteps, int);
    vtkSetMacro(CapNumberOfRelaxationSteps, int);

    /** Default ON: CGAL protect_constraints for the cap remesh (seam edges are never split/collapsed). */
    vtkGetMacro(CapRemeshProtectConstraints, bool);
    vtkSetMacro(CapRemeshProtectConstraints, bool);
    vtkBooleanMacro(CapRemeshProtectConstraints, bool);

    //@{
    /**
     * When ON, the BFS expansion field is recomputed from seam vertices before each
     * iteration after the first.  New vertices added by previous splits receive sizes
     * anchored to their actual hop-distance from the seam rather than interpolation drift.
     * Default ON (one BFS refresh per iteration after the first; turn OFF for a faster path when
     * interpolation drift is acceptable).
     */
    vtkGetMacro(CapRefineSizingField, bool);
    vtkSetMacro(CapRefineSizingField, bool);
    vtkBooleanMacro(CapRefineSizingField, bool);
    //@}

protected:
    vtkSHYXRemeshWithEndpoint();
    ~vtkSHYXRemeshWithEndpoint() override = default;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    bool EndpointIndexAllScalars = false;
    bool LargestConnectedRegionOnly = true;

    double MinEdgeLength = 0.0;
    double MaxEdgeLength = 0.0;
    double AdaptiveTolerance = 0.01;
    double AdaptiveSizingNeighborMaxRatio = 1.6;
    bool ScaleToRange = false;
    bool RemeshRecomputeCurvatureEachIteration = true;
    int NumberOfIterations = 3;
    int NumberOfRelaxationSteps = 3;
    bool EnableWallRemesh = true;

    bool RemeshProtectConstraints = false;
    bool RemeshCollapseConstraints = true;
    bool RemeshRelaxConstraints = false;

    bool EnableCapRemesh = true;
    double CapExpansionRatio = 1.5;
    int CapNumberOfIterations = 3;
    int CapNumberOfRelaxationSteps = 3;
    bool CapRemeshProtectConstraints = true;
    bool CapRefineSizingField = true;

private:
    vtkSHYXRemeshWithEndpoint(const vtkSHYXRemeshWithEndpoint&) = delete;
    void operator=(const vtkSHYXRemeshWithEndpoint&) = delete;
};

#endif
