/**
 * @class   vtkSHYXImageTeasarCenterline
 * @brief   TEASAR centerlines from a vtkImageData mask or label volume.
 *
 * Foreground voxels (threshold or integer labels) are skeletonized per
 * 26-connected component. The curve is the penalized shortest path used by
 * TEASAR (Sato, Bitter, Kaufman): distance-to-boundary keeps the path off
 * the wall, and a normalized distance-from-root term breaks ties in wide
 * regions. Each path invalidates a cube of physical radius
 * scale * distanceToBoundary + const. With FixBranching, later paths travel
 * the existing skeleton at zero cost so the fork sits closer to the branch.
 *
 * Output is vtkPolyData polylines. Point data MaximumInscribedSphereRadius
 * is the physical distance to the nearest non-foreground voxel. Ends are
 * moved from the cap rim to the centroid of the farthest voxel shell so a
 * flat opening is met at its center. Loops become trees.
 */

#ifndef vtkSHYXImageTeasarCenterline_h
#define vtkSHYXImageTeasarCenterline_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXImageTeasarCenterlineModule.h"
#include "vtkType.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXIMAGETEASARCENTERLINE_EXPORT vtkSHYXImageTeasarCenterline
  : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXImageTeasarCenterline* New();
  vtkTypeMacro(vtkSHYXImageTeasarCenterline, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum ForegroundModeType
  {
    THRESHOLD = 0,
    LABEL = 1
  };

  vtkSetClampMacro(ForegroundMode, int, THRESHOLD, LABEL);
  vtkGetMacro(ForegroundMode, int);

  vtkSetMacro(Threshold, double);
  vtkGetMacro(Threshold, double);

  /** Label mode only. 0 = every positive integer label. */
  vtkSetMacro(LabelValue, int);
  vtkGetMacro(LabelValue, int);

  vtkSetClampMacro(Scale, double, 0.0, 1.0e6);
  vtkGetMacro(Scale, double);

  vtkSetClampMacro(ConstRadius, double, 0.0, 1.0e12);
  vtkGetMacro(ConstRadius, double);

  vtkSetClampMacro(PdrfScale, double, 0.0, 1.0e12);
  vtkGetMacro(PdrfScale, double);

  vtkSetClampMacro(PdrfExponent, int, 1, 32);
  vtkGetMacro(PdrfExponent, int);

  vtkSetClampMacro(DustThreshold, int, 0, VTK_INT_MAX);
  vtkGetMacro(DustThreshold, int);

  /** Per component. 0 = no limit. */
  vtkSetClampMacro(MaxPaths, int, 0, VTK_INT_MAX);
  vtkGetMacro(MaxPaths, int);

  vtkSetMacro(FixBranching, vtkTypeBool);
  vtkGetMacro(FixBranching, vtkTypeBool);
  vtkBooleanMacro(FixBranching, vtkTypeBool);

  vtkSetMacro(FixBorders, vtkTypeBool);
  vtkGetMacro(FixBorders, vtkTypeBool);
  vtkBooleanMacro(FixBorders, vtkTypeBool);

protected:
  vtkSHYXImageTeasarCenterline();
  ~vtkSHYXImageTeasarCenterline() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int RequestUpdateExtent(vtkInformation* request, vtkInformationVector** inputVector,
    vtkInformationVector* outputVector) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int ForegroundMode = THRESHOLD;
  double Threshold = 0.5;
  int LabelValue = 0;
  double Scale = 1.5;
  double ConstRadius = 0.0;
  double PdrfScale = 100000.0;
  int PdrfExponent = 4;
  int DustThreshold = 0;
  int MaxPaths = 0;
  vtkTypeBool FixBranching = 1;
  vtkTypeBool FixBorders = 1;

private:
  vtkSHYXImageTeasarCenterline(const vtkSHYXImageTeasarCenterline&) = delete;
  void operator=(const vtkSHYXImageTeasarCenterline&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
