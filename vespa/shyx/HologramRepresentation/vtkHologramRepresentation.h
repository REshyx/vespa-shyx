// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkHologramRepresentation_h
#define vtkHologramRepresentation_h

#include "vtkHologramRepresentationModule.h"
#include "vtkGeometryRepresentationWithFaces.h"

class vtkAlgorithm;
class vtkCallbackCommand;
class vtkInformation;
class vtkInformationRequestKey;
class vtkInformationVector;
class vtkRenderer;

class VTKHOLOGRAMREPRESENTATION_EXPORT vtkHologramRepresentation
  : public vtkGeometryRepresentationWithFaces
{
public:
  static vtkHologramRepresentation* New();
  vtkTypeMacro(vtkHologramRepresentation, vtkGeometryRepresentationWithFaces);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(Animate, bool);
  vtkGetMacro(Animate, bool);
  vtkBooleanMacro(Animate, bool);

  /** 0 = lattice (wires stay, scan reveals faces). 1 = ghost (fresnel gates faces and wires). */
  vtkSetClampMacro(Style, int, 0, 1);
  vtkGetMacro(Style, int);

  /** 0 = foaming waves. 1 = turbulence warp. 2 = maelstrom. 3 = scan along ScanAxis. */
  vtkSetClampMacro(Flow, int, 0, 3);
  vtkGetMacro(Flow, int);

  vtkSetClampMacro(WireOpacity, double, 0.0, 1.0);
  vtkGetMacro(WireOpacity, double);

  vtkSetClampMacro(FaceOpacity, double, 0.0, 1.0);
  vtkGetMacro(FaceOpacity, double);

  vtkSetClampMacro(FresnelPower, double, 0.05, 12.0);
  vtkGetMacro(FresnelPower, double);

  /** 0 = rim / grazing reflection. 1 = faces toward the camera. 2 = uniform, no normal. */
  vtkSetClampMacro(FresnelMode, int, 0, 2);
  vtkGetMacro(FresnelMode, int);

  vtkSetClampMacro(ScanStrength, double, 0.0, 4.0);
  vtkGetMacro(ScanStrength, double);

  vtkSetClampMacro(ScanSpeed, double, -8.0, 8.0);
  vtkGetMacro(ScanSpeed, double);

  /** Used when Flow is Scan. 0/1/2 = dataset X/Y/Z. 3 = view depth (near side of the bounds to the far side). */
  vtkSetClampMacro(ScanAxis, int, 0, 3);
  vtkGetMacro(ScanAxis, int);

  /** Thickness of one stripe as a fraction of its period. */
  vtkSetClampMacro(ScanWidth, double, 0.002, 0.5);
  vtkGetMacro(ScanWidth, double);

  vtkSetClampMacro(BandCount, double, 0.5, 64.0);
  vtkGetMacro(BandCount, double);

  /** Larger than 1 enlarges the noise features. */
  vtkSetClampMacro(NoiseScale, double, 0.1, 16.0);
  vtkGetMacro(NoiseScale, double);

  /** Above 1 keeps only the bright peaks. */
  vtkSetClampMacro(NoisePower, double, 0.2, 16.0);
  vtkGetMacro(NoisePower, double);

  /**
   * Off: each fragment samples at its own position. Scan is a slice through the faces.
   * On: every face of one volume cell shares that cell's centroid, so the cell lights
   * as one piece and neighboring cells appear on their own. A surface polygon uses its
   * own centroid. Scan is a crinkle slice: the same moving band, but a cell is wholly
   * in or out from its center. Noise is one value per cell. Fresnel stays per face
   * unless FresnelMode is uniform, which ignores the normal.
   */
  virtual void SetGroupMesh(bool group);
  vtkGetMacro(GroupMesh, bool);
  vtkBooleanMacro(GroupMesh, bool);

protected:
  vtkHologramRepresentation();
  ~vtkHologramRepresentation() override;

  int ProcessViewRequest(
    vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo) override;

  int RequestData(vtkInformation* request, vtkInformationVector** inputVector,
    vtkInformationVector* outputVector) override;

  bool AddToView(vtkView* view) override;
  bool RemoveFromView(vtkView* view) override;

  void UpdateShaderReplacements();
  void UpdateAnimationObservers();
  void OnUpdateShader(vtkObject* caller, void* calldata);
  static void ShaderCallback(
    vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);

  vtkCallbackCommand* ShaderObserver = nullptr;
  vtkAlgorithm* VolumeFaces = nullptr;
  vtkRenderer* BoundRenderer = nullptr;
  double StartTime = 0.0;
  bool Animate = true;
  int Style = 1;
  int Flow = 0;
  double WireOpacity = 0.01;
  double FaceOpacity = 0.2;
  double FresnelPower = 4.0;
  int FresnelMode = 0;
  double ScanStrength = 2.0;
  double ScanSpeed = 2.0;
  int ScanAxis = 0;
  double ScanWidth = 0.08;
  double BandCount = 4.0;
  double NoiseScale = 0.5;
  double NoisePower = 3.0;
  bool GroupMesh = false;

private:
  vtkHologramRepresentation(const vtkHologramRepresentation&) = delete;
  void operator=(const vtkHologramRepresentation&) = delete;
};

#endif
