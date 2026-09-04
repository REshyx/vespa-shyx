/**
 * @class   vtkSHYXSkeletonEndClipper
 * @brief   Skeleton extraction followed by vessel-end clipping (one input).
 *
 * Combines vtkCGALSkeletonExtraction and vtkCGALVesselEndClipper. The two
 * standalone filters are unchanged. Input is a closed vessel surface.
 *
 * Outputs:
 *   - Port 0: clipped (and optionally capped) surface, same as End Clipper port 0.
 *   - Port 1: skeleton polylines plus clip-plane labels (verts) and short
 *     direction lines. Clip-plane points are stored first (2 per endpoint) so
 *     the existing End Clipper plane widget can read origins/handles.
 */

#ifndef vtkSHYXSkeletonEndClipper_h
#define vtkSHYXSkeletonEndClipper_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXSkeletonEndClipperModule.h"

#include <vtkDataArraySelection.h>
#include <vtkSmartPointer.h>

class vtkCGALSkeletonExtraction;
class vtkCGALVesselEndClipper;

class VTKSHYXSKELETONENDCLIPPER_EXPORT vtkSHYXSkeletonEndClipper : public vtkCGALPolyDataAlgorithm
{
public:
    static vtkSHYXSkeletonEndClipper* New();
    vtkTypeMacro(vtkSHYXSkeletonEndClipper, vtkCGALPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Skeleton extraction (vtkCGALSkeletonExtraction).
     */
    vtkGetMacro(MaxTriangleAngle, double);
    vtkSetMacro(MaxTriangleAngle, double);

    vtkGetMacro(MinEdgeLength, double);
    vtkSetMacro(MinEdgeLength, double);

    vtkGetMacro(MaxIterations, int);
    vtkSetMacro(MaxIterations, int);

    vtkGetMacro(AreaThreshold, double);
    vtkSetMacro(AreaThreshold, double);

    vtkGetMacro(QualitySpeedTradeoff, double);
    vtkSetMacro(QualitySpeedTradeoff, double);

    vtkGetMacro(MediallyCentered, bool);
    vtkSetMacro(MediallyCentered, bool);
    vtkBooleanMacro(MediallyCentered, bool);

    vtkGetMacro(MediallyCenteredSpeedTradeoff, double);
    vtkSetMacro(MediallyCenteredSpeedTradeoff, double);
    ///@}

    ///@{
    /**
     * End clipper (vtkCGALVesselEndClipper).
     */
    vtkGetMacro(ClipOffset, double);
    vtkSetMacro(ClipOffset, double);

    vtkGetMacro(MinBranchLength, double);
    vtkSetMacro(MinBranchLength, double);

    vtkGetMacro(TangentDepth, int);
    vtkSetClampMacro(TangentDepth, int, 1, 10);

    vtkGetMacro(CapEndpoints, bool);
    vtkSetMacro(CapEndpoints, bool);
    vtkBooleanMacro(CapEndpoints, bool);

    vtkGetMacro(FairingContinuity, int);
    vtkSetClampMacro(FairingContinuity, int, 0, 2);
    ///@}

    vtkDataArraySelection* GetEndpointSelection();

    vtkMTimeType GetMTime() override;

    vtkGetStringMacro(InteractiveCutPackedString);
    vtkSetStringMacro(InteractiveCutPackedString);

    vtkGetMacro(UseInteractiveCutPlanes, bool);
    void SetUseInteractiveCutPlanes(bool flag);
    vtkBooleanMacro(UseInteractiveCutPlanes, bool);

    vtkGetStringMacro(OutputMessage);

protected:
    vtkSHYXSkeletonEndClipper();
    ~vtkSHYXSkeletonEndClipper() override;

    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    void SetOutputMessageNoModified(const char* msg);
    void ApplyParametersToInnerFilters();

    double MaxTriangleAngle              = 110.0;
    double MinEdgeLength                 = 0.0;
    int    MaxIterations                 = 500;
    double AreaThreshold                 = 1e-4;
    double QualitySpeedTradeoff          = 0.1;
    bool   MediallyCentered              = true;
    double MediallyCenteredSpeedTradeoff = 0.2;

    double ClipOffset        = 0.0;
    double MinBranchLength   = 0.0;
    int    TangentDepth      = 1;
    bool   CapEndpoints      = true;
    int    FairingContinuity = 0;

    char* InteractiveCutPackedString = nullptr;
    bool  UseInteractiveCutPlanes    = false;
    char* OutputMessage              = nullptr;

    vtkSmartPointer<vtkCGALSkeletonExtraction> SkeletonFilter;
    vtkSmartPointer<vtkCGALVesselEndClipper>   ClipperFilter;

private:
    vtkSHYXSkeletonEndClipper(const vtkSHYXSkeletonEndClipper&) = delete;
    void operator=(const vtkSHYXSkeletonEndClipper&)            = delete;
};

#endif
