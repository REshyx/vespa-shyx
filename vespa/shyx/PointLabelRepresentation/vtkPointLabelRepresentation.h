// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkPointLabelRepresentation_h
#define vtkPointLabelRepresentation_h

#include "vtkPointLabelRepresentationModule.h"
#include "vtkGeometryRepresentationWithFaces.h"
#include "vtkSmartPointer.h"
#include "vtkWeakPointer.h"

#include <vector>

class vtkActor;
class vtkActor2D;
class vtkBillboardTextActor3D;
class vtkCallbackCommand;
class vtkLabeledDataMapper;
class vtkMaskPoints;
class vtkDataSet;
class vtkMergeBlocks;
class vtkRenderer;
class vtkTextProperty;
class vtkTransform;

/**
 * Surface-style representation with optional per-point text labels from a chosen point-data array.
 * Empty PointLabelArray (XML default) uses the active point scalars on the rendered geometry.
 * Optional VertexOnly restricts labels to points used by vertex cells (not line-only points).
 * Values are formatted as strings (see vtkLabeledDataMapper).
 *
 * When OccludeLabels is on (default), labels are drawn as 3D billboards in the main renderer so they
 * participate in depth testing and translucent blending (depth peeling). A small polygon-offset
 * toward the camera reduces z-fighting with coincident surface geometry, similar to Surface With
 * Edges line offsets and selection highlights. When OccludeLabels is off, labels use a 2D overlay
 * on the non-composited renderer (always on top), matching ParaView's data label representation.
 */
class VTKPOINTLABELREPRESENTATION_EXPORT vtkPointLabelRepresentation
  : public vtkGeometryRepresentationWithFaces
{
public:
  static vtkPointLabelRepresentation* New();
  vtkTypeMacro(vtkPointLabelRepresentation, vtkGeometryRepresentationWithFaces);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Enable/disable drawing point labels (int 0/1 for ParaView SM). */
  virtual void SetShowPointLabels(int val);
  virtual int GetShowPointLabels();
  virtual void ShowPointLabelsOn();
  virtual void ShowPointLabelsOff();

  /**
   * When enabled (default), labels are depth-tested and can be occluded by geometry, and blend with
   * translucent objects. When disabled, labels always draw on top as a 2D overlay.
   */
  virtual void SetOccludeLabels(int val);
  virtual int GetOccludeLabels();
  virtual void OccludeLabelsOn();
  virtual void OccludeLabelsOff();

  /**
   * When enabled, draw surface edges like Surface With Edges (vtkProperty::EdgeVisibility).
   * Default off. Edge color uses the shared Surface EdgeColor property.
   */
  virtual void SetShowEdges(int val);
  virtual int GetShowEdges();
  virtual void ShowEdgesOn();
  virtual void ShowEdgesOff();

  virtual void SetVertexOnly(int val);
  virtual int GetVertexOnly();
  virtual void VertexOnlyOn();
  virtual void VertexOnlyOff();

  virtual void SetPointLabelArray(const char*);
  vtkGetStringMacro(PointLabelArray);

  virtual void SetMaximumNumberOfLabels(int val);
  vtkGetMacro(MaximumNumberOfLabels, int);

  virtual void SetLabelFormat(const char* format);
  vtkGetStringMacro(LabelFormat);

  /** Label text color (RGB in [0,1]). Applies to both overlay and occluded billboard labels. */
  virtual void SetLabelColor(double r, double g, double b);
  virtual void SetLabelColor(const double rgb[3]);
  vtkGetVector3Macro(LabelColor, double);

  /**
   * Polygon-offset units for occluded labels (negative pulls toward camera). Default -4, same
   * magnitude as ParaView Surface-With-Edges line offset. More negative reduces z-fighting with
   * coincident surfaces; values near 0 allow more fighting. Only used when OccludeLabels is on.
   */
  virtual void SetDepthOffset(double units);
  vtkGetMacro(DepthOffset, double);

  void SetVisibility(bool val) override;

  void SetOrientation(double x, double y, double z) override;
  void SetOrigin(double x, double y, double z) override;
  void SetPosition(double x, double y, double z) override;
  void SetScale(double x, double y, double z) override;
  void SetUserTransform(const double matrix[16]) override;

protected:
  vtkPointLabelRepresentation();
  ~vtkPointLabelRepresentation() override;

  int ProcessViewRequest(
    vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo) override;

  bool AddToView(vtkView* view) override;
  bool RemoveFromView(vtkView* view) override;

  void UpdateLabelTransform();
  void SyncOccludedLabelActors();
  void HideOccludedLabelActors();
  void ApplyDepthOffsetToBillboard(vtkBillboardTextActor3D* bb);
  void ApplyDepthOffsetToAllBillboards();
  /** Wire LabelMapper: bypass MaskPoints when every point fits under Max. */
  void SetLabelSource(vtkDataSet* source);
  /** Configure MaskPoints when subsampling is required. */
  void ConfigurePointMaskSampling(vtkIdType numberOfInputPoints);
  void UpdateColoringParameters() override;
  static void OnWarningEvent(vtkObject*, unsigned long, void* clientdata, void*);

  bool ShouldDrawLabels();

  int ShowPointLabels = 1;
  /** If non-zero, depth-test labels against scene geometry (default). */
  int OccludeLabels = 1;
  /** If non-zero, draw edges like Surface With Edges (default off). */
  int ShowEdges = 0;
  /** If non-zero, labels only points referenced by vertex cells (vtkPolyData::Verts or VTK_VERTEX). */
  int VertexOnly = 1;
  char* PointLabelArray = nullptr;
  char* LabelFormat = nullptr;
  int MaximumNumberOfLabels = 2000;
  double LabelColor[3] = { 0.9, 0.9, 0.95 };
  /** Relative coincident polygon-offset units for occluded label quads (toward camera if negative). */
  double DepthOffset = -4.0;

  vtkMergeBlocks* MergeBlocks = nullptr;
  vtkMaskPoints* PointMask = nullptr;
  vtkLabeledDataMapper* LabelMapper = nullptr;
  vtkActor2D* LabelActor = nullptr;
  vtkTextProperty* LabelProperty = nullptr;
  vtkTransform* LabelTransform = nullptr;
  vtkActor* TransformHelperProp = nullptr;
  vtkCallbackCommand* WarningObserver = nullptr;

  /** Main renderer for occluded 3D billboards (not via vtkPropAssembly: it clears Position via PokeMatrix). */
  vtkWeakPointer<vtkRenderer> MainRenderer;
  std::vector<vtkSmartPointer<vtkBillboardTextActor3D>> BillboardActors;

  /** Resolved label array: explicit name, or active point scalars when unset / empty / "(Scalars)". */
  const char* GetEffectivePointLabelArrayName(vtkDataSet* mergedPiece) const;

private:
  vtkPointLabelRepresentation(const vtkPointLabelRepresentation&) = delete;
  void operator=(const vtkPointLabelRepresentation&) = delete;
};

#endif
