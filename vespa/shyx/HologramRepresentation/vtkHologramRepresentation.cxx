// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkHologramRepresentation.h"

#include "vtkAbstractArray.h"
#include "vtkActor.h"
#include "vtkAlgorithmOutput.h"
#include "vtkCallbackCommand.h"
#include "vtkCamera.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCellType.h"
#include "vtkCellTypes.h"
#include "vtkCommand.h"
#include "vtkCompositeDataIterator.h"
#include "vtkCompositeDataSet.h"
#include "vtkDataObjectAlgorithm.h"
#include "vtkDataObjectTree.h"
#include "vtkDataObjectTreeIterator.h"
#include "vtkDataObjectTypes.h"
#include "vtkDataSet.h"
#include "vtkExplicitStructuredGrid.h"
#include "vtkFloatArray.h"
#include "vtkGenericCell.h"
#include "vtkHexagonalPrism.h"
#include "vtkHexahedron.h"
#include "vtkIdList.h"
#include "vtkIdTypeArray.h"
#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMapper.h"
#include "vtkMatrix4x4.h"
#include "vtkMultiProcessController.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPVDataRepresentation.h"
#include "vtkPVLODActor.h"
#include "vtkPVRenderView.h"
#include "vtkPVTrivialProducer.h"
#include "vtkPVView.h"
#include "vtkPentagonalPrism.h"
#include "vtkPointData.h"
#include "vtkPointSet.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkProperty.h"
#include "vtkPyramid.h"
#include "vtkRectilinearGrid.h"
#include "vtkRenderer.h"
#include "vtkShaderProgram.h"
#include "vtkShaderProperty.h"
#include "vtkSmartPointer.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkStructuredGrid.h"
#include "vtkTetra.h"
#include "vtkTimerLog.h"
#include "vtkUnsignedCharArray.h"
#include "vtkUnstructuredGrid.h"
#include "vtkVoxel.h"
#include "vtkWedge.h"

#include <algorithm>
#include <string>

namespace
{
void InstallHologramShader(vtkShaderProperty* sp, bool groupMesh)
{
  if (!sp)
  {
    return;
  }

  sp->ClearAllShaderReplacements();

  // VTK's surface-with-edges pass writes a screen-space edge mask `emix` in main().
  // Keep a file-scope copy so the lighting replacement can see it, and so line/selection
  // shaders that never emit `emix` still compile (the mask stays 0).
  sp->AddFragmentShaderReplacement("//VTK::Edges::Dec", true,
    "float shyxWire = 0.0;\n"
    "//VTK::Edges::Dec\n",
    false);

  sp->AddFragmentShaderReplacement(
    "float emix = clamp(0.5 + 0.5*lineWidth - min( min( edist[0], edist[1]), edist[2]), 0.0, 1.0);",
    false,
    "float emix = clamp(0.5 + 0.5*lineWidth - min( min( edist[0], edist[1]), edist[2]), 0.0, 1.0);\n"
    "shyxWire = emix;\n",
    false);

  sp->AddFragmentShaderReplacement("//VTK::Light::Dec", true,
    "//VTK::Light::Dec\n"
    "uniform mat4 hgVCMC;\n"
    "uniform vec3 hgScanOrigin;\n"
    "uniform vec3 hgScanExtent;\n"
    "uniform float hgTime;\n"
    "uniform float hgStyle;\n"
    "uniform float hgFlow;\n"
    "uniform float hgWireOpacity;\n"
    "uniform float hgFaceOpacity;\n"
    "uniform float hgFresnelPower;\n"
    "uniform float hgFresnelFacing;\n"
    "uniform float hgScanStrength;\n"
    "uniform float hgScanSpeed;\n"
    "uniform float hgBandCount;\n"
    "uniform float hgNoiseScale;\n"
    "uniform float hgNoisePower;\n"
    "uniform float hgScanAxis;\n"
    "uniform float hgScanWidth;\n"
    "uniform float hgDepthMin;\n"
    "uniform float hgDepthExtent;\n"
    "uniform mat4 hgMCVC;\n"
    "vec3 hgRot(vec3 p, vec3 axis, float ang)\n"
    "{\n"
    "  float c = cos(ang);\n"
    "  float s = sin(ang);\n"
    "  return p * c + cross(axis, p) * s + axis * dot(axis, p) * (1.0 - c);\n"
    "}\n"
    "// psrdnoise3 (c) Stefan Gustavson and Ian McEwan, MIT.\n"
    "// https://github.com/stegu/psrdnoise  -- 3D flow noise for Shadertoy 7sBcRG.\n"
    "vec4 hgPerm4(vec4 x)\n"
    "{\n"
    "  vec4 xm = mod(x, 289.0);\n"
    "  return mod(((xm * 34.0) + 10.0) * xm, 289.0);\n"
    "}\n"
    "float hgPsrd3(vec3 x, float alpha, out vec3 gradient)\n"
    "{\n"
    "  const mat3 M = mat3(0.0, 1.0, 1.0, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0);\n"
    "  const mat3 Mi = mat3(-0.5, 0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5, -0.5);\n"
    "  vec3 uvw = M * x;\n"
    "  vec3 i0 = floor(uvw);\n"
    "  vec3 f0 = fract(uvw);\n"
    "  vec3 g_ = step(f0.xyx, f0.yzz);\n"
    "  vec3 l_ = 1.0 - g_;\n"
    "  vec3 g = vec3(l_.z, g_.xy);\n"
    "  vec3 l = vec3(l_.xy, g_.z);\n"
    "  vec3 o1 = min(g, l);\n"
    "  vec3 o2 = max(g, l);\n"
    "  vec3 i1 = i0 + o1;\n"
    "  vec3 i2 = i0 + o2;\n"
    "  vec3 i3 = i0 + vec3(1.0);\n"
    "  vec3 v0 = Mi * i0;\n"
    "  vec3 v1 = Mi * i1;\n"
    "  vec3 v2 = Mi * i2;\n"
    "  vec3 v3 = Mi * i3;\n"
    "  vec3 x0 = x - v0;\n"
    "  vec3 x1 = x - v1;\n"
    "  vec3 x2 = x - v2;\n"
    "  vec3 x3 = x - v3;\n"
    "  vec4 hash = hgPerm4(hgPerm4(hgPerm4(vec4(i0.z, i1.z, i2.z, i3.z))\n"
    "    + vec4(i0.y, i1.y, i2.y, i3.y)) + vec4(i0.x, i1.x, i2.x, i3.x));\n"
    "  vec4 theta = hash * 3.883222077;\n"
    "  vec4 sz = hash * -0.006920415 + 0.996539792;\n"
    "  vec4 psi = hash * 0.108705628;\n"
    "  vec4 Ct = cos(theta);\n"
    "  vec4 St = sin(theta);\n"
    "  vec4 szp = sqrt(max(1.0 - sz * sz, 0.0));\n"
    "  vec4 Sp = sin(psi);\n"
    "  vec4 Cp = cos(psi);\n"
    "  vec4 px = Ct * szp;\n"
    "  vec4 py = St * szp;\n"
    "  vec4 pz = sz;\n"
    "  vec4 Ctp = St * Sp - Ct * Cp;\n"
    "  vec4 qx = mix(Ctp * St, Sp, sz);\n"
    "  vec4 qy = mix(-Ctp * Ct, Cp, sz);\n"
    "  vec4 qz = -(py * Cp + px * Sp);\n"
    "  vec4 Sa = vec4(sin(alpha));\n"
    "  vec4 Ca = vec4(cos(alpha));\n"
    "  vec4 gx = Ca * px + Sa * qx;\n"
    "  vec4 gy = Ca * py + Sa * qy;\n"
    "  vec4 gz = Ca * pz + Sa * qz;\n"
    "  vec3 g0 = vec3(gx.x, gy.x, gz.x);\n"
    "  vec3 g1 = vec3(gx.y, gy.y, gz.y);\n"
    "  vec3 g2 = vec3(gx.z, gy.z, gz.z);\n"
    "  vec3 g3 = vec3(gx.w, gy.w, gz.w);\n"
    "  vec4 w = 0.5 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3));\n"
    "  w = max(w, 0.0);\n"
    "  vec4 w2 = w * w;\n"
    "  vec4 w3 = w2 * w;\n"
    "  vec4 gdotx = vec4(dot(g0, x0), dot(g1, x1), dot(g2, x2), dot(g3, x3));\n"
    "  float n = dot(w3, gdotx);\n"
    "  vec4 dw = -6.0 * w2 * gdotx;\n"
    "  gradient = 39.5 * (w3.x * g0 + dw.x * x0 + w3.y * g1 + dw.y * x1\n"
    "    + w3.z * g2 + dw.z * x2 + w3.w * g3 + dw.w * x3);\n"
    "  return 39.5 * n;\n"
    "}\n"
    "// Foaming waves: gradients spin in place. No drift along a world axis.\n"
    "float hgFoam(vec3 npos, float bands, float time, float speed)\n"
    "{\n"
    "  float freq = max(bands, 0.5);\n"
    "  float amp = 1.0;\n"
    "  float n = 0.0;\n"
    "  vec3 g = vec3(0.0);\n"
    "  vec3 gsum = vec3(0.0);\n"
    "  for (int i = 0; i < 5; ++i)\n"
    "  {\n"
    "    n += amp * hgPsrd3(npos * freq + gsum * 0.14, 8.0 / freq * time * speed, g);\n"
    "    gsum += g * amp;\n"
    "    freq *= 2.0;\n"
    "    amp *= 0.5;\n"
    "  }\n"
    "  return clamp(-n * 0.35 + 0.5, 0.0, 1.0);\n"
    "}\n"
    "// Shadertoy NXS3DG, chronos, \"Tribulence Warp\", lifted to vec3.\n"
    "vec3 hgTri3(vec3 p)\n"
    "{\n"
    "  return 1.0 - abs(p - round(p)) * 4.0;\n"
    "}\n"
    "vec3 hgFm3(vec3 p)\n"
    "{\n"
    "  return hgTri3(p + 0.7 * hgTri3(1.9 * p + 0.8));\n"
    "}\n"
    "vec3 hgTurbOnce(vec3 p, float time)\n"
    "{\n"
    "  for (int k = 0; k < 13; ++k)\n"
    "  {\n"
    "    float i = exp2(float(k)) * 0.001;\n"
    "    p += hgFm3(p.yzx / i * 0.7) * i * 0.025;\n"
    "    vec3 ax = normalize(p + vec3(0.17, 0.53, 0.31));\n"
    "    p = hgRot(p, ax, 2.399 + time * 0.05);\n"
    "  }\n"
    "  return p;\n"
    "}\n"
    "float hgTurbulence(vec3 npos, float bands, float time, float speed)\n"
    "{\n"
    "  vec3 p = (npos - 0.5) * 2.0 * max(bands, 0.5) / 7.0;\n"
    "  float t = time * speed;\n"
    "  for (int k = 0; k < 4; ++k)\n"
    "    p = hgTurbOnce(p, t);\n"
    "  float sdf = length(p);\n"
    "  float K = 0.1 / (sdf * sdf + 0.1);\n"
    "  return sqrt(K);\n"
    "}\n"
    "// Shadertoy MflyRf, incre_ment, \"Maelstrom_\".\n"
    "// simplex3d from nikat (XsX3zB); value noise from the Book of Shaders.\n"
    "vec3 hgRandom3(vec3 c, float time)\n"
    "{\n"
    "  float j = 4096.0 * sin(dot(c, vec3(17.0, 59.4, 15.0)) + time);\n"
    "  vec3 r;\n"
    "  r.z = fract(512.0 * j);\n"
    "  j *= 0.125;\n"
    "  r.x = fract(512.0 * j);\n"
    "  j *= 0.125;\n"
    "  r.y = fract(512.0 * j);\n"
    "  return r - 0.5;\n"
    "}\n"
    "float hgSimplex3(vec3 p, float time)\n"
    "{\n"
    "  const float F3 = 0.3333333;\n"
    "  const float G3 = 0.1666667;\n"
    "  vec3 s = floor(p + dot(p, vec3(F3)));\n"
    "  vec3 x = p - s + dot(s, vec3(G3));\n"
    "  vec3 e = step(vec3(0.0), x - x.yzx);\n"
    "  vec3 i1 = e * (1.0 - e.zxy);\n"
    "  vec3 i2 = 1.0 - e.zxy * (1.0 - e);\n"
    "  vec3 x1 = x - i1 + G3;\n"
    "  vec3 x2 = x - i2 + 2.0 * G3;\n"
    "  vec3 x3 = x - 1.0 + 3.0 * G3;\n"
    "  vec4 w = vec4(dot(x, x), dot(x1, x1), dot(x2, x2), dot(x3, x3));\n"
    "  w = max(0.6 - w, 0.0);\n"
    "  vec4 d = vec4(dot(hgRandom3(s, time), x), dot(hgRandom3(s + i1, time), x1),\n"
    "    dot(hgRandom3(s + i2, time), x2), dot(hgRandom3(s + 1.0, time), x3));\n"
    "  w *= w;\n"
    "  w *= w;\n"
    "  d *= w;\n"
    "  return dot(d, vec4(52.0));\n"
    "}\n"
    "float hgHash3(vec3 p, float time)\n"
    "{\n"
    "  p = fract(p * 0.1031);\n"
    "  p += dot(p, p.zyx + 31.32);\n"
    "  return fract((p.x + p.y) * p.z + time * 0.17);\n"
    "}\n"
    "float hgNoise3(vec3 p, float time)\n"
    "{\n"
    "  vec3 i = floor(p);\n"
    "  vec3 f = fract(p);\n"
    "  f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(mix(hgHash3(i, time), hgHash3(i + vec3(1, 0, 0), time), f.x),\n"
    "                 mix(hgHash3(i + vec3(0, 1, 0), time), hgHash3(i + vec3(1, 1, 0), time), f.x), f.y),\n"
    "             mix(mix(hgHash3(i + vec3(0, 0, 1), time), hgHash3(i + vec3(1, 0, 1), time), f.x),\n"
    "                 mix(hgHash3(i + vec3(0, 1, 1), time), hgHash3(i + vec3(1, 1, 1), time), f.x), f.y), f.z);\n"
    "}\n"
    "float hgFbm3(vec3 p, float time)\n"
    "{\n"
    "  float nVal = 0.0;\n"
    "  float amp = 0.45;\n"
    "  for (int i = 0; i < 4; ++i)\n"
    "  {\n"
    "    nVal += amp * hgSimplex3(p, time);\n"
    "    nVal += amp * hgNoise3(p, time);\n"
    "    p *= 3.0;\n"
    "    amp *= 0.45;\n"
    "  }\n"
    "  return nVal;\n"
    "}\n"
    "float hgMaelstrom(vec3 npos, float bands, float time, float speed)\n"
    "{\n"
    "  vec3 p = (npos - 0.5) * max(bands, 0.5);\n"
    "  float t = time * speed;\n"
    "  float n0 = hgFbm3(p, t);\n"
    "  float n1 = hgFbm3(p + vec3(5.2, 1.3, 2.8), t);\n"
    "  float n2 = hgFbm3(p + vec3(1.7, 9.2, 3.1), t);\n"
    "  float n = hgFbm3(p + vec3(n0, n1, n2), t);\n"
    "  return clamp(3.0 * (n - 0.4), 0.0, 1.5);\n"
    "}\n"
    "// One or more stripes traveling along a normalized axis. Speed 1 crosses the\n"
    "// bounds in about 5 seconds. Width is a fraction of one period.\n"
    "float hgSweep(float coord, float bands, float time, float speed, float width)\n"
    "{\n"
    "  float freq = max(bands, 0.5);\n"
    "  float travel = time * speed * 0.2;\n"
    "  float phase = fract((coord - travel) * freq);\n"
    "  float d = abs(phase - 0.5);\n"
    "  float w = clamp(width, 0.002, 0.49);\n"
    "  float core = 1.0 - smoothstep(0.0, w, d);\n"
    "  float halo = 1.0 - smoothstep(w, min(w * 4.0, 0.5), d);\n"
    "  return clamp(core + 0.45 * halo, 0.0, 1.0);\n"
    "}\n"
    "// Grouped scan: cells behind the front stay on. Width is the fade at the\n"
    "// front, as a fraction of the bounds. The fill restarts when the front wraps.\n"
    "float hgReveal(float coord, float time, float speed, float width)\n"
    "{\n"
    "  float front = fract(time * speed * 0.2);\n"
    "  float w = clamp(width, 0.002, 0.49);\n"
    "  float c = clamp(coord, 0.0, 1.0);\n"
    "  return smoothstep(0.0, w, front - c + w);\n"
    "}\n",
    false);

  // Grouping carries the volume-cell centroid in tangentMC. The fragment shader
  // names it *VSOut so the shader cache renames it to *GSOut when the edge
  // geometry shader is present.
  const char* flowPos = groupMesh
    ? "  vec3 flowPos = (shyxGroupMCVSOut - hgScanOrigin) / extent;\n"
    : "  vec3 flowPos = npos;\n";
  const char* scanBody = groupMesh
    ? "    float axisCoord;\n"
      "    if (hgScanAxis < 0.5)\n"
      "      axisCoord = clamp(flowPos.x, 0.0, 1.0);\n"
      "    else if (hgScanAxis < 1.5)\n"
      "      axisCoord = clamp(flowPos.y, 0.0, 1.0);\n"
      "    else if (hgScanAxis < 2.5)\n"
      "      axisCoord = clamp(flowPos.z, 0.0, 1.0);\n"
      "    else\n"
      "    {\n"
      "      vec3 centerVC = (hgMCVC * vec4(shyxGroupMCVSOut, 1.0)).xyz;\n"
      "      axisCoord = clamp((-centerVC.z - hgDepthMin) / max(hgDepthExtent, 1e-6), 0.0, 1.0);\n"
      "    }\n"
      "    band = hgReveal(axisCoord, hgTime, hgScanSpeed, hgScanWidth);\n"
    : "    float axisCoord;\n"
      "    if (hgScanAxis < 0.5)\n"
      "      axisCoord = npos.x;\n"
      "    else if (hgScanAxis < 1.5)\n"
      "      axisCoord = npos.y;\n"
      "    else if (hgScanAxis < 2.5)\n"
      "      axisCoord = npos.z;\n"
      "    else\n"
      "      axisCoord = clamp((-vertexVC.z - hgDepthMin) / max(hgDepthExtent, 1e-6), 0.0, 1.0);\n"
      "    band = hgSweep(axisCoord, max(hgBandCount, 0.5), hgTime, hgScanSpeed, hgScanWidth);\n";

  // Runs after VTK writes gl_FragData[0]. Overwrites it with an unlit hologram:
  // triangle edges from the edge mask, face alpha from fresnel and either the flow or a scan band.
  const std::string lightImpl = std::string(
    "{\n"
    "  vec3 nrm = normalize(normalVCVSOutput);\n"
    "  vec3 vdir = cameraParallel == 1 ? vec3(0.0, 0.0, 1.0) : normalize(-vertexVC.xyz);\n"
    "  float facing = clamp(abs(dot(nrm, vdir)), 0.0, 1.0);\n"
    "  float fresPower = max(hgFresnelPower, 0.05);\n"
    "  float rim = pow(clamp(1.0 - facing, 0.0, 1.0), fresPower);\n"
    "  float front = pow(facing, fresPower);\n"
    "  float fres = mix(rim, front, clamp(hgFresnelFacing, 0.0, 1.0));\n"
    "  vec3 modelPos = (hgVCMC * vec4(vertexVC.xyz, 1.0)).xyz;\n"
    "  vec3 extent = max(hgScanExtent, vec3(1e-6));\n"
    "  vec3 npos = (modelPos - hgScanOrigin) / extent;\n") +
    flowPos +
    std::string(
    "  float bands = max(hgBandCount, 0.5) / max(hgNoiseScale, 0.05);\n"
    "  float field = 0.0;\n"
    "  float band = 0.0;\n"
    "  if (hgFlow < 2.5)\n"
    "  {\n"
    "    if (hgFlow < 0.5)\n"
    "      field = hgFoam(flowPos, bands, hgTime, hgScanSpeed);\n"
    "    else if (hgFlow < 1.5)\n"
    "      field = hgTurbulence(flowPos, bands, hgTime, hgScanSpeed);\n"
    "    else\n"
    "      field = hgMaelstrom(flowPos, bands, hgTime, hgScanSpeed);\n"
    "    field = pow(clamp(field, 0.0, 1.0), max(hgNoisePower, 0.05));\n"
    "  }\n"
    "  else\n"
    "  {\n") +
    scanBody +
    std::string(
    "  }\n"
    "  float noiseScan = clamp(field * hgScanStrength, 0.0, 1.5);\n"
    "  float bandScan = clamp(band * hgScanStrength, 0.0, 1.5);\n"
    "  float scan = max(noiseScan, bandScan);\n"
    "  float style = clamp(hgStyle, 0.0, 1.0);\n"
    "  float wireMask = clamp(shyxWire, 0.0, 1.0);\n"
    "  float wireReveal = mix(1.0, clamp(fres * 0.9 + scan, 0.0, 1.0), style);\n"
    "  float wireA = wireMask * hgWireOpacity * wireReveal;\n"
    "  // Ghost keeps the noise behind fresnel. The sweep still fills faces in the band.\n"
    "  float faceGate = mix(0.08 + 0.7 * scan, fres * (0.55 + noiseScan) + bandScan, style);\n"
    "  float faceA = (1.0 - wireMask) * hgFaceOpacity * faceGate;\n"
    "  float alpha = clamp(wireA + faceA, 0.0, 1.0);\n"
    "  vec3 tint = diffuseColor;\n"
    "  float lum = dot(tint, vec3(0.299, 0.587, 0.114));\n"
    "  if (lum < 0.02) tint = vec3(1.0, 0.32, 0.08);\n"
    "  vec3 rgb = tint * mix(0.35 + 1.2 * scan, 1.15 + 1.6 * scan, wireMask);\n"
    "  gl_FragData[0] = vec4(rgb, alpha * opacity);\n"
    "}\n"
    "//VTK::Light::Impl\n");
  sp->AddFragmentShaderReplacement("//VTK::Light::Impl", false, lightImpl, false);

  if (!groupMesh)
  {
    return;
  }

  // Flat shading does not declare tangentMC. Point tangents hold the cell center.
  sp->AddVertexShaderReplacement("//VTK::PositionVC::Dec", true,
    "in vec3 tangentMC;\n"
    "out vec3 shyxGroupMCVSOut;\n"
    "//VTK::PositionVC::Dec\n",
    false);
  sp->AddVertexShaderReplacement("//VTK::PositionVC::Impl", true,
    "shyxGroupMCVSOut = tangentMC;\n"
    "//VTK::PositionVC::Impl\n",
    false);
  sp->AddGeometryShaderReplacement("//VTK::PositionVC::Dec", true,
    "in vec3 shyxGroupMCVSOut[];\n"
    "out vec3 shyxGroupMCGSOut;\n"
    "//VTK::PositionVC::Dec\n",
    false);
  sp->AddGeometryShaderReplacement("//VTK::PositionVC::Impl", true,
    "shyxGroupMCGSOut = shyxGroupMCVSOut[i];\n"
    "//VTK::PositionVC::Impl\n",
    false);
  sp->AddFragmentShaderReplacement("//VTK::PositionVC::Dec", true,
    "in vec3 shyxGroupMCVSOut;\n"
    "//VTK::PositionVC::Dec\n",
    false);
}
} // namespace

namespace
{
constexpr const char* kVolumeCellIdsArray = "shyxVolumeCellIds";
constexpr const char* kGroupCenterArray = "shyxGroupCenter";
constexpr const char* kGroupExpandedArray = "shyxGroupExpanded";
constexpr unsigned char kSkipGhost = static_cast<unsigned char>(vtkDataSetAttributes::HIDDENCELL |
  vtkDataSetAttributes::DUPLICATECELL | vtkDataSetAttributes::REFINEDCELL);

bool SkipGhost(vtkUnsignedCharArray* ghosts, vtkIdType cellId)
{
  return ghosts && cellId < ghosts->GetNumberOfTuples() && (ghosts->GetValue(cellId) & kSkipGhost);
}

bool DatasetHasVolumeCells(vtkDataSet* ds)
{
  if (!ds || ds->GetNumberOfCells() == 0)
  {
    return false;
  }
  vtkNew<vtkCellTypes> types;
  ds->GetCellTypes(types);
  for (vtkIdType i = 0; i < types->GetNumberOfTypes(); ++i)
  {
    if (vtkCellTypes::GetDimension(types->GetCellType(i)) == 3)
    {
      return true;
    }
  }
  return false;
}

struct FaceSink
{
  vtkCellArray* Polys = nullptr;
  vtkCellData* OutCD = nullptr;
  vtkCellData* InCD = nullptr;
  vtkIdTypeArray* SourceIds = nullptr;
  vtkFloatArray* Centers = nullptr;
  double Center[3] = { 0.0, 0.0, 0.0 };

  void SetCenter(const double c[3])
  {
    this->Center[0] = c[0];
    this->Center[1] = c[1];
    this->Center[2] = c[2];
  }

  void StampLast(vtkIdType src, vtkIdType count)
  {
    if (count <= 0)
    {
      return;
    }
    const vtkIdType first = this->Polys->GetNumberOfCells() - count;
    for (vtkIdType i = 0; i < count; ++i)
    {
      this->OutCD->CopyData(this->InCD, src, first + i);
      this->SourceIds->InsertNextValue(src);
      if (this->Centers)
      {
        this->Centers->InsertNextTuple3(this->Center[0], this->Center[1], this->Center[2]);
      }
    }
  }

  void Add(vtkIdType n, const vtkIdType* pts, vtkIdType src)
  {
    this->Polys->InsertNextCell(n, pts);
    this->StampLast(src, 1);
  }
};

bool EmitLocalFace(
  vtkCellArray* polys, const vtkIdType* local, const vtkIdType* cellPts, vtkIdType cellNpts)
{
  vtkIdType global[8];
  int n = 0;
  for (; n < 8; ++n)
  {
    const vtkIdType li = local[n];
    if (li < 0)
    {
      break;
    }
    if (li >= cellNpts)
    {
      return false;
    }
    global[n] = cellPts[li];
  }
  if (n < 3)
  {
    return false;
  }
  polys->InsertNextCell(n, global);
  return true;
}

int EmitFastVolumeFaces(
  unsigned char type, const vtkIdType* pts, vtkIdType npts, vtkCellArray* polys)
{
  int nFaces = 0;
  const vtkIdType* (*faceAt)(vtkIdType) = nullptr;
  vtkIdType need = 0;
  switch (type)
  {
    case VTK_TETRA:
      nFaces = static_cast<int>(vtkTetra::NumberOfFaces);
      faceAt = vtkTetra::GetFaceArray;
      need = 4;
      break;
    case VTK_VOXEL:
      nFaces = static_cast<int>(vtkVoxel::NumberOfFaces);
      faceAt = vtkVoxel::GetFaceArray;
      need = 8;
      break;
    case VTK_HEXAHEDRON:
      nFaces = static_cast<int>(vtkHexahedron::NumberOfFaces);
      faceAt = vtkHexahedron::GetFaceArray;
      need = 8;
      break;
    case VTK_WEDGE:
      nFaces = static_cast<int>(vtkWedge::NumberOfFaces);
      faceAt = vtkWedge::GetFaceArray;
      need = 6;
      break;
    case VTK_PYRAMID:
      nFaces = static_cast<int>(vtkPyramid::NumberOfFaces);
      faceAt = vtkPyramid::GetFaceArray;
      need = 5;
      break;
    case VTK_PENTAGONAL_PRISM:
      nFaces = static_cast<int>(vtkPentagonalPrism::NumberOfFaces);
      faceAt = vtkPentagonalPrism::GetFaceArray;
      need = 10;
      break;
    case VTK_HEXAGONAL_PRISM:
      nFaces = static_cast<int>(vtkHexagonalPrism::NumberOfFaces);
      faceAt = vtkHexagonalPrism::GetFaceArray;
      need = 12;
      break;
    default:
      return -1;
  }
  if (!pts || npts < need)
  {
    return 0;
  }
  const vtkIdType before = polys->GetNumberOfCells();
  for (int f = 0; f < nFaces; ++f)
  {
    EmitLocalFace(polys, faceAt(f), pts, npts);
  }
  return static_cast<int>(polys->GetNumberOfCells() - before);
}

void AddFacePolygon(FaceSink& sink, vtkCell* face, vtkIdType src, vtkIdList* scratch)
{
  const vtkIdType n = face->GetNumberOfPoints();
  if (n < 3)
  {
    return;
  }
  if (n <= 16)
  {
    vtkIdType buf[16];
    for (vtkIdType i = 0; i < n; ++i)
    {
      buf[i] = face->GetPointId(i);
    }
    sink.Add(n, buf, src);
    return;
  }
  scratch->SetNumberOfIds(n);
  for (vtkIdType i = 0; i < n; ++i)
  {
    scratch->SetId(i, face->GetPointId(i));
  }
  sink.Add(n, scratch->GetPointer(0), src);
}

void EmitGenericVolumeCell(vtkDataSet* ds, vtkIdType cellId, vtkGenericCell* gen, vtkIdList* triIds,
  vtkIdList* scratch, FaceSink& sink)
{
  ds->GetCell(cellId, gen);
  if (gen->GetCellDimension() != 3)
  {
    return;
  }
  const int nFaces = gen->GetNumberOfFaces();
  for (int f = 0; f < nFaces; ++f)
  {
    vtkCell* face = gen->GetFace(f);
    if (!face)
    {
      continue;
    }
    if (vtkCellTypes::IsLinear(static_cast<unsigned char>(face->GetCellType())))
    {
      AddFacePolygon(sink, face, cellId, scratch);
      continue;
    }
    if (!face->TriangulateIds(0, triIds))
    {
      continue;
    }
    const vtkIdType n = triIds->GetNumberOfIds();
    for (vtkIdType i = 0; i + 2 < n; i += 3)
    {
      const vtkIdType tri[3] = { triIds->GetId(i), triIds->GetId(i + 1), triIds->GetId(i + 2) };
      sink.Add(3, tri, cellId);
    }
  }
}

void EmitSurfaceCell(vtkDataSet* ds, vtkIdType cellId, unsigned char type, const vtkIdType* pts,
  vtkIdType npts, vtkGenericCell* gen, vtkIdList* triIds, FaceSink& sink)
{
  if (!pts || npts < 3)
  {
    return;
  }
  if (type == VTK_PIXEL && npts == 4)
  {
    const vtkIdType quad[4] = { pts[0], pts[1], pts[3], pts[2] };
    sink.Add(4, quad, cellId);
    return;
  }
  if (type == VTK_TRIANGLE_STRIP)
  {
    for (vtkIdType t = 0; t < npts - 2; ++t)
    {
      vtkIdType tri[3] = { pts[t], pts[t + 1], pts[t + 2] };
      if (t & 1)
      {
        std::swap(tri[1], tri[2]);
      }
      sink.Add(3, tri, cellId);
    }
    return;
  }
  if (vtkCellTypes::IsLinear(type))
  {
    sink.Add(npts, pts, cellId);
    return;
  }
  ds->GetCell(cellId, gen);
  if (!gen->TriangulateIds(0, triIds))
  {
    return;
  }
  const vtkIdType n = triIds->GetNumberOfIds();
  for (vtkIdType i = 0; i + 2 < n; i += 3)
  {
    const vtkIdType tri[3] = { triIds->GetId(i), triIds->GetId(i + 1), triIds->GetId(i + 2) };
    sink.Add(3, tri, cellId);
  }
}

bool UseGenericVolume(unsigned char type)
{
  return type == VTK_POLYHEDRON || type == VTK_CONVEX_POINT_SET || !vtkCellTypes::IsLinear(type);
}

void MeanOfPointIds(vtkDataSet* ds, const vtkIdType* ids, vtkIdType n, double out[3])
{
  out[0] = out[1] = out[2] = 0.0;
  if (!ds || !ids || n <= 0)
  {
    return;
  }
  for (vtkIdType i = 0; i < n; ++i)
  {
    double p[3];
    ds->GetPoint(ids[i], p);
    out[0] += p[0];
    out[1] += p[1];
    out[2] += p[2];
  }
  const double inv = 1.0 / static_cast<double>(n);
  out[0] *= inv;
  out[1] *= inv;
  out[2] *= inv;
}

void EmitCellPoints(vtkDataSet* ds, vtkIdType cellId, unsigned char type, const vtkIdType* pts,
  vtkIdType npts, vtkGenericCell* gen, vtkIdList* triIds, vtkIdList* scratch, FaceSink& sink)
{
  double center[3];
  MeanOfPointIds(ds, pts, npts, center);
  sink.SetCenter(center);
  const int dim = vtkCellTypes::GetDimension(type);
  if (dim == 2)
  {
    EmitSurfaceCell(ds, cellId, type, pts, npts, gen, triIds, sink);
    return;
  }
  if (dim != 3)
  {
    return;
  }
  if (!UseGenericVolume(type))
  {
    const vtkIdType before = sink.Polys->GetNumberOfCells();
    const int added = EmitFastVolumeFaces(type, pts, npts, sink.Polys);
    if (added >= 0)
    {
      sink.StampLast(cellId, sink.Polys->GetNumberOfCells() - before);
      return;
    }
  }
  EmitGenericVolumeCell(ds, cellId, gen, triIds, scratch, sink);
}

void CopyDatasetPoints(vtkDataSet* in, vtkPolyData* out)
{
  if (auto* ps = vtkPointSet::SafeDownCast(in))
  {
    if (ps->GetPoints())
    {
      out->SetPoints(ps->GetPoints());
    }
    else
    {
      vtkNew<vtkPoints> empty;
      out->SetPoints(empty);
    }
  }
  else
  {
    const vtkIdType n = in->GetNumberOfPoints();
    vtkNew<vtkPoints> pts;
    pts->SetDataTypeToDouble();
    pts->SetNumberOfPoints(n);
    double p[3];
    for (vtkIdType i = 0; i < n; ++i)
    {
      in->GetPoint(i, p);
      pts->SetPoint(i, p);
    }
    out->SetPoints(pts);
  }
  out->GetPointData()->PassData(in->GetPointData());
  out->GetFieldData()->PassData(in->GetFieldData());
}

bool CellTopologyIsVolume(vtkDataSet* in, int pdims[3], int cdims[3])
{
  pdims[0] = pdims[1] = pdims[2] = 0;
  if (auto* image = vtkImageData::SafeDownCast(in))
  {
    image->GetDimensions(pdims);
  }
  else if (auto* rg = vtkRectilinearGrid::SafeDownCast(in))
  {
    rg->GetDimensions(pdims);
  }
  else if (auto* sg = vtkStructuredGrid::SafeDownCast(in))
  {
    sg->GetDimensions(pdims);
  }
  else
  {
    return false;
  }
  for (int a = 0; a < 3; ++a)
  {
    cdims[a] = pdims[a] > 0 ? pdims[a] - 1 : 0;
  }
  return cdims[0] >= 1 && cdims[1] >= 1 && cdims[2] >= 1;
}

bool StructuredCellHidden(vtkDataSet* in, vtkIdType cellId, vtkUnsignedCharArray* ghosts, bool checkBlank)
{
  if (SkipGhost(ghosts, cellId))
  {
    return true;
  }
  if (!checkBlank)
  {
    return false;
  }
  if (auto* image = vtkImageData::SafeDownCast(in))
  {
    return image->IsCellVisible(cellId) == 0;
  }
  if (auto* rg = vtkRectilinearGrid::SafeDownCast(in))
  {
    return rg->IsCellVisible(cellId) == 0;
  }
  if (auto* sg = vtkStructuredGrid::SafeDownCast(in))
  {
    return sg->IsCellVisible(cellId) == 0;
  }
  return false;
}

bool EmitImplicitVolume(vtkDataSet* in, FaceSink& sink, vtkAlgorithm* progress)
{
  int pdims[3];
  int cdims[3];
  if (!CellTopologyIsVolume(in, pdims, cdims))
  {
    return false;
  }
  const bool voxel = vtkImageData::SafeDownCast(in) || vtkRectilinearGrid::SafeDownCast(in);
  const vtkIdType di = 1;
  const vtkIdType dj = pdims[0];
  const vtkIdType dk = static_cast<vtkIdType>(pdims[0]) * static_cast<vtkIdType>(pdims[1]);
  const vtkIdType cdj = cdims[0];
  const vtkIdType cdk = static_cast<vtkIdType>(cdims[0]) * static_cast<vtkIdType>(cdims[1]);
  vtkUnsignedCharArray* ghosts = in->GetCellGhostArray();
  const bool checkBlank = in->HasAnyBlankCells() || in->HasAnyBlankPoints();

  for (int k = 0; k < cdims[2]; ++k)
  {
    if (progress && (k % 8) == 0)
    {
      progress->UpdateProgress(static_cast<double>(k) / static_cast<double>(cdims[2]));
      if (progress->CheckAbort())
      {
        return true;
      }
    }
    for (int j = 0; j < cdims[1]; ++j)
    {
      for (int i = 0; i < cdims[0]; ++i)
      {
        const vtkIdType cellId = static_cast<vtkIdType>(i) + static_cast<vtkIdType>(j) * cdj +
          static_cast<vtkIdType>(k) * cdk;
        if (StructuredCellHidden(in, cellId, ghosts, checkBlank))
        {
          continue;
        }
        const vtkIdType base = static_cast<vtkIdType>(i) + static_cast<vtkIdType>(j) * dj +
          static_cast<vtkIdType>(k) * dk;
        vtkIdType corners[8];
        if (voxel)
        {
          corners[0] = base;
          corners[1] = base + di;
          corners[2] = base + dj;
          corners[3] = base + di + dj;
          corners[4] = base + dk;
          corners[5] = base + di + dk;
          corners[6] = base + dj + dk;
          corners[7] = base + di + dj + dk;
        }
        else
        {
          corners[0] = base;
          corners[1] = base + di;
          corners[2] = base + di + dj;
          corners[3] = base + dj;
          corners[4] = base + dk;
          corners[5] = base + di + dk;
          corners[6] = base + di + dj + dk;
          corners[7] = base + dj + dk;
        }
        double center[3];
        MeanOfPointIds(in, corners, 8, center);
        sink.SetCenter(center);
        const vtkIdType before = sink.Polys->GetNumberOfCells();
        for (int f = 0; f < 6; ++f)
        {
          const vtkIdType* face = voxel ? vtkVoxel::GetFaceArray(f) : vtkHexahedron::GetFaceArray(f);
          EmitLocalFace(sink.Polys, face, corners, 8);
        }
        sink.StampLast(cellId, sink.Polys->GetNumberOfCells() - before);
      }
    }
  }
  return true;
}

void EmitGeneralCells(vtkDataSet* in, FaceSink& sink, vtkAlgorithm* progress)
{
  const vtkIdType ncells = in->GetNumberOfCells();
  vtkUnsignedCharArray* ghosts = in->GetCellGhostArray();
  vtkNew<vtkIdList> ptIds;
  vtkNew<vtkGenericCell> gen;
  vtkNew<vtkIdList> triIds;
  vtkNew<vtkIdList> scratch;
  auto* ug = vtkUnstructuredGrid::SafeDownCast(in);
  auto* esg = vtkExplicitStructuredGrid::SafeDownCast(in);
  const vtkIdType step = (std::max)(ncells / 20, static_cast<vtkIdType>(1));

  for (vtkIdType cellId = 0; cellId < ncells; ++cellId)
  {
    if (progress && cellId % step == 0)
    {
      progress->UpdateProgress(ncells > 0 ? static_cast<double>(cellId) / static_cast<double>(ncells) : 1.0);
      if (progress->CheckAbort())
      {
        return;
      }
    }
    if (SkipGhost(ghosts, cellId))
    {
      continue;
    }
    if (esg && !esg->IsCellVisible(cellId))
    {
      continue;
    }

    vtkIdType npts = 0;
    const vtkIdType* pts = nullptr;
    vtkIdType* esgPts = nullptr;
    if (ug)
    {
      ug->GetCellPoints(cellId, npts, pts);
    }
    else if (esg)
    {
      esg->GetCellPoints(cellId, npts, esgPts);
      pts = esgPts;
    }
    else
    {
      in->GetCellPoints(cellId, ptIds);
      npts = ptIds->GetNumberOfIds();
      pts = ptIds->GetPointer(0);
    }
    const unsigned char type = esg ? static_cast<unsigned char>(VTK_HEXAHEDRON)
                                   : static_cast<unsigned char>(in->GetCellType(cellId));
    EmitCellPoints(in, cellId, type, pts, npts, gen, triIds, scratch, sink);
  }
}

void ExtractVolumeFaces(vtkDataSet* in, vtkPolyData* out, vtkAlgorithm* progress)
{
  out->Initialize();
  if (!in)
  {
    return;
  }
  CopyDatasetPoints(in, out);

  const vtkIdType ncells = in->GetNumberOfCells();
  vtkNew<vtkCellArray> polys;
  // Hexes and voxels have 6 faces; prisms have more. InsertTuple grows past this.
  const vtkIdType estimate = (std::max)(ncells, static_cast<vtkIdType>(1)) * 6;
  polys->AllocateEstimate(estimate, 4);

  vtkNew<vtkIdTypeArray> srcIds;
  srcIds->SetName(kVolumeCellIdsArray);
  srcIds->SetNumberOfComponents(1);
  srcIds->Allocate(estimate);

  vtkCellData* outCD = out->GetCellData();
  outCD->CopyAllocate(in->GetCellData(), estimate);
  FaceSink sink;
  vtkNew<vtkFloatArray> centers;
  centers->SetName(kGroupCenterArray);
  centers->SetNumberOfComponents(3);
  centers->Allocate(estimate * 3);

  sink.Polys = polys;
  sink.OutCD = outCD;
  sink.InCD = in->GetCellData();
  sink.SourceIds = srcIds;
  sink.Centers = centers;

  if (vtkExplicitStructuredGrid::SafeDownCast(in))
  {
    EmitGeneralCells(in, sink, progress);
  }
  else if (!EmitImplicitVolume(in, sink, progress))
  {
    EmitGeneralCells(in, sink, progress);
  }

  polys->Squeeze();
  srcIds->Squeeze();
  const vtkIdType nout = polys->GetNumberOfCells();
  for (int a = 0; a < outCD->GetNumberOfArrays(); ++a)
  {
    if (vtkAbstractArray* arr = outCD->GetAbstractArray(a))
    {
      if (arr->GetNumberOfTuples() != nout)
      {
        arr->SetNumberOfTuples(nout);
      }
    }
  }
  out->SetPolys(polys);
  outCD->AddArray(srcIds);
  if (centers->GetNumberOfTuples() != nout)
  {
    centers->SetNumberOfTuples(nout);
  }
  outCD->AddArray(centers);
  outCD->RemoveArray(vtkDataSetAttributes::GhostArrayName());
  if (progress)
  {
    progress->UpdateProgress(1.0);
  }
}

void RestoreOneVolumeCellIds(vtkPolyData* pd)
{
  if (!pd)
  {
    return;
  }
  vtkCellData* cd = pd->GetCellData();
  auto* src = vtkIdTypeArray::SafeDownCast(cd->GetArray(kVolumeCellIdsArray));
  if (!src)
  {
    return;
  }
  const vtkIdType n = src->GetNumberOfTuples();
  auto* dst = vtkIdTypeArray::SafeDownCast(cd->GetArray("vtkOriginalCellIds"));
  if (!dst || dst->GetNumberOfComponents() != 1)
  {
    vtkNew<vtkIdTypeArray> created;
    created->SetName("vtkOriginalCellIds");
    created->SetNumberOfComponents(1);
    created->SetNumberOfTuples(n);
    cd->AddArray(created);
    dst = created;
  }
  else if (dst->GetNumberOfTuples() != n)
  {
    cd->RemoveArray(kVolumeCellIdsArray);
    return;
  }
  if (n > 0)
  {
    std::copy(src->GetPointer(0), src->GetPointer(n), dst->GetPointer(0));
    dst->Modified();
  }
  cd->RemoveArray(kVolumeCellIdsArray);
}

void RestoreVolumeCellIds(vtkDataObject* data)
{
  if (!data)
  {
    return;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(data))
  {
    RestoreOneVolumeCellIds(pd);
    return;
  }
  auto* cds = vtkCompositeDataSet::SafeDownCast(data);
  if (!cds)
  {
    return;
  }
  auto iter = vtk::TakeSmartPointer(cds->NewIterator());
  if (!iter)
  {
    return;
  }
  iter->SkipEmptyNodesOn();
  if (auto* treeIt = vtkDataObjectTreeIterator::SafeDownCast(iter))
  {
    treeIt->VisitOnlyLeavesOn();
  }
  for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
  {
    RestoreVolumeCellIds(iter->GetCurrentDataObject());
  }
}

void MeanOfIds(vtkPoints* pts, vtkIdList* ids, double out[3])
{
  out[0] = out[1] = out[2] = 0.0;
  const vtkIdType n = ids ? ids->GetNumberOfIds() : 0;
  if (!pts || n <= 0)
  {
    return;
  }
  for (vtkIdType i = 0; i < n; ++i)
  {
    double p[3];
    pts->GetPoint(ids->GetId(i), p);
    out[0] += p[0];
    out[1] += p[1];
    out[2] += p[2];
  }
  const double inv = 1.0 / static_cast<double>(n);
  out[0] *= inv;
  out[1] *= inv;
  out[2] *= inv;
}

void WriteCellCenters(vtkCellArray* cells, vtkIdType base, vtkPoints* pts, vtkFloatArray* created)
{
  if (!cells || !created)
  {
    return;
  }
  vtkNew<vtkIdList> ids;
  const vtkIdType n = cells->GetNumberOfCells();
  for (vtkIdType c = 0; c < n; ++c)
  {
    cells->GetCellAtId(c, ids);
    double center[3];
    MeanOfIds(pts, ids, center);
    created->SetTuple3(base + c, center[0], center[1], center[2]);
  }
}

// Volume extraction already stored one centroid per original cell on every
// face. Surface meshes never went through that path, so each polygon gets its
// own centroid here. Do not recompute when the volume-cell array is present:
// a hex face's own centroid is not the hex centroid.
void EnsureGroupCenters(vtkPolyData* pd)
{
  vtkCellData* cd = pd->GetCellData();
  auto* existing = vtkFloatArray::SafeDownCast(cd->GetArray(kGroupCenterArray));
  if (existing && existing->GetNumberOfComponents() == 3 &&
    existing->GetNumberOfTuples() == pd->GetNumberOfCells())
  {
    return;
  }
  vtkNew<vtkFloatArray> created;
  created->SetName(kGroupCenterArray);
  created->SetNumberOfComponents(3);
  created->SetNumberOfTuples(pd->GetNumberOfCells());
  vtkPoints* pts = pd->GetPoints();
  const vtkIdType nVerts = pd->GetNumberOfVerts();
  const vtkIdType nLines = pd->GetNumberOfLines();
  const vtkIdType nPolys = pd->GetNumberOfPolys();
  WriteCellCenters(pd->GetVerts(), 0, pts, created);
  WriteCellCenters(pd->GetLines(), nVerts, pts, created);
  WriteCellCenters(pd->GetPolys(), nVerts + nLines, pts, created);
  WriteCellCenters(pd->GetStrips(), nVerts + nLines + nPolys, pts, created);
  cd->RemoveArray(kGroupCenterArray);
  cd->AddArray(created);
}

void MarkGroupExpanded(vtkPolyData* pd)
{
  vtkNew<vtkUnsignedCharArray> mark;
  mark->SetName(kGroupExpandedArray);
  mark->SetNumberOfTuples(1);
  mark->SetValue(0, 1);
  pd->GetFieldData()->AddArray(mark);
  pd->Modified();
}

void FillKeptPoints(vtkPoints* inPts, vtkIdType nKeep, vtkPoints* outPts, vtkPointData* inPD,
  vtkPointData* outPD, vtkFloatArray* tangents)
{
  for (vtkIdType i = 0; i < nKeep; ++i)
  {
    double p[3];
    inPts->GetPoint(i, p);
    outPts->SetPoint(i, p);
    outPD->CopyData(inPD, i, i);
    tangents->SetTuple3(i, p[0], p[1], p[2]);
  }
}

void RewriteCellsWithCenter(vtkCellArray* cells, vtkIdType base, vtkFloatArray* centers,
  vtkPoints* inPts, vtkPoints* outPts, vtkPointData* inPD, vtkPointData* outPD,
  vtkFloatArray* tangents, vtkIdType& cursor, vtkCellArray* rewritten)
{
  if (!cells || !rewritten)
  {
    return;
  }
  const vtkIdType n = cells->GetNumberOfCells();
  rewritten->AllocateEstimate((std::max)(n, static_cast<vtkIdType>(1)), 4);
  vtkNew<vtkIdList> ids;
  vtkNew<vtkIdList> newIds;
  for (vtkIdType c = 0; c < n; ++c)
  {
    cells->GetCellAtId(c, ids);
    const vtkIdType npts = ids->GetNumberOfIds();
    newIds->SetNumberOfIds(npts);
    double center[3] = { 0.0, 0.0, 0.0 };
    const vtkIdType tuple = base + c;
    if (centers && tuple >= 0 && tuple < centers->GetNumberOfTuples())
    {
      centers->GetTuple(tuple, center);
    }
    else
    {
      MeanOfIds(inPts, ids, center);
    }
    for (vtkIdType k = 0; k < npts; ++k)
    {
      double p[3];
      inPts->GetPoint(ids->GetId(k), p);
      outPts->SetPoint(cursor, p);
      outPD->CopyData(inPD, ids->GetId(k), cursor);
      tangents->SetTuple3(cursor, center[0], center[1], center[2]);
      newIds->SetId(k, cursor);
      ++cursor;
    }
    rewritten->InsertNextCell(newIds);
  }
}

// Duplicate face vertices so each face can carry its volume-cell centroid in
// point tangents. Shared points cannot store a per-cell center. Runs on the
// mapper-facing mesh, after the geometry filter has finished welding.
void AttachOneGroupCenters(vtkPolyData* pd)
{
  if (!pd || !pd->GetPoints() || pd->GetNumberOfPoints() == 0)
  {
    return;
  }
  if (pd->GetFieldData()->GetAbstractArray(kGroupExpandedArray))
  {
    return;
  }

  EnsureGroupCenters(pd);
  auto* centers = vtkFloatArray::SafeDownCast(pd->GetCellData()->GetArray(kGroupCenterArray));
  vtkCellArray* polys = pd->GetPolys();
  vtkCellArray* strips = pd->GetStrips();
  const vtkIdType nVerts = pd->GetNumberOfVerts();
  const vtkIdType nLines = pd->GetNumberOfLines();
  const vtkIdType nPolys = pd->GetNumberOfPolys();
  const vtkIdType polyBase = nVerts + nLines;
  const vtkIdType stripBase = polyBase + nPolys;
  vtkIdType extra = 0;
  if (polys)
  {
    extra += polys->GetNumberOfConnectivityIds();
  }
  if (strips)
  {
    extra += strips->GetNumberOfConnectivityIds();
  }

  vtkPoints* inPts = pd->GetPoints();
  vtkPointData* inPD = pd->GetPointData();
  const bool keepOriginal = nVerts > 0 || nLines > 0;
  const vtkIdType nKeep = keepOriginal ? inPts->GetNumberOfPoints() : 0;
  const vtkIdType nNew = nKeep + extra;
  if (nNew <= 0)
  {
    MarkGroupExpanded(pd);
    return;
  }

  vtkNew<vtkPoints> outPts;
  outPts->SetDataType(inPts->GetDataType());
  outPts->SetNumberOfPoints(nNew);
  vtkNew<vtkPointData> outPD;
  outPD->CopyAllocate(inPD, nNew);
  vtkNew<vtkFloatArray> tangents;
  tangents->SetName("Tangents");
  tangents->SetNumberOfComponents(3);
  tangents->SetNumberOfTuples(nNew);

  if (extra == 0)
  {
    FillKeptPoints(inPts, nKeep, outPts, inPD, outPD, tangents);
    pd->SetPoints(outPts);
    pd->GetPointData()->ShallowCopy(outPD);
    pd->GetPointData()->SetTangents(tangents);
    MarkGroupExpanded(pd);
    return;
  }

  vtkIdType cursor = 0;
  if (nKeep > 0)
  {
    FillKeptPoints(inPts, nKeep, outPts, inPD, outPD, tangents);
    cursor = nKeep;
  }
  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkCellArray> newStrips;
  RewriteCellsWithCenter(
    polys, polyBase, centers, inPts, outPts, inPD, outPD, tangents, cursor, newPolys);
  RewriteCellsWithCenter(
    strips, stripBase, centers, inPts, outPts, inPD, outPD, tangents, cursor, newStrips);

  pd->SetPoints(outPts);
  pd->GetPointData()->ShallowCopy(outPD);
  pd->GetPointData()->SetTangents(tangents);
  if (polys)
  {
    pd->SetPolys(newPolys);
  }
  if (strips && strips->GetNumberOfCells() > 0)
  {
    pd->SetStrips(newStrips);
  }
  MarkGroupExpanded(pd);
}

void AttachGroupCenters(vtkDataObject* data)
{
  if (!data)
  {
    return;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(data))
  {
    AttachOneGroupCenters(pd);
    return;
  }
  auto* cds = vtkCompositeDataSet::SafeDownCast(data);
  if (!cds)
  {
    return;
  }
  auto iter = vtk::TakeSmartPointer(cds->NewIterator());
  if (!iter)
  {
    return;
  }
  iter->SkipEmptyNodesOn();
  if (auto* treeIt = vtkDataObjectTreeIterator::SafeDownCast(iter))
  {
    treeIt->VisitOnlyLeavesOn();
  }
  for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
  {
    AttachGroupCenters(iter->GetCurrentDataObject());
  }
}

class vtkHologramVolumeFaces : public vtkDataObjectAlgorithm
{
public:
  static vtkHologramVolumeFaces* New();
  vtkTypeMacro(vtkHologramVolumeFaces, vtkDataObjectAlgorithm);

protected:
  vtkHologramVolumeFaces() = default;
  ~vtkHologramVolumeFaces() override = default;

  int RequestDataObject(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector) override
  {
    auto* input = vtkDataObject::GetData(inputVector[0], 0);
    if (!input)
    {
      return 1;
    }
    int outType = input->GetDataObjectType();
    if (auto* ds = vtkDataSet::SafeDownCast(input))
    {
      if (DatasetHasVolumeCells(ds))
      {
        outType = VTK_POLY_DATA;
      }
    }
    return vtkDataObjectAlgorithm::SetOutputDataObject(
             outType, outputVector->GetInformationObject(0), /*exact*/ true)
      ? 1
      : 0;
  }

  int RequestData(vtkInformation*, vtkInformationVector** inputVector,
    vtkInformationVector* outputVector) override
  {
    auto* input = vtkDataObject::GetData(inputVector[0], 0);
    auto* output = vtkDataObject::GetData(outputVector, 0);
    if (!input || !output)
    {
      return 1;
    }
    if (auto* inTree = vtkDataObjectTree::SafeDownCast(input))
    {
      auto* outTree = vtkDataObjectTree::SafeDownCast(output);
      if (!outTree)
      {
        return 0;
      }
      outTree->ShallowCopy(inTree);
      auto iter = vtk::TakeSmartPointer(outTree->NewTreeIterator());
      iter->SkipEmptyNodesOn();
      iter->VisitOnlyLeavesOn();
      for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
      {
        auto* ds = vtkDataSet::SafeDownCast(iter->GetCurrentDataObject());
        if (!ds || !DatasetHasVolumeCells(ds))
        {
          continue;
        }
        vtkNew<vtkPolyData> faces;
        ExtractVolumeFaces(ds, faces, this);
        outTree->SetDataSet(iter.GetPointer(), faces);
        if (this->CheckAbort())
        {
          break;
        }
      }
      return 1;
    }
    if (auto* ds = vtkDataSet::SafeDownCast(input))
    {
      if (DatasetHasVolumeCells(ds))
      {
        auto* pd = vtkPolyData::SafeDownCast(output);
        if (!pd)
        {
          return 0;
        }
        ExtractVolumeFaces(ds, pd, this);
        return 1;
      }
    }
    output->ShallowCopy(input);
    return 1;
  }

  int FillInputPortInformation(int, vtkInformation* info) override
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkCompositeDataSet");
    info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkHyperTreeGrid");
    info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkCellGrid");
    return 1;
  }

private:
  vtkHologramVolumeFaces(const vtkHologramVolumeFaces&) = delete;
  void operator=(const vtkHologramVolumeFaces&) = delete;
};

vtkStandardNewMacro(vtkHologramVolumeFaces);
} // namespace

vtkStandardNewMacro(vtkHologramRepresentation);

vtkHologramRepresentation::vtkHologramRepresentation()
{
  this->VolumeFaces = vtkHologramVolumeFaces::New();
  this->Animate = true;
  this->Style = 1;
  this->Flow = 0;
  this->WireOpacity = 0.01;
  this->FaceOpacity = 0.2;
  this->FresnelPower = 4.0;
  this->FresnelMode = 0;
  this->ScanStrength = 2.0;
  this->ScanSpeed = 2.0;
  this->ScanAxis = 0;
  this->ScanWidth = 0.08;
  this->BandCount = 4.0;
  this->NoiseScale = 0.5;
  this->NoisePower = 3.0;
  this->GroupMesh = false;
  this->StartTime = vtkTimerLog::GetUniversalTime();

  this->ShaderObserver = vtkCallbackCommand::New();
  this->ShaderObserver->SetClientData(this);
  this->ShaderObserver->SetCallback(&vtkHologramRepresentation::ShaderCallback);

  // Screen-space triangle edges (VTK surface-with-edges), flat normals so each
  // triangle shares one fresnel, translucent so the fill can fade.
  this->SetRepresentation(SURFACE_WITH_EDGES);
  this->SetBackfaceRepresentation(CULL_BACKFACE);
  if (this->Property)
  {
    this->Property->SetColor(1.0, 0.30, 0.08);
    this->Property->SetLineWidth(1.6);
    this->Property->SetEdgeOpacity(0.0);
    this->Property->SetInterpolationToFlat();
    this->Property->SetAmbient(0.0);
    this->Property->SetDiffuse(1.0);
    this->Property->SetSpecular(0.0);
  }
  if (this->Actor)
  {
    this->Actor->SetForceTranslucent(true);
  }

  this->UpdateShaderReplacements();
  this->UpdateAnimationObservers();
}

vtkHologramRepresentation::~vtkHologramRepresentation()
{
  if (this->VolumeFaces)
  {
    this->VolumeFaces->Delete();
    this->VolumeFaces = nullptr;
  }
  if (this->ShaderObserver)
  {
    this->ShaderObserver->Delete();
    this->ShaderObserver = nullptr;
  }
}

bool vtkHologramRepresentation::AddToView(vtkView* view)
{
  const bool added = this->Superclass::AddToView(view);
  if (auto* rview = vtkPVRenderView::SafeDownCast(view))
  {
    this->BoundRenderer = rview->GetRenderer();
  }
  this->UpdateAnimationObservers();
  return added;
}

bool vtkHologramRepresentation::RemoveFromView(vtkView* view)
{
  this->BoundRenderer = nullptr;
  vtkMapper* mappers[] = { this->Mapper, this->LODMapper, this->BackfaceMapper, this->LODBackfaceMapper };
  for (vtkMapper* mp : mappers)
  {
    if (mp && this->ShaderObserver)
    {
      mp->RemoveObservers(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
    }
  }
  return this->Superclass::RemoveFromView(view);
}

int vtkHologramRepresentation::ProcessViewRequest(
  vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo)
{
  // The inherited Surface proxy pushes Representation = "Surface" and
  // BackfaceRepresentation = Follow Frontface. Edges and flat normals have to
  // win before UpdateColoringParameters runs inside the superclass.
  if (this->Representation != SURFACE_WITH_EDGES)
  {
    this->SetRepresentation(SURFACE_WITH_EDGES);
  }
  if (this->BackfaceRepresentation != CULL_BACKFACE)
  {
    this->SetBackfaceRepresentation(CULL_BACKFACE);
  }
  if (this->Property)
  {
    if (this->Property->GetInterpolation() != VTK_FLAT)
    {
      this->Property->SetInterpolationToFlat();
    }
    if (this->Property->GetEdgeOpacity() != 0.0)
    {
      this->Property->SetEdgeOpacity(0.0);
    }
  }

  return this->Superclass::ProcessViewRequest(request_type, inInfo, outInfo);
}

int vtkHologramRepresentation::RequestData(
  vtkInformation* request, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // vtkGeometryRepresentation::RequestData feeds the dataset straight to
  // vtkPVGeometryFilter, which keeps only the outer shell of volume cells.
  // Explode those cells into faces first, then let the geometry filter run.
  if (inputVector[0]->GetNumberOfInformationObjects() == 1)
  {
    vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
    vtkAlgorithmOutput* internalOutputPort = this->GetInternalOutputPort();
    auto prod = vtkPVTrivialProducer::SafeDownCast(internalOutputPort->GetProducer());
    if (inInfo->Has(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT()) && prod)
    {
      prod->SetWholeExtent(inInfo->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT()));
    }
    this->VolumeFaces->SetInputConnection(internalOutputPort);
    this->GeometryFilter->SetInputConnection(this->VolumeFaces->GetOutputPort());
  }
  else
  {
    auto placeHolder =
      vtk::TakeSmartPointer(vtkDataObjectTypes::NewDataObject(this->PlaceHolderDataType));
    placeHolder->Initialize();
    this->VolumeFaces->RemoveAllInputConnections(0);
    this->GeometryFilter->SetInputDataObject(0, placeHolder);
  }

  auto* controller = vtkMultiProcessController::GetGlobalController();
  if (controller && controller->GetNumberOfProcesses() > 1)
  {
    this->VolumeFaces->Modified();
    this->GeometryFilter->Modified();
  }
  this->MultiBlockMaker->Update();
  vtkDataObject* delivered = this->MultiBlockMaker->GetOutputDataObject(0);
  RestoreVolumeCellIds(delivered);
  if (this->GroupMesh)
  {
    AttachGroupCenters(delivered);
  }

  return this->vtkPVDataRepresentation::RequestData(request, inputVector, outputVector);
}

void vtkHologramRepresentation::SetGroupMesh(bool group)
{
  if (this->GroupMesh == group)
  {
    return;
  }
  this->GroupMesh = group;
  // The grouped shader reads tangentMC. A decimated LOD mesh does not have it.
  this->SetSuppressLOD(group);
  if (this->VolumeFaces)
  {
    this->VolumeFaces->Modified();
  }
  this->UpdateShaderReplacements();
  this->MarkModified();
}

void vtkHologramRepresentation::UpdateAnimationObservers()
{
  vtkMapper* mappers[] = { this->Mapper, this->LODMapper, this->BackfaceMapper, this->LODBackfaceMapper };
  for (vtkMapper* mp : mappers)
  {
    if (!mp || !this->ShaderObserver)
    {
      continue;
    }
    mp->RemoveObservers(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
    mp->AddObserver(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
  }
}

void vtkHologramRepresentation::UpdateShaderReplacements()
{
  if (this->Actor)
  {
    InstallHologramShader(this->Actor->GetShaderProperty(), this->GroupMesh);
  }
  if (this->BackfaceActor)
  {
    InstallHologramShader(this->BackfaceActor->GetShaderProperty(), this->GroupMesh);
  }
}

void vtkHologramRepresentation::OnUpdateShader(vtkObject* caller, void* calldata)
{
  vtkShaderProgram* program = reinterpret_cast<vtkShaderProgram*>(calldata);
  if (!program)
  {
    return;
  }

  const double now = vtkTimerLog::GetUniversalTime();
  const float t = this->Animate ? static_cast<float>(now - this->StartTime) : 0.0f;

  if (program->IsUniformUsed("hgTime"))
  {
    program->SetUniformf("hgTime", t);
  }
  if (program->IsUniformUsed("hgStyle"))
  {
    program->SetUniformf("hgStyle", static_cast<float>(this->Style));
  }
  if (program->IsUniformUsed("hgFlow"))
  {
    program->SetUniformf("hgFlow", static_cast<float>(this->Flow));
  }
  if (program->IsUniformUsed("hgWireOpacity"))
  {
    program->SetUniformf("hgWireOpacity", static_cast<float>(this->WireOpacity));
  }
  if (program->IsUniformUsed("hgFaceOpacity"))
  {
    program->SetUniformf("hgFaceOpacity", static_cast<float>(this->FaceOpacity));
  }
  if (program->IsUniformUsed("hgFresnelPower"))
  {
    program->SetUniformf("hgFresnelPower", static_cast<float>(this->FresnelPower));
  }
  if (program->IsUniformUsed("hgFresnelFacing"))
  {
    program->SetUniformf("hgFresnelFacing", this->FresnelMode == 1 ? 1.0f : 0.0f);
  }
  if (program->IsUniformUsed("hgScanStrength"))
  {
    program->SetUniformf("hgScanStrength", static_cast<float>(this->ScanStrength));
  }
  if (program->IsUniformUsed("hgScanSpeed"))
  {
    program->SetUniformf("hgScanSpeed", static_cast<float>(this->ScanSpeed));
  }
  if (program->IsUniformUsed("hgScanAxis"))
  {
    program->SetUniformf("hgScanAxis", static_cast<float>(this->ScanAxis));
  }
  if (program->IsUniformUsed("hgScanWidth"))
  {
    program->SetUniformf("hgScanWidth", static_cast<float>(this->ScanWidth));
  }
  if (program->IsUniformUsed("hgBandCount"))
  {
    program->SetUniformf("hgBandCount", static_cast<float>(this->BandCount));
  }
  if (program->IsUniformUsed("hgNoiseScale"))
  {
    program->SetUniformf("hgNoiseScale", static_cast<float>(this->NoiseScale));
  }
  if (program->IsUniformUsed("hgNoisePower"))
  {
    program->SetUniformf("hgNoisePower", static_cast<float>(this->NoisePower));
  }
  // Noise is sampled in dataset coordinates. Actor::GetBounds() is the
  // world-space AABB, which stays aligned to the world axes when the actor rotates.
  vtkMapper* drawMapper = vtkMapper::SafeDownCast(caller);
  if (!drawMapper)
  {
    drawMapper = this->Mapper;
  }
  double bounds[6] = { 0.0, 1.0, 0.0, 1.0, 0.0, 1.0 };
  if (drawMapper)
  {
    if (double* mapperBounds = drawMapper->GetBounds())
    {
      for (int i = 0; i < 6; ++i)
      {
        bounds[i] = mapperBounds[i];
      }
    }
  }
  if (!(bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5]))
  {
    bounds[0] = 0.0;
    bounds[1] = 1.0;
    bounds[2] = 0.0;
    bounds[3] = 1.0;
    bounds[4] = 0.0;
    bounds[5] = 1.0;
  }
  const double origin[3] = { bounds[0], bounds[2], bounds[4] };
  const double extent[3] = { (std::max)(bounds[1] - bounds[0], 1e-6),
    (std::max)(bounds[3] - bounds[2], 1e-6), (std::max)(bounds[5] - bounds[4], 1e-6) };
  if (program->IsUniformUsed("hgScanOrigin"))
  {
    program->SetUniform3f("hgScanOrigin", origin);
  }
  if (program->IsUniformUsed("hgScanExtent"))
  {
    program->SetUniform3f("hgScanExtent", extent);
  }

  double depthMin = 0.0;
  double depthExtent = 1.0;
  if (program->IsUniformUsed("hgVCMC") || program->IsUniformUsed("hgMCVC") ||
    program->IsUniformUsed("hgDepthMin") || program->IsUniformUsed("hgDepthExtent"))
  {
    // view = WCVC * Actor * model. Invert that so the field is glued to the
    // mesh: Display translation / rotation moves the pattern with the object.
    // SetUniformMatrix feeds row-major elements to glUniformMatrix4fv as
    // column-major, so transpose first, the same way vtkOpenGLCamera::GetKeyMatrices does.
    vtkNew<vtkMatrix4x4> modelFromView;
    modelFromView->Identity();
    vtkNew<vtkMatrix4x4> glView;
    glView->Identity();
    vtkCamera* cam = this->BoundRenderer ? this->BoundRenderer->GetActiveCamera() : nullptr;
    if (cam && cam->GetModelViewTransformMatrix())
    {
      vtkActor* drawActor = this->Actor;
      if ((caller == this->BackfaceMapper || caller == this->LODBackfaceMapper) &&
        this->BackfaceActor)
      {
        drawActor = this->BackfaceActor;
      }
      vtkNew<vtkMatrix4x4> actorMatrix;
      actorMatrix->Identity();
      if (drawActor)
      {
        drawActor->GetMatrix(actorMatrix);
      }
      vtkNew<vtkMatrix4x4> viewFromModel;
      vtkMatrix4x4::Multiply4x4(cam->GetModelViewTransformMatrix(), actorMatrix, viewFromModel);
      double nearDepth = 1e300;
      double farDepth = -1e300;
      const double xs[2] = { bounds[0], bounds[1] };
      const double ys[2] = { bounds[2], bounds[3] };
      const double zs[2] = { bounds[4], bounds[5] };
      for (int ix = 0; ix < 2; ++ix)
      {
        for (int iy = 0; iy < 2; ++iy)
        {
          for (int iz = 0; iz < 2; ++iz)
          {
            const double in[4] = { xs[ix], ys[iy], zs[iz], 1.0 };
            double out[4];
            viewFromModel->MultiplyPoint(in, out);
            // Camera looks down -Z, so -Z grows from the near side toward the far side.
            const double depth = -out[2];
            nearDepth = (std::min)(nearDepth, depth);
            farDepth = (std::max)(farDepth, depth);
          }
        }
      }
      depthMin = nearDepth;
      depthExtent = (std::max)(farDepth - nearDepth, 1e-6);
      glView->DeepCopy(viewFromModel);
      modelFromView->DeepCopy(viewFromModel);
      modelFromView->Invert();
    }
    if (program->IsUniformUsed("hgMCVC"))
    {
      // Dataset cell center -> view. Transpose for glUniformMatrix4fv, same as hgVCMC.
      glView->Transpose();
      program->SetUniformMatrix("hgMCVC", glView);
    }
    if (program->IsUniformUsed("hgVCMC"))
    {
      modelFromView->Transpose();
      program->SetUniformMatrix("hgVCMC", modelFromView);
    }
  }
  if (program->IsUniformUsed("hgDepthMin"))
  {
    program->SetUniformf("hgDepthMin", static_cast<float>(depthMin));
  }
  if (program->IsUniformUsed("hgDepthExtent"))
  {
    program->SetUniformf("hgDepthExtent", static_cast<float>(depthExtent));
  }
}

void vtkHologramRepresentation::ShaderCallback(
  vtkObject* caller, unsigned long, void* clientdata, void* calldata)
{
  auto* self = reinterpret_cast<vtkHologramRepresentation*>(clientdata);
  if (self)
  {
    self->OnUpdateShader(caller, calldata);
  }
}

void vtkHologramRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Animate: " << this->Animate << "\n";
  os << indent << "Style: " << this->Style << "\n";
  os << indent << "Flow: " << this->Flow << "\n";
  os << indent << "WireOpacity: " << this->WireOpacity << "\n";
  os << indent << "FaceOpacity: " << this->FaceOpacity << "\n";
  os << indent << "FresnelPower: " << this->FresnelPower << "\n";
  os << indent << "FresnelMode: " << this->FresnelMode << "\n";
  os << indent << "ScanStrength: " << this->ScanStrength << "\n";
  os << indent << "ScanSpeed: " << this->ScanSpeed << "\n";
  os << indent << "ScanAxis: " << this->ScanAxis << "\n";
  os << indent << "ScanWidth: " << this->ScanWidth << "\n";
  os << indent << "BandCount: " << this->BandCount << "\n";
  os << indent << "NoiseScale: " << this->NoiseScale << "\n";
  os << indent << "NoisePower: " << this->NoisePower << "\n";
  os << indent << "GroupMesh: " << this->GroupMesh << "\n";
}
