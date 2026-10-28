/**
 * @class   vtkSHYXCameraAdjust
 * @brief   Store named camera presets in a pipeline source.
 *
 * A source (no input) that keeps a list of camera presets in its own properties, so the camera
 * configuration travels with the pipeline branch and the ParaView state instead of living in a
 * global view / script variable. The properties are newline-separated lists aligned by row
 * (empty lines keep the row):
 *
 *   CameraNames         preset name (one per line)
 *   CameraPositions     x,y,z (view camera position)
 *   CameraFocalPoints   x,y,z (view camera focal point)
 *   CameraViewUps       x,y,z (view camera up vector)
 *   CameraLens          viewAngle,parallelScale,parallelProjection(0/1)
 *
 * The client-side Cameras table (pqSHYXCameraAdjustWidget) is the normal way to edit them:
 * Add captures the active RenderView camera, Update overwrites a row from the current camera,
 * Delete removes rows, and Apply sets the active RenderView camera from the selected row.
 *
 * The output is a vtkPolyData with one vertex per preset at CameraPositions, plus point data
 * CameraIndex (int) and CameraName (string) and field data CameraPositions / CameraFocalPoints /
 * CameraViewUps / CameraLens. It is meant as a compact "where are my presets" preview: the XML
 * hint shows it with Point Gaussian, and Point Label can label the vertices with CameraName.
 * The output does not affect any camera by itself.
 */

#ifndef vtkSHYXCameraAdjust_h
#define vtkSHYXCameraAdjust_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXCameraAdjustModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXCAMERAADJUST_EXPORT vtkSHYXCameraAdjust : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXCameraAdjust* New();
  vtkTypeMacro(vtkSHYXCameraAdjust, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Newline-separated preset names, in table order. Empty lines are kept as unnamed rows. */
  vtkSetStringMacro(CameraNames);
  vtkGetStringMacro(CameraNames);

  /** Newline-separated camera positions, one "x,y,z" per row (same row order as CameraNames). */
  vtkSetStringMacro(CameraPositions);
  vtkGetStringMacro(CameraPositions);

  /** Newline-separated camera focal points, one "x,y,z" per row. */
  vtkSetStringMacro(CameraFocalPoints);
  vtkGetStringMacro(CameraFocalPoints);

  /** Newline-separated camera up vectors, one "x,y,z" per row. */
  vtkSetStringMacro(CameraViewUps);
  vtkGetStringMacro(CameraViewUps);

  /**
   * Newline-separated lens settings, one "viewAngle,parallelScale,parallelProjection" per row.
   * parallelProjection is 0 (perspective) or 1 (parallel). Missing lines default to
   * 30,1,0.
   */
  vtkSetStringMacro(CameraLens);
  vtkGetStringMacro(CameraLens);

protected:
  vtkSHYXCameraAdjust();
  ~vtkSHYXCameraAdjust() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  char* CameraNames       = nullptr;
  char* CameraPositions   = nullptr;
  char* CameraFocalPoints = nullptr;
  char* CameraViewUps     = nullptr;
  char* CameraLens        = nullptr;

private:
  vtkSHYXCameraAdjust(const vtkSHYXCameraAdjust&) = delete;
  void operator=(const vtkSHYXCameraAdjust&)      = delete;
};

VTK_ABI_NAMESPACE_END
#endif
