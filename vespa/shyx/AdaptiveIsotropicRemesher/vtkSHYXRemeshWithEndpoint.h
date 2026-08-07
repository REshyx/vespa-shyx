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
 * Output port 0 is the remeshed extracted patch (not the full vessel with caps). Same ICC sizing
 * stack as vtkSHYXAdaptiveIsotropicRemesher, without selection, feature detection, or mask logic.
 * CGAL split/collapse/flip follow CGAL defaults (all enabled); only protect/collapse/relax
 * constraint flags are exposed besides sizing and iteration controls.
 *
 * After remesh (or sizing-only when wall remesh is off), port 0 point data includes sizing vs
 * realized edge length diagnostics: **VespaSizeGlobal** (target), **VespaMeanEdgeLength** /
 * **VespaMinEdgeLength**, and absolute deltas (**VespaEdgeLengthDelta**, **VespaMinEdgeLengthDelta**,
 * relative **VespaEdgeLengthRelDelta**). Cap interior vertices use \c v:vespa_cap_size when present.
 *
 * Multi-iteration note: wall remesh runs one CGAL \c isotropic_remeshing pass per iteration and may
 * call \c recompute_curvature between passes. After each remesh, CGAL \c Surface_mesh often retains
 * large amounts of removed-element garbage; a subsequent remesh without \c collect_garbage() can
 * hang for a very long time even when the mesh is combinatorially valid and the sizing field looks
 * healthy. This filter therefore calls \c collect_garbage() around remesh/recompute. See
 * AdaptiveIsotropicRemesher/README.md §3.
 */

#ifndef vtkSHYXRemeshWithEndpoint_h
#define vtkSHYXRemeshWithEndpoint_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXAdaptiveIsotropicRemesherModule.h"

#include <vector>

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
    /**
     * Wall remesh iteration count. Each count is one \c isotropic_remeshing(..., iterations=1).
     * When &gt; 1, Surface_mesh garbage is compacted between passes (see class comment / README §3);
     * chaining two filters with iterations=1 is not identical to one filter with iterations=2.
     */
    vtkGetMacro(NumberOfIterations, int);
    vtkSetMacro(NumberOfIterations, int);
    vtkGetMacro(NumberOfRelaxationSteps, int);
    vtkSetMacro(NumberOfRelaxationSteps, int);

    //@{
    /**
     * When ON (default), run CGAL isotropic remesh on the extracted wall patch; optional cap remesh
     * follows when EnableCapRemesh is ON. When OFF, skip wall remesh and all cap/hole-fill stages;
     * the ICC sizing field is still computed and written with length-mismatch diagnostics
     * (**VespaSizeGlobal**, **VespaEdgeLengthDelta**, …) on the output point data.
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
     * CGAL \c do_project for wall (and cap) isotropic remesh. When ON (default), vertices are
     * reprojected onto the input surface after creation/displacement. Turn OFF to skip projection.
     */
    vtkGetMacro(RemeshDoProject, bool);
    vtkSetMacro(RemeshDoProject, bool);
    vtkBooleanMacro(RemeshDoProject, bool);
    //@}

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
     * Expansion ratio per BFS hop from the seam for **cap** remesh sizing (not wall ICC
     * Expansion ratio). At seam vertices the target equals the average adjacent wall edge length;
     * each hop into the cap multiplies by this factor. **1.0** → uniform density like the wall;
     * **> 1** → progressively coarser toward the cap centre. No curvature field.
     *
     * Cap multi-iteration remesh also \c collect_garbage()'s between passes (same hang risk as wall).
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

    //@{
    /**
     * Static histogram of the **pre-remesh** uncapped ICC sizing field on the extracted wall
     * (same formula as VespaAdaptiveSizeGlobalUncapped). Filled once at the start of RequestData
     * and not refreshed across remesh iterations. Panel-only; not linked to Min/Max edge length.
     */
    static constexpr int UncappedSizeHistBinCount = 64;
    /** Manual getters (not vtkGetVectorMacro) so create-time SM info pulls can be logged. */
    double* GetUncappedSizeHistCenters();
    double* GetUncappedSizeHistCounts();
    double* GetUncappedSizeHistRange();
    int GetUncappedSizeHistSampleCount();
    //@}

protected:
    vtkSHYXRemeshWithEndpoint();
    ~vtkSHYXRemeshWithEndpoint() override = default;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    void ClearUncappedSizeHistogram();
    void FillUncappedSizeHistogram(const std::vector<double>& uncappedSizes);

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
    bool RemeshDoProject = true;

    bool EnableCapRemesh = true;
    double CapExpansionRatio = 1.6;
    int CapNumberOfIterations = 3;
    int CapNumberOfRelaxationSteps = 3;
    bool CapRemeshProtectConstraints = true;
    bool CapRefineSizingField = true;

    double UncappedSizeHistCenters[UncappedSizeHistBinCount] = {};
    double UncappedSizeHistCounts[UncappedSizeHistBinCount] = {};
    double UncappedSizeHistRange[2] = { 0.0, 0.0 };
    int UncappedSizeHistSampleCount = 0;

private:
    vtkSHYXRemeshWithEndpoint(const vtkSHYXRemeshWithEndpoint&) = delete;
    void operator=(const vtkSHYXRemeshWithEndpoint&) = delete;
};

#endif
