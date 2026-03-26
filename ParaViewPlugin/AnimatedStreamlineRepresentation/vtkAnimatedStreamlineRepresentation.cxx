// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkAnimatedStreamlineRepresentation.h"

#include "vtkAnimatedStreamlineCompositePolyDataMapper.h"

#include "vtkCallbackCommand.h"
#include "vtkCommand.h"
#include "vtkCompositePolyDataMapper.h"
#include "vtkDataObject.h"
#include "vtkDataObjectTree.h"
#include "vtkDataObjectTreeRange.h"
#include "vtkInformation.h"
#include "vtkMapper.h"
#include "vtkObjectFactory.h"
#include "vtkPVLODActor.h"
#include "vtkPVView.h"
#include "vtkShaderProgram.h"
#include "vtkShaderProperty.h"
#include "vtkTimerLog.h"

#include <cstring>

namespace
{
const char* EffectiveAnimationCoordinateArray(const char* s)
{
  if (!s || !s[0] || strcmp(s, "None") == 0)
  {
    return "IntegrationTime";
  }
  return s;
}

void ReplaceWithAnimatedStreamlineMapper(vtkMapper*& mapperSlot, vtkPVLODActor* actor, bool setOnMainMapper)
{
  auto* oldMapper = vtkCompositePolyDataMapper::SafeDownCast(mapperSlot);
  if (!oldMapper || !actor)
  {
    return;
  }

  vtkAnimatedStreamlineCompositePolyDataMapper* neu = vtkAnimatedStreamlineCompositePolyDataMapper::New();
  neu->ShallowCopy(oldMapper);

  if (setOnMainMapper)
  {
    actor->SetMapper(nullptr);
  }
  else
  {
    actor->SetLODMapper(nullptr);
  }
  oldMapper->Delete();
  mapperSlot = neu;

  if (setOnMainMapper)
  {
    actor->SetMapper(neu);
  }
  else
  {
    actor->SetLODMapper(neu);
  }
}
} // namespace

vtkStandardNewMacro(vtkAnimatedStreamlineRepresentation);

vtkAnimatedStreamlineRepresentation::vtkAnimatedStreamlineRepresentation()
{
  this->Animate = true;
  this->OpacityScale = 0.8;
  this->TimeScale = 0.4;
  this->IntegrationScale = 50.0;
  this->Trunc = 2.0;
  this->Pow = 1.0;
  this->StartTime = vtkTimerLog::GetUniversalTime();
  this->SetAnimationCoordinateArray("IntegrationTime");

  this->ShaderObserver = vtkCallbackCommand::New();
  this->ShaderObserver->SetClientData(this);
  this->ShaderObserver->SetCallback(
    &vtkAnimatedStreamlineRepresentation::ShaderCallback);

  ReplaceWithAnimatedStreamlineMapper(this->Mapper, this->Actor, true);
  ReplaceWithAnimatedStreamlineMapper(this->LODMapper, this->Actor, false);
  ReplaceWithAnimatedStreamlineMapper(this->BackfaceMapper, this->BackfaceActor, true);
  ReplaceWithAnimatedStreamlineMapper(this->LODBackfaceMapper, this->BackfaceActor, false);

  this->SyncAnimatedMapperAnimationCoordinateArray();
  this->UpdateShaderReplacements();
  this->UpdateAnimationMTime();
}

vtkAnimatedStreamlineRepresentation::~vtkAnimatedStreamlineRepresentation()
{
  this->SetAnimationCoordinateArray(nullptr);
  this->SetAnimationCoordinateYArray(nullptr);
  if (this->ShaderObserver)
  {
    this->ShaderObserver->Delete();
    this->ShaderObserver = nullptr;
  }
}

bool vtkAnimatedStreamlineRepresentation::AddToView(vtkView* view)
{
  const bool added = this->Superclass::AddToView(view);
  this->UpdateAnimationMTime();
  return added;
}

int vtkAnimatedStreamlineRepresentation::ProcessViewRequest(
  vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo)
{
  const int ret = this->Superclass::ProcessViewRequest(request_type, inInfo, outInfo);
  if (!ret)
  {
    return 0;
  }
  if (request_type == vtkPVView::REQUEST_RENDER() && this->Mapper)
  {
    this->SyncAnimatedMapperAnimationCoordinateArray();
    this->Mapper->Update();
    vtkDataObject* input = this->Mapper->GetInputDataObject(0, 0);
    const char* aname = EffectiveAnimationCoordinateArray(this->GetAnimationCoordinateArray());
    const bool hasIT =
      vtkAnimatedStreamlineCompositePolyDataMapper::DatasetTreeHasPointArray(input, aname);
    if (hasIT != this->CachedUsesIntegrationTimeAttribute)
    {
      this->CachedUsesIntegrationTimeAttribute = hasIT;
      this->UpdateShaderReplacements();
    }
  }
  return ret;
}

void vtkAnimatedStreamlineRepresentation::SyncAnimatedMapperAnimationCoordinateArray()
{
  const char* n = this->GetAnimationCoordinateArray();
  const char* ny = this->GetAnimationCoordinateYArray();
  vtkMapper* mappers[] = { this->Mapper, this->LODMapper, this->BackfaceMapper, this->LODBackfaceMapper };
  for (vtkMapper* mp : mappers)
  {
    if (auto* am = vtkAnimatedStreamlineCompositePolyDataMapper::SafeDownCast(mp))
    {
      am->SetAnimationCoordinateArray(n ? n : "IntegrationTime");
      const char* yForMapper = nullptr;
      if (ny && ny[0] && strcmp(ny, "None") != 0 && strcmp(ny, "(Uniform)") != 0)
      {
        yForMapper = ny;
      }
      am->SetAnimationCoordinateYArray(yForMapper);
    }
  }
}

bool vtkAnimatedStreamlineRepresentation::RemoveFromView(vtkView* view)
{
  (void)view;
  if (this->Mapper && this->ShaderObserver)
  {
    this->Mapper->RemoveObservers(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
  }
  if (this->LODMapper && this->ShaderObserver)
  {
    this->LODMapper->RemoveObservers(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
  }
  return this->Superclass::RemoveFromView(view);
}

void vtkAnimatedStreamlineRepresentation::UpdateAnimationMTime()
{
  if (this->Mapper && this->ShaderObserver)
  {
    this->Mapper->RemoveObservers(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
    this->Mapper->AddObserver(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
  }
  if (this->LODMapper && this->ShaderObserver)
  {
    this->LODMapper->RemoveObservers(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
    this->LODMapper->AddObserver(vtkCommand::UpdateShaderEvent, this->ShaderObserver);
  }
}

void vtkAnimatedStreamlineRepresentation::UpdateShaderReplacements()
{
  vtkShaderProperty* sp = this->Actor ? this->Actor->GetShaderProperty() : nullptr;
  if (!sp)
  {
    return;
  }

  sp->ClearAllShaderReplacements();

  // animCoordx / animCoordy: X from tcoord.x when a point array exists; Y from tcoord.y only if a
  // Y array is chosen (otherwise tcoord.y is 1.0 on CPU). No-array fallback: arc length and animCoordy=1.
  if (this->CachedUsesIntegrationTimeAttribute)
  {
    sp->AddVertexShaderReplacement(
      "//VTK::PositionVC::Dec", true,
      "//VTK::PositionVC::Dec\n"
      "in vec2 tcoord;\n"
      "out float animCoordx;\n"
      "flat out float animCoordy;\n",
      false);
    sp->AddVertexShaderReplacement(
      "//VTK::PositionVC::Impl", true,
      "//VTK::PositionVC::Impl\n"
      "animCoordx = tcoord.x;\n"
      "animCoordy = tcoord.y;\n",
      false);
  }
  else
  {
    sp->AddVertexShaderReplacement(
      "//VTK::PositionVC::Dec", true,
      "//VTK::PositionVC::Dec\n"
      "out float animCoordx;\n"
      "flat out float animCoordy;\n",
      false);
    sp->AddVertexShaderReplacement(
      "//VTK::PositionVC::Impl", true,
      "//VTK::PositionVC::Impl\n"
      "animCoordx = length(vertexMC.xyz);\n"
      "animCoordy = 1.0;\n",
      false);
  }

  // Alpha pulse along animCoordx / animCoordy (tcoords or arc length fallback).
  sp->AddFragmentShaderReplacement(
    "//VTK::Light::Dec", true,
    "//VTK::Light::Dec\n"
    "in float animCoordx;\n"
    "flat in float animCoordy;\n"
    "uniform float time;\n"
    "uniform float integrationScale;\n"
    "uniform float timeScale;\n"
    "uniform float opacityScale;\n"
    "uniform float truncValue;\n"
    "uniform float powValue;\n",
    false);

  sp->AddFragmentShaderReplacement(
    "//VTK::Light::Impl", false,
    "float mixValue = animCoordx * integrationScale + time * timeScale / animCoordy;\n"
    "float phase = fract(mixValue);\n"
    "float pulse = 1.0 - pow(clamp(truncValue * phase, 0.0, 1.0), powValue);\n"
    "gl_FragData[0].a = gl_FragData[0].a * opacityScale * pulse;\n"
    "//VTK::Light::Impl",
    false);
}

void vtkAnimatedStreamlineRepresentation::OnUpdateShader(void* calldata)
{
  vtkShaderProgram* program = reinterpret_cast<vtkShaderProgram*>(calldata);
  if (!program)
  {
    return;
  }

  const double now = vtkTimerLog::GetUniversalTime();
  const float t = this->Animate ? static_cast<float>(now - this->StartTime) : 0.0f;

  if (program->IsUniformUsed("time"))
  {
    program->SetUniformf("time", t);
  }
  if (program->IsUniformUsed("integrationScale"))
  {
    program->SetUniformf("integrationScale",
      static_cast<float>(this->IntegrationScale));
  }
  if (program->IsUniformUsed("timeScale"))
  {
    program->SetUniformf("timeScale", static_cast<float>(this->TimeScale));
  }
  if (program->IsUniformUsed("opacityScale"))
  {
    program->SetUniformf("opacityScale", static_cast<float>(this->OpacityScale));
  }
  if (program->IsUniformUsed("truncValue"))
  {
    program->SetUniformf("truncValue", static_cast<float>(this->Trunc));
  }
  if (program->IsUniformUsed("powValue"))
  {
    program->SetUniformf("powValue", static_cast<float>(this->Pow));
  }
}

void vtkAnimatedStreamlineRepresentation::ShaderCallback(
  vtkObject*, unsigned long, void* clientdata, void* calldata)
{
  auto self = reinterpret_cast<vtkAnimatedStreamlineRepresentation*>(clientdata);
  if (self)
  {
    self->OnUpdateShader(calldata);
  }
}

void vtkAnimatedStreamlineRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Animate: " << this->Animate << "\n";
  os << indent << "OpacityScale: " << this->OpacityScale << "\n";
  os << indent << "TimeScale: " << this->TimeScale << "\n";
  os << indent << "IntegrationScale: " << this->IntegrationScale << "\n";
  os << indent << "Trunc: " << this->Trunc << "\n";
  os << indent << "Pow: " << this->Pow << "\n";
  os << indent << "AnimationCoordinateArray: "
     << (this->GetAnimationCoordinateArray() ? this->GetAnimationCoordinateArray() : "(null)") << "\n";
  os << indent << "AnimationCoordinateYArray: "
     << (this->GetAnimationCoordinateYArray() ? this->GetAnimationCoordinateYArray() : "(null)") << "\n";
}

