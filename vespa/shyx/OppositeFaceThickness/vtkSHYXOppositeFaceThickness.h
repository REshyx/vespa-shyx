/**
 * @class   vtkSHYXOppositeFaceThickness
 * @brief   For each mesh point, ray cast along \c -normal and record Euclidean distance to the first
 *          hit triangle that does not use the query vertex (shell thickness / opposite wall distance).
 *
 * Uses vtkTriangleFilter internally, vtkStaticCellLocator (BVH) for spatial queries, and
 * vtkSMPTools for parallel per-point work. Output is the input vtkPolyData shallow-copied with a new
 * point-data scalar array (default name \c OppositeFaceThickness). When \c ThinThicknessRecalculate
 * is on, vertices that pass the thin test are written at \c p + ThinThicknessRecalculateThreshold * n
 * on the output mesh (deep-copied points); thickness is recomputed from the new position when possible.
 *
 * @sa vtkStaticCellLocator vtkPolyDataNormals vtkSMPTools
 */

#ifndef vtkSHYXOppositeFaceThickness_h
#define vtkSHYXOppositeFaceThickness_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXOppositeFaceThicknessModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXOPPOSITEFACETHICKNESS_EXPORT vtkSHYXOppositeFaceThickness : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXOppositeFaceThickness* New();
  vtkTypeMacro(vtkSHYXOppositeFaceThickness, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Point vector array (3 components) defining the outward normal; cast is along \c -normal. */
  vtkSetStringMacro(NormalArrayName);
  vtkGetStringMacro(NormalArrayName);

  /** When the named normal array is missing, run vtkPolyDataNormals on the input (default on). */
  vtkSetMacro(AutoComputeNormals, int);
  vtkGetMacro(AutoComputeNormals, int);
  vtkBooleanMacro(AutoComputeNormals, int);

  /** Maximum ray length along \c -normal. If <= 0, uses 10× the bounding diagonal at execution time. */
  vtkSetMacro(MaxRayLength, double);
  vtkGetMacro(MaxRayLength, double);

  /** Offset start of the ray along \c +normal (outward) to reduce self-hits. If <= 0, uses
   *  1e-4× bounding diagonal at execution time. */
  vtkSetMacro(SurfaceOffset, double);
  vtkGetMacro(SurfaceOffset, double);

  /** Name of the output vtkDoubleArray on point data. */
  vtkSetStringMacro(ThicknessArrayName);
  vtkGetStringMacro(ThicknessArrayName);

  /** Locator intersection tolerance (passed to IntersectWithLine). */
  vtkSetMacro(RayTolerance, double);
  vtkGetMacro(RayTolerance, double);

  /** After a skipped hit, advance the segment start by this distance along the ray direction. */
  vtkSetMacro(RayAdvanceEpsilon, double);
  vtkGetMacro(RayAdvanceEpsilon, double);

  /** Upper bound on skipped hits (fan around vertex) per point. */
  vtkSetMacro(MaxRayIterations, int);
  vtkGetMacro(MaxRayIterations, int);

  /** When on, output \c vtkPoints are deep-copied; for each vertex with first-pass thickness finite,
   *  non-negative, and strictly less than ThinThicknessRecalculateThreshold, the output coordinate is
   *  set to \c p + threshold * (+unit normal), then thickness is ray-cast again from that position.
   *  When off, output geometry is a shallow copy of the input (positions unchanged). */
  vtkSetMacro(ThinThicknessRecalculate, int);
  vtkGetMacro(ThinThicknessRecalculate, int);
  vtkBooleanMacro(ThinThicknessRecalculate, int);

  /** With ThinThicknessRecalculate: upper bound on first thickness that triggers extrusion, and the
   *  extrusion length along +normal on the output mesh; same scalar for both. */
  vtkSetMacro(ThinThicknessRecalculateThreshold, double);
  vtkGetMacro(ThinThicknessRecalculateThreshold, double);

protected:
  vtkSHYXOppositeFaceThickness();
  ~vtkSHYXOppositeFaceThickness() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int AutoComputeNormals = 1;
  double MaxRayLength = 0.0;
  double SurfaceOffset = 0.0;
  double RayTolerance = 1e-12;
  double RayAdvanceEpsilon = 0.0;
  int MaxRayIterations = 64;
  int ThinThicknessRecalculate = 0;
  double ThinThicknessRecalculateThreshold = 0.0;
  char* NormalArrayName = nullptr;
  char* ThicknessArrayName = nullptr;

private:
  vtkSHYXOppositeFaceThickness(const vtkSHYXOppositeFaceThickness&) = delete;
  void operator=(const vtkSHYXOppositeFaceThickness&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
