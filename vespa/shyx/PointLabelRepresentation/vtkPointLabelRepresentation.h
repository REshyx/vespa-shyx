// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkPointLabelRepresentation_h
#define vtkPointLabelRepresentation_h

#include "vtkPointLabelRepresentationModule.h"
#include "vtkGeometryRepresentationWithFaces.h"
#include "vtkSmartPointer.h"
#include "vtkType.h"
#include "vtkWeakPointer.h"

class vtkActor;
class vtkCallbackCommand;
class vtkFastLabeledDataMapper;
class vtkMaskPoints;
class vtkDataSet;
class vtkMergeBlocks;
class vtkRenderer;
class vtkTextProperty;

/**
 * Surface-style representation with optional per-point text labels from a chosen point-data array.
 * Empty PointLabelArray (XML default) uses the active point scalars on the rendered geometry.
 * Optional VertexOnly restricts labels to points used by vertex cells (not line-only points).
 *
 * Labels are drawn with vtkFastLabeledDataMapper (one shader/atlas draw, not a vtkTextMapper per
 * point). When OccludeLabels is on, the label actor lives in the main renderer so it depth-tests
 * against geometry. When off (default), it lives on the non-composited overlay renderer (always
 * on top).
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
   * When enabled, labels are depth-tested and can be occluded by geometry. When disabled,
   * labels always draw on top as an overlay.
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

  /** Label text color (RGB in [0,1]). */
  virtual void SetLabelColor(double r, double g, double b);
  virtual void SetLabelColor(const double rgb[3]);
  vtkGetVector3Macro(LabelColor, double);

  /**
   * Polygon-offset units for occluded labels (negative pulls toward camera). Default -4, same
   * magnitude as ParaView Surface-With-Edges line offset. Only used when OccludeLabels is on.
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
  void PlaceLabelActor();
  void ApplyDepthOffset();
  void SetLabelSource(vtkDataSet* source);
  void ConfigurePointMaskSampling(vtkIdType numberOfInputPoints);
  void UpdateColoringParameters() override;
  static void OnWarningEvent(vtkObject*, unsigned long, void* clientdata, void*);

  bool ShouldDrawLabels();

  int ShowPointLabels = 1;
  int OccludeLabels = 0;
  int ShowEdges = 0;
  int VertexOnly = 1;
  char* PointLabelArray = nullptr;
  char* LabelFormat = nullptr;
  int MaximumNumberOfLabels = 2000;
  double LabelColor[3] = { 0.9, 0.9, 0.95 };
  double DepthOffset = -4.0;

  vtkMergeBlocks* MergeBlocks = nullptr;
  vtkMaskPoints* PointMask = nullptr;
  vtkFastLabeledDataMapper* LabelMapper = nullptr;
  vtkActor* LabelActor = nullptr;
  vtkTextProperty* LabelProperty = nullptr;
  vtkCallbackCommand* WarningObserver = nullptr;

  vtkWeakPointer<vtkRenderer> MainRenderer;
  vtkWeakPointer<vtkRenderer> OverlayRenderer;
  vtkWeakPointer<vtkDataSet> CachedDataSet;
  vtkMTimeType CachedDataMTime = 0;
  vtkMTimeType CachedPropMTime = 0;

  const char* GetEffectivePointLabelArrayName(vtkDataSet* mergedPiece) const;

private:
  vtkPointLabelRepresentation(const vtkPointLabelRepresentation&) = delete;
  void operator=(const vtkPointLabelRepresentation&) = delete;
};

#endif
