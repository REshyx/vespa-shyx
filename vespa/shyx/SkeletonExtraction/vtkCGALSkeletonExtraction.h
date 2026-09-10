/**
 * @class   vtkCGALSkeletonExtraction
 * @brief   Extracts a curve skeleton from a closed triangulated surface mesh.
 *
 * vtkCGALSkeletonExtraction uses the CGAL Mean Curvature Flow Skeletonization
 * algorithm to extract a 1D curve skeleton from a closed 3D triangle mesh.
 * The output is a vtkPolyData containing polylines representing the skeleton.
 *
 * Optional post-process (AppendCapEndpoints): cells whose cap array (default
 * cell-data EndpointIndex) first component is greater than 0 are grouped into
 * connected patches. Each patch centroid is joined to the nearest point on the
 * skeleton (existing vertex, or a new vertex that splits an edge).
 * If that attach vertex has line degree > 1, the stub is a new branch (not a
 * leaf extension): the stub cell is marked 1 on cell-data NewCapBranch and a
 * warning is issued. Other cells are 0.
 */

#ifndef vtkCGALSkeletonExtraction_h
#define vtkCGALSkeletonExtraction_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXSkeletonExtractionModule.h" // For export macro

class vtkDataArray;

class VTKSHYXSKELETONEXTRACTION_EXPORT vtkCGALSkeletonExtraction : public vtkCGALPolyDataAlgorithm
{
public:
    static vtkCGALSkeletonExtraction* New();
    vtkTypeMacro(vtkCGALSkeletonExtraction, vtkCGALPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Maximum triangle angle (in degrees) used during the local remeshing step.
     * Triangles with an angle larger than this value may be split.
     * Default is 110 degrees (CGAL default).
     */
    vtkGetMacro(MaxTriangleAngle, double);
    vtkSetMacro(MaxTriangleAngle, double);
    ///@}

    ///@{
    /**
     * Minimum edge length used during the local remeshing step.
     * Edges shorter than this value may be collapsed.
     *
     * When <= 0, the filter uses 0.001 times the longest side of the input mesh AABB
     * (same scale as ParaView vtkSMBoundsDomain mode scaled_extent with scale_factor 0.001).
     * The stored default is 0 (meaning use that automatic length).
     */
    vtkGetMacro(MinEdgeLength, double);
    vtkSetMacro(MinEdgeLength, double);
    ///@}

    ///@{
    /**
     * Max number of iterations for the contraction step.
     * Default is 500.
     */
    vtkGetMacro(MaxIterations, int);
    vtkSetMacro(MaxIterations, int);
    ///@}

    ///@{
    /**
     * Max ratio of the surface area that the contracted mesh can reach
     * before the algorithm terminates. Default is 1e-4.
     */
    vtkGetMacro(AreaThreshold, double);
    vtkSetMacro(AreaThreshold, double);
    ///@}

    ///@{
    /**
     * Controls the contraction velocity and approximation quality (CGAL w_H).
     * Smaller values converge faster but can reduce skeleton quality.
     * Default is 0.1 (CGAL default).
     */
    vtkGetMacro(QualitySpeedTradeoff, double);
    vtkSetMacro(QualitySpeedTradeoff, double);
    ///@}

    ///@{
    /**
     * If true, medially centered skeleton is computed (higher quality
     * but slower). Default is true.
     */
    vtkGetMacro(MediallyCentered, bool);
    vtkSetMacro(MediallyCentered, bool);
    vtkBooleanMacro(MediallyCentered, bool);
    ///@}

    ///@{
    /**
     * Controls smoothness of the medial approximation (CGAL w_M).
     * Higher values produce a skeleton closer to the medial axis but may converge slower.
     * Only used if MediallyCentered is true.
     * Default is 0.2 (CGAL default).
     */
    vtkGetMacro(MediallyCenteredSpeedTradeoff, double);
    vtkSetMacro(MediallyCenteredSpeedTradeoff, double);
    ///@}

    ///@{
    /**
     * When ON, after skeleton extraction, each connected patch of cells whose
     * cap array (input-array slot 0, default cell EndpointIndex) first component
     * is > 0 is reduced to an area-weighted centroid and joined by a line to the
     * nearest point on the skeleton. Hits on the interior of an edge insert a
     * vertex and split that edge so the graph stays connected.
     * If the attach vertex already has line degree > 1, the stub is treated as a
     * new branch (not extending a leaf): cell-data NewCapBranch is 1 on that
     * stub and a warning is emitted. Other cells are 0.
     * Intended for capped meshes (e.g. after vtkCGALVesselEndClipper). Default OFF.
     */
    vtkGetMacro(AppendCapEndpoints, bool);
    vtkSetMacro(AppendCapEndpoints, bool);
    vtkBooleanMacro(AppendCapEndpoints, bool);
    ///@}

protected:
    vtkCGALSkeletonExtraction();
    ~vtkCGALSkeletonExtraction() override = default;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    void AppendCapEndpointsToOutput(
        vtkPolyData* surface, vtkDataArray* capArray, vtkPolyData* skeleton);

    double MaxTriangleAngle = 110.0;
    double MinEdgeLength    = 0.0;
    int    MaxIterations    = 500;
    double AreaThreshold    = 1e-4;
    double QualitySpeedTradeoff         = 0.1;
    bool   MediallyCentered = true;
    double MediallyCenteredSpeedTradeoff = 0.2;
    bool   AppendCapEndpoints = false;

private:
    vtkCGALSkeletonExtraction(const vtkCGALSkeletonExtraction&) = delete;
    void operator=(const vtkCGALSkeletonExtraction&)            = delete;
};

#endif
