/**
 * @class   vtkCGALSkeletonExtraction
 * @brief   Extracts a curve skeleton from a closed triangulated surface mesh.
 *
 * vtkCGALSkeletonExtraction uses the CGAL Mean Curvature Flow Skeletonization
 * algorithm to extract a 1D curve skeleton from a closed 3D triangle mesh.
 * The output is a vtkPolyData containing polylines representing the skeleton.
 */

#ifndef vtkCGALSkeletonExtraction_h
#define vtkCGALSkeletonExtraction_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXSkeletonExtractionModule.h" // For export macro

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
     * If set to <= 0, the CGAL default (mesh-dependent) value is used.
     * Default is 0 (use CGAL default).
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

protected:
    vtkCGALSkeletonExtraction()           = default;
    ~vtkCGALSkeletonExtraction() override = default;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    double MaxTriangleAngle = 110.0;
    double MinEdgeLength    = 0.0;
    int    MaxIterations    = 500;
    double AreaThreshold    = 1e-4;
    double QualitySpeedTradeoff         = 0.1;
    bool   MediallyCentered = true;
    double MediallyCenteredSpeedTradeoff = 0.2;

private:
    vtkCGALSkeletonExtraction(const vtkCGALSkeletonExtraction&) = delete;
    void operator=(const vtkCGALSkeletonExtraction&)            = delete;
};

#endif
