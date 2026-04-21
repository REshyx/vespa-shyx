// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkPointLabelRepresentation_h
#define vtkPointLabelRepresentation_h

#include "vtkPointLabelRepresentationModule.h"
#include "vtkGeometryRepresentationWithFaces.h"

class vtkActor;
class vtkActor2D;
class vtkCallbackCommand;
class vtkLabeledDataMapper;
class vtkMaskPoints;
class vtkDataSet;
class vtkMergeBlocks;
class vtkTextProperty;
class vtkTransform;

/**
 * Surface-style representation with optional per-point text labels from a chosen point-data array.
 * Empty PointLabelArray (XML default) uses the active point scalars on the rendered geometry.
 * Optional VertexOnly restricts labels to points used by vertex cells (not line-only points).
 * Values are formatted as strings (see vtkLabeledDataMapper) and drawn at point positions in world
 * space (non-composited overlay renderer), consistent with ParaView's data label representation.
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
  static void OnWarningEvent(vtkObject*, unsigned long, void* clientdata, void*);

  bool ShouldDrawLabels();

  int ShowPointLabels = 1;
  /** If non-zero, labels only points referenced by vertex cells (vtkPolyData::Verts or VTK_VERTEX). */
  int VertexOnly = 1;
  char* PointLabelArray = nullptr;
  char* LabelFormat = nullptr;
  int MaximumNumberOfLabels = 2000;

  vtkMergeBlocks* MergeBlocks = nullptr;
  vtkMaskPoints* PointMask = nullptr;
  vtkLabeledDataMapper* LabelMapper = nullptr;
  vtkActor2D* LabelActor = nullptr;
  vtkTextProperty* LabelProperty = nullptr;
  vtkTransform* LabelTransform = nullptr;
  vtkActor* TransformHelperProp = nullptr;
  vtkCallbackCommand* WarningObserver = nullptr;

  /** Resolved label array: explicit name, or active point scalars when unset / empty / "(Scalars)". */
  const char* GetEffectivePointLabelArrayName(vtkDataSet* mergedPiece) const;

private:
  vtkPointLabelRepresentation(const vtkPointLabelRepresentation&) = delete;
  void operator=(const vtkPointLabelRepresentation&) = delete;
};

#endif
