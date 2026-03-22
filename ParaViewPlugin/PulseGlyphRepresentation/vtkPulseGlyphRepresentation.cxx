// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkPulseGlyphRepresentation.h"

#include "vtkAlgorithmOutput.h"
#include "vtkDataArray.h"
#include "vtkDataSet.h"
#include "vtkDataObjectTree.h"
#include "vtkDataObjectTreeRange.h"
#include "vtkFloatArray.h"
#include "vtkGlyph3DMapper.h"
#include "vtkInformation.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkNew.h"
#include "vtkPartitionedDataSet.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkPVRenderView.h"
#include "vtkSMPTools.h"
#include "vtkPVView.h"
#include "vtkTimerLog.h"

#include <algorithm>
#include <cmath>
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

double AnimationCoordScalar(const double* tuple, int numComp)
{
  if (numComp <= 1)
  {
    return tuple[0];
  }
  const double x = tuple[0];
  const double y = tuple[1];
  const double z = (numComp >= 3) ? tuple[2] : 0.0;
  return std::sqrt(x * x + y * y + z * z);
}
} // namespace

vtkStandardNewMacro(vtkPulseGlyphRepresentation);

//------------------------------------------------------------------------------
vtkPulseGlyphRepresentation::vtkPulseGlyphRepresentation()
{
  this->Animate = true;
  this->TimeScale = 0.4;
  this->IntegrationScale = 50.0;
  this->Trunc = 2.0;
  this->Pow = 1.0;
  this->PulseOverallScale = 1.0;
  this->StartTime = vtkTimerLog::GetUniversalTime();
  this->SetAnimationCoordinateArray("IntegrationTime");

  this->GlyphMapper->SetScaling(true);
  this->GlyphMapper->SetScaleMode(vtkGlyph3DMapper::SCALE_BY_MAGNITUDE);
  this->GlyphMapper->SetScaleArray("PulseGlyphScale");

  this->LODGlyphMapper->SetScaling(true);
  this->LODGlyphMapper->SetScaleMode(vtkGlyph3DMapper::SCALE_BY_MAGNITUDE);
  this->LODGlyphMapper->SetScaleArray("PulseGlyphScale");
}

//------------------------------------------------------------------------------
vtkPulseGlyphRepresentation::~vtkPulseGlyphRepresentation()
{
  this->SetAnimationCoordinateArray(nullptr);
}

//------------------------------------------------------------------------------
void vtkPulseGlyphRepresentation::FillPolyDataPulseArray(vtkPolyData* pd)
{
  if (!pd || pd->GetNumberOfPoints() < 1)
  {
    return;
  }

  const vtkIdType n = pd->GetNumberOfPoints();
  const char* aname = EffectiveAnimationCoordinateArray(this->AnimationCoordinateArray);
  vtkDataArray* acArr = pd->GetPointData()->GetArray(aname);

  vtkFloatArray* pulse = vtkFloatArray::SafeDownCast(pd->GetPointData()->GetArray("PulseGlyphScale"));
  if (!pulse)
  {
    vtkNew<vtkFloatArray> neu;
    neu->SetName("PulseGlyphScale");
    neu->SetNumberOfComponents(1);
    pd->GetPointData()->AddArray(neu);
    pulse = vtkFloatArray::SafeDownCast(pd->GetPointData()->GetArray("PulseGlyphScale"));
  }
  if (!pulse)
  {
    return;
  }
  pulse->SetNumberOfTuples(n);

  const double now = vtkTimerLog::GetUniversalTime();
  const float t = this->Animate ? static_cast<float>(now - this->StartTime) : 0.0f;
  const float iscale = static_cast<float>(this->IntegrationScale);
  const float ts = static_cast<float>(this->TimeScale);
  const float truncV = static_cast<float>(this->Trunc);
  const float powV = static_cast<float>(this->Pow);
  const float overall = static_cast<float>(this->PulseOverallScale);

  vtkSMPTools::For(0, n, [&](vtkIdType start, vtkIdType end) {
    std::vector<double> tuple(16);
    for (vtkIdType i = start; i < end; ++i)
    {
      float anim = 0.f;
      if (acArr && acArr->GetNumberOfTuples() > i)
      {
        const int nc = acArr->GetNumberOfComponents();
        acArr->GetTuple(i, tuple.data());
        anim = static_cast<float>(AnimationCoordScalar(tuple.data(), nc));
      }
      else
      {
        double x[3];
        pd->GetPoint(i, x);
        anim = static_cast<float>(std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]));
      }

      const float mixValue = anim * iscale + t * ts;
      const float phase = mixValue - std::floor(mixValue);
      const float clamped = std::min(1.f, std::max(0.f, truncV * phase));
      const float pulseVal = (1.0f - std::pow(clamped, powV)) * overall;
      pulse->SetValue(i, pulseVal);
    }
  });

  pulse->Modified();
  pd->GetPointData()->Modified();
}

//------------------------------------------------------------------------------
void vtkPulseGlyphRepresentation::UpdatePulseScaleArrays(vtkDataObject* dobj)
{
  if (!dobj)
  {
    return;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(dobj))
  {
    this->FillPolyDataPulseArray(pd);
    return;
  }
  if (auto* pds = vtkPartitionedDataSet::SafeDownCast(dobj))
  {
    const unsigned int np = pds->GetNumberOfPartitions();
    for (unsigned int i = 0; i < np; ++i)
    {
      if (vtkDataSet* part = pds->GetPartition(i))
      {
        this->UpdatePulseScaleArrays(part);
      }
    }
    return;
  }
  if (auto* mb = vtkMultiBlockDataSet::SafeDownCast(dobj))
  {
    const unsigned int nb = mb->GetNumberOfBlocks();
    for (unsigned int i = 0; i < nb; ++i)
    {
      vtkDataObject* b = mb->GetBlock(i);
      if (b)
      {
        this->UpdatePulseScaleArrays(b);
      }
    }
    return;
  }
  if (auto* tree = vtkDataObjectTree::SafeDownCast(dobj))
  {
    using Opts = vtk::DataObjectTreeOptions;
    for (vtkDataObject* child : vtk::Range(tree, Opts::SkipEmptyNodes))
    {
      this->UpdatePulseScaleArrays(child);
    }
  }
}

//------------------------------------------------------------------------------
int vtkPulseGlyphRepresentation::ProcessViewRequest(
  vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo)
{
  const int ret = this->Superclass::ProcessViewRequest(request_type, inInfo, outInfo);
  if (!ret)
  {
    return 0;
  }

  if (request_type == vtkPVView::REQUEST_RENDER())
  {
    // Glyph3DRepresentation XML defaults Scaling=0; SM proxy sync turns scaling off and ignores
    // PulseGlyphScale. Re-assert every render. Also bump mapper MTime so instance matrices
    // rebuild when only nested point arrays change (composite top-level MTime may not move).
    this->GlyphMapper->SetScaling(true);
    this->GlyphMapper->SetScaleMode(vtkGlyph3DMapper::SCALE_BY_MAGNITUDE);
    this->GlyphMapper->SetScaleArray("PulseGlyphScale");
    this->LODGlyphMapper->SetScaling(true);
    this->LODGlyphMapper->SetScaleMode(vtkGlyph3DMapper::SCALE_BY_MAGNITUDE);
    this->LODGlyphMapper->SetScaleArray("PulseGlyphScale");

    // Do not call GlyphMapper->Update() here: vtkOpenGLGlyph3DMapper port 0 only accepts
    // vtkDataSet / vtkCompositeDataSet; ParaView may deliver vtkPartitionedDataSet etc. on
    // the geometry pipeline, and mapper Update() would fail type checks. Update the same
    // piece producers the superclass connects and fill arrays on that output.
    vtkAlgorithmOutput* producerPort = vtkPVRenderView::GetPieceProducer(inInfo, this, 0);
    vtkAlgorithmOutput* producerPortLOD = vtkPVRenderView::GetPieceProducerLOD(inInfo, this, 0);

    vtkDataObject* mainPiece = nullptr;
    vtkDataObject* lodPiece = nullptr;
    if (producerPort && producerPort->GetProducer())
    {
      producerPort->GetProducer()->Update();
      mainPiece = producerPort->GetProducer()->GetOutputDataObject(producerPort->GetIndex());
      this->UpdatePulseScaleArrays(mainPiece);
    }
    if (producerPortLOD && producerPortLOD->GetProducer())
    {
      producerPortLOD->GetProducer()->Update();
      lodPiece = producerPortLOD->GetProducer()->GetOutputDataObject(producerPortLOD->GetIndex());
      if (lodPiece != mainPiece)
      {
        this->UpdatePulseScaleArrays(lodPiece);
      }
    }
    if (mainPiece)
    {
      mainPiece->Modified();
    }
    if (lodPiece && lodPiece != mainPiece)
    {
      lodPiece->Modified();
    }
    this->GlyphMapper->Modified();
    this->LODGlyphMapper->Modified();
  }

  return 1;
}

//------------------------------------------------------------------------------
void vtkPulseGlyphRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Animate: " << this->Animate << "\n";
  os << indent << "TimeScale: " << this->TimeScale << "\n";
  os << indent << "IntegrationScale: " << this->IntegrationScale << "\n";
  os << indent << "Trunc: " << this->Trunc << "\n";
  os << indent << "Pow: " << this->Pow << "\n";
  os << indent << "PulseOverallScale: " << this->PulseOverallScale << "\n";
  os << indent << "AnimationCoordinateArray: "
     << (this->AnimationCoordinateArray ? this->AnimationCoordinateArray : "(null)") << "\n";
}
