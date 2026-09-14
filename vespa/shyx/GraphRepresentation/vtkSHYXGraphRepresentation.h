// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkSHYXGraphRepresentation_h
#define vtkSHYXGraphRepresentation_h

#include "vtkSHYXGraphRepresentationModule.h"
#include "vtkGeometryRepresentationWithFaces.h"
#include "vtkSmartPointer.h"
#include "vtkWeakPointer.h"

class vtkActor;
class vtkActor2D;
class vtkCallbackCommand;
class vtkCellCenters;
class vtkDataObject;
class vtkDataSet;
class vtkExtractEdges;
class vtkGeometryFilter;
class vtkLabeledDataMapper;
class vtkMaskPoints;
class vtkMergeBlocks;
class vtkPointGaussianMapper;
class vtkPolyDataMapper;
class vtkRenderer;
class vtkTextProperty;
class vtkTransform;

/**
 * Display representation that visualizes a mesh as a graph of vertices, lines, faces,
 * and volumes. Vertex cells (or all points when AllPoint is on) are camera-facing disks
 * with numbers; 1D/2D/3D cells are drawn with labels at cell centers. Volumes use outer
 * faces plus a wireframe of all unique edges (selection-style), not volume raycasting.
 *
 * The default Surface actor is hidden; only the enabled entity layers are drawn.
 * Empty array names (or "IDs") label with point/cell ids.
 */
class VTKSHYXGRAPHREPRESENTATION_EXPORT vtkSHYXGraphRepresentation
  : public vtkGeometryRepresentationWithFaces
{
public:
  static vtkSHYXGraphRepresentation* New();
  vtkTypeMacro(vtkSHYXGraphRepresentation, vtkGeometryRepresentationWithFaces);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetClampMacro(ShowVertex, int, 0, 1);
  vtkGetMacro(ShowVertex, int);
  vtkBooleanMacro(ShowVertex, int);

  vtkSetClampMacro(AllPoint, int, 0, 1);
  vtkGetMacro(AllPoint, int);
  vtkBooleanMacro(AllPoint, int);

  vtkSetClampMacro(ShowLine, int, 0, 1);
  vtkGetMacro(ShowLine, int);
  vtkBooleanMacro(ShowLine, int);

  vtkSetClampMacro(ShowFace, int, 0, 1);
  vtkGetMacro(ShowFace, int);
  vtkBooleanMacro(ShowFace, int);

  vtkSetClampMacro(ShowVolume, int, 0, 1);
  vtkGetMacro(ShowVolume, int);
  vtkBooleanMacro(ShowVolume, int);

  virtual void SetVertexArray(const char*);
  vtkGetStringMacro(VertexArray);
  virtual void SetLineArray(const char*);
  vtkGetStringMacro(LineArray);
  virtual void SetFaceArray(const char*);
  vtkGetStringMacro(FaceArray);
  virtual void SetVolumeArray(const char*);
  vtkGetStringMacro(VolumeArray);

  virtual void SetVertexColor(double r, double g, double b);
  virtual void SetVertexColor(const double rgb[3]);
  vtkGetVector3Macro(VertexColor, double);
  vtkSetClampMacro(VertexOpacity, double, 0.0, 1.0);
  vtkGetMacro(VertexOpacity, double);
  vtkSetClampMacro(VertexScale, double, 0.0, 1000.0);
  vtkGetMacro(VertexScale, double);

  virtual void SetLineColor(double r, double g, double b);
  virtual void SetLineColor(const double rgb[3]);
  vtkGetVector3Macro(LineColor, double);
  vtkSetClampMacro(LineOpacity, double, 0.0, 1.0);
  vtkGetMacro(LineOpacity, double);

  virtual void SetFaceColor(double r, double g, double b);
  virtual void SetFaceColor(const double rgb[3]);
  vtkGetVector3Macro(FaceColor, double);
  vtkSetClampMacro(FaceOpacity, double, 0.0, 1.0);
  vtkGetMacro(FaceOpacity, double);

  virtual void SetVolumeColor(double r, double g, double b);
  virtual void SetVolumeColor(const double rgb[3]);
  vtkGetVector3Macro(VolumeColor, double);
  vtkSetClampMacro(VolumeOpacity, double, 0.0, 1.0);
  vtkGetMacro(VolumeOpacity, double);

  virtual void SetMaximumNumberOfLabels(int n);
  vtkGetMacro(MaximumNumberOfLabels, int);

  virtual void SetLabelFormat(const char* format);
  vtkGetStringMacro(LabelFormat);

  vtkSetClampMacro(LabelFontSize, int, 4, 128);
  vtkGetMacro(LabelFontSize, int);

  void SetVisibility(bool val) override;
  void SetOrientation(double x, double y, double z) override;
  void SetOrigin(double x, double y, double z) override;
  void SetPosition(double x, double y, double z) override;
  void SetScale(double x, double y, double z) override;
  void SetUserTransform(const double matrix[16]) override;

protected:
  vtkSHYXGraphRepresentation();
  ~vtkSHYXGraphRepresentation() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int ProcessViewRequest(
    vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo) override;
  bool AddToView(vtkView* view) override;
  bool RemoveFromView(vtkView* view) override;
  void UpdateColoringParameters() override;
  bool NeedsOrderedCompositing() override;

  void HideDefaultSurfaceActors();
  void UpdateLayerTransforms();
  void ApplyAppearance();
  void HideAllLayers();
  void RebuildFromData(vtkDataSet* ds);
  static void OnWarningEvent(vtkObject*, unsigned long, void* clientdata, void*);

  int ShowVertex = 1;
  int AllPoint = 0;
  int ShowLine = 1;
  int ShowFace = 1;
  int ShowVolume = 1;
  char* VertexArray = nullptr;
  char* LineArray = nullptr;
  char* FaceArray = nullptr;
  char* VolumeArray = nullptr;
  double VertexColor[3] = { 1.0, 0.85, 0.2 };
  double VertexOpacity = 1.0;
  double VertexScale = 1.0;
  double LineColor[3] = { 0.2, 0.7, 1.0 };
  double LineOpacity = 1.0;
  double FaceColor[3] = { 0.45, 0.8, 0.5 };
  double FaceOpacity = 0.55;
  double VolumeColor[3] = { 1.0, 0.5, 0.2 };
  double VolumeOpacity = 0.35;
  int MaximumNumberOfLabels = 2000;
  char* LabelFormat = nullptr;
  int LabelFontSize = 14;

  vtkSmartPointer<vtkDataObject> OriginalInput;
  vtkMergeBlocks* MergeBlocks = nullptr;
  vtkTransform* LayerTransform = nullptr;
  vtkActor* TransformHelperProp = nullptr;
  vtkCallbackCommand* WarningObserver = nullptr;
  vtkWeakPointer<vtkRenderer> MainRenderer;

  vtkPointGaussianMapper* VertexMapper = nullptr;
  vtkActor* VertexActor = nullptr;
  vtkPolyDataMapper* LineMapper = nullptr;
  vtkActor* LineActor = nullptr;
  vtkPolyDataMapper* FaceMapper = nullptr;
  vtkActor* FaceActor = nullptr;
  vtkPolyDataMapper* VolumeFaceMapper = nullptr;
  vtkActor* VolumeFaceActor = nullptr;
  vtkPolyDataMapper* VolumeEdgeMapper = nullptr;
  vtkActor* VolumeEdgeActor = nullptr;

  vtkLabeledDataMapper* VertexLabelMapper = nullptr;
  vtkActor2D* VertexLabelActor = nullptr;
  vtkTextProperty* VertexLabelProperty = nullptr;
  vtkMaskPoints* VertexMask = nullptr;

  vtkLabeledDataMapper* LineLabelMapper = nullptr;
  vtkActor2D* LineLabelActor = nullptr;
  vtkTextProperty* LineLabelProperty = nullptr;
  vtkMaskPoints* LineMask = nullptr;
  vtkCellCenters* LineCenters = nullptr;

  vtkLabeledDataMapper* FaceLabelMapper = nullptr;
  vtkActor2D* FaceLabelActor = nullptr;
  vtkTextProperty* FaceLabelProperty = nullptr;
  vtkMaskPoints* FaceMask = nullptr;
  vtkCellCenters* FaceCenters = nullptr;

  vtkLabeledDataMapper* VolumeLabelMapper = nullptr;
  vtkActor2D* VolumeLabelActor = nullptr;
  vtkTextProperty* VolumeLabelProperty = nullptr;
  vtkMaskPoints* VolumeMask = nullptr;
  vtkCellCenters* VolumeCenters = nullptr;

  vtkGeometryFilter* LineGeometry = nullptr;
  vtkGeometryFilter* FaceGeometry = nullptr;
  vtkGeometryFilter* VolumeGeometry = nullptr;
  vtkExtractEdges* VolumeEdges = nullptr;

private:
  vtkSHYXGraphRepresentation(const vtkSHYXGraphRepresentation&) = delete;
  void operator=(const vtkSHYXGraphRepresentation&) = delete;
};

#endif
