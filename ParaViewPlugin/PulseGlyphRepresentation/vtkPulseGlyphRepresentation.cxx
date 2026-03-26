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
#include <cstdint>
#include <cstring>
#include <random>

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

/** nullptr means do not modulate scale by a second array. */
const char* EffectiveExtraScaleArrayName(const char* s)
{
  if (!s || !s[0] || strcmp(s, "None") == 0)
  {
    return nullptr;
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

/** Scalar: absolute value; multi-component: Euclidean norm (same as animation magnitude). */
double ExtraScaleMagnitude(const double* tuple, int numComp)
{
  if (numComp <= 1)
  {
    return std::fabs(tuple[0]);
  }
  return AnimationCoordScalar(tuple, numComp);
}

/** Seed \c std::minstd_rand from \c mixValue bits only (same mixValue → same seed). */
uint32_t SeedFromMixValue(float mixValue)
{
  uint32_t u = 0;
  static_assert(sizeof(float) == sizeof(uint32_t), "");
  std::memcpy(&u, &mixValue, sizeof(u));
  u ^= u >> 16;
  u *= 0x7feb352du;
  u ^= u >> 15;
  return u ? u : 1u;
}

/**
 * Four independent [0,1) phases from one RNG stream seeded only by mixValue.
 * Same mixValue always yields the same four values (scale, then X/Y/Z rotation channels).
 */
void PulseShufflePhasesFromMixValue(float mixValue, float phases[4])
{
  std::minstd_rand gen(SeedFromMixValue(mixValue));
  std::uniform_real_distribution<float> dist(0.f, 1.f);
  phases[0] = dist(gen);
  phases[1] = dist(gen);
  phases[2] = dist(gen);
  phases[3] = dist(gen);
}

void ApplyPulseScaleToMapper(vtkGlyph3DMapper* m)
{
  if (!m)
  {
    return;
  }
  m->SetScaling(true);
  m->SetScaleMode(vtkGlyph3DMapper::SCALE_BY_MAGNITUDE);
  m->SetScaleArray("PulseGlyphScale");
}

void ApplyPulseRotationToMapper(vtkGlyph3DMapper* m)
{
  if (!m)
  {
    return;
  }
  m->SetOrient(true);
  m->SetOrientationMode(vtkGlyph3DMapper::ROTATION);
  m->SetOrientationArray("PulseGlyphOrientation");
}

vtkFloatArray* GetOrCreatePulseGlyphScale(vtkPolyData* pd, vtkIdType n)
{
  vtkFloatArray* pulse = vtkFloatArray::SafeDownCast(pd->GetPointData()->GetArray("PulseGlyphScale"));
  if (!pulse)
  {
    vtkNew<vtkFloatArray> neu;
    neu->SetName("PulseGlyphScale");
    neu->SetNumberOfComponents(1);
    pd->GetPointData()->AddArray(neu);
    pulse = vtkFloatArray::SafeDownCast(pd->GetPointData()->GetArray("PulseGlyphScale"));
  }
  if (pulse)
  {
    pulse->SetNumberOfTuples(n);
  }
  return pulse;
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
  this->ArrayAffectScale = true;
  this->ArrayAffectScaleRatio = 1.0;
  this->PulseAffectsScale = true;
  this->PulseAffectsRotation = false;
  this->Shuffle = false;
  this->RotationSweep[0] = 360.0;
  this->RotationSweep[1] = 360.0;
  this->RotationSweep[2] = 360.0;

  if (this->PulseAffectsScale ||
    (this->ArrayAffectScale && EffectiveExtraScaleArrayName(this->ExtraScaleArray)))
  {
    ApplyPulseScaleToMapper(this->GlyphMapper);
    ApplyPulseScaleToMapper(this->LODGlyphMapper);
  }
}

//------------------------------------------------------------------------------
vtkPulseGlyphRepresentation::~vtkPulseGlyphRepresentation()
{
  this->SetAnimationCoordinateArray(nullptr);
  this->SetExtraScaleArray(nullptr);
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
  const char* exName = EffectiveExtraScaleArrayName(this->ExtraScaleArray);
  vtkDataArray* exArr = exName ? pd->GetPointData()->GetArray(exName) : nullptr;

  // PulseGlyphScale = Overall × (envTerm + arrTerm): envTerm is envelope(ph) if PulseAffectsScale,
  // else 0; arrTerm is |extra array|×ratio if useExtraScaleArray, else 0. Rotation uses the same
  // envelope in PulseGlyphOrientation when PulseAffectsRotation. Extra array: one completeness
  // check (tuples >= n).
  const bool pulseAffectRotate = this->PulseAffectsRotation;
  const bool pulseAffectScale = this->PulseAffectsScale;
  const bool arrayAffectScale = this->ArrayAffectScale;
  const bool useExtraScaleArray =
    arrayAffectScale && exArr != nullptr && exArr->GetNumberOfTuples() >= n;
  const int exEnc = useExtraScaleArray ? exArr->GetNumberOfComponents() : 0;

  // Nothing to write: no pulse-on-scale, no rotation pulse, no per-point extra array.
  if (!pulseAffectScale && !pulseAffectRotate && !useExtraScaleArray)
  {
    return;
  }

  // Only extra array (no pulse envelope, no rotation): PulseGlyphScale = Overall × (0 + array×ratio).
  if (!pulseAffectScale && !pulseAffectRotate)
  {
    vtkFloatArray* pulse = GetOrCreatePulseGlyphScale(pd, n);
    if (!pulse)
    {
      return;
    }
    const float overall = static_cast<float>(this->PulseOverallScale);
    vtkSMPTools::For(0, n, [&](vtkIdType start, vtkIdType end) {
      std::vector<double> tuple(16);
      for (vtkIdType i = start; i < end; ++i)
      {
        exArr->GetTuple(i, tuple.data());
        const float arrTerm = static_cast<float>(
          ExtraScaleMagnitude(tuple.data(), exEnc) * this->ArrayAffectScaleRatio);
        pulse->SetValue(i, overall * arrTerm);
      }
    });
    pulse->Modified();
    pd->GetPointData()->Modified();
    return;
  }

  vtkFloatArray* orient = nullptr;
  if (pulseAffectRotate)
  {
    orient = vtkFloatArray::SafeDownCast(pd->GetPointData()->GetArray("PulseGlyphOrientation"));
    if (!orient)
    {
      vtkNew<vtkFloatArray> oa;
      oa->SetName("PulseGlyphOrientation");
      oa->SetNumberOfComponents(3);
      pd->GetPointData()->AddArray(oa);
      orient = vtkFloatArray::SafeDownCast(pd->GetPointData()->GetArray("PulseGlyphOrientation"));
    }
    if (orient)
    {
      orient->SetNumberOfTuples(n);
    }
  }

  const bool writePulseGlyphScale = pulseAffectScale || useExtraScaleArray;
  vtkFloatArray* pulse = nullptr;
  if (writePulseGlyphScale)
  {
    pulse = GetOrCreatePulseGlyphScale(pd, n);
    if (!pulse)
    {
      return;
    }
  }

  const double now = vtkTimerLog::GetUniversalTime();
  const float t = this->Animate ? static_cast<float>(now - this->StartTime) : 0.0f;
  const float iscale = static_cast<float>(this->IntegrationScale);
  const float ts = static_cast<float>(this->TimeScale);
  const float truncV = static_cast<float>(this->Trunc);
  const float powV = static_cast<float>(this->Pow);
  const float overall = static_cast<float>(this->PulseOverallScale);
  const float sx = static_cast<float>(this->RotationSweep[0]);
  const float sy = static_cast<float>(this->RotationSweep[1]);
  const float sz = static_cast<float>(this->RotationSweep[2]);
  const bool shuffle = this->Shuffle;

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

      float mixValue = anim * iscale;
      if (shuffle && this->Animate)
      {
        mixValue += static_cast<float>(this->RenderFrame) * ts;
      }
      else
      {
        mixValue += t * ts;
      }

      auto envelopeFromPhase = [&](float ph) -> float {
        const float clamped = std::min(1.f, std::max(0.f, truncV * ph));
        return 1.0f - std::pow(clamped, powV);
      };

      float ph0, ph1, ph2, ph3;
      if (shuffle)
      {
        float phases[4];
        PulseShufflePhasesFromMixValue(mixValue, phases);
        ph0 = phases[0];
        ph1 = phases[1];
        ph2 = phases[2];
        ph3 = phases[3];
      }
      else
      {
        const float ph = mixValue - std::floor(mixValue);
        ph0 = ph1 = ph2 = ph3 = ph;
      }

      const float envTerm = pulseAffectScale ? envelopeFromPhase(ph0) : 0.f;
      float arrTerm = 0.f;
      if (useExtraScaleArray)
      {
        exArr->GetTuple(i, tuple.data());
        arrTerm = static_cast<float>(
          ExtraScaleMagnitude(tuple.data(), exEnc) * this->ArrayAffectScaleRatio);
      }
      if (pulse)
      {
        pulse->SetValue(i, overall * (envTerm + arrTerm));
      }

      if (pulseAffectRotate && orient)
      {
        float rx, ry, rz;
        if (!shuffle)
        {
          const float e = envelopeFromPhase(ph0);
          rx = e * sx;
          ry = e * sy;
          rz = e * sz;
        }
        else
        {
          rx = envelopeFromPhase(ph1) * sx;
          ry = envelopeFromPhase(ph2) * sy;
          rz = envelopeFromPhase(ph3) * sz;
        }
        orient->SetTuple3(i, rx, ry, rz);
      }
    }
  });

  if (pulse)
  {
    pulse->Modified();
  }
  if (orient)
  {
    orient->Modified();
  }
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
    // Drive PulseGlyphScale on the mapper only when pulse-on-scale or array-driven scale is
    // active (dual to applying PulseGlyphOrientation only when PulseAffectsRotation). Otherwise
    // leave scale mode/array to the superclass / SM (e.g. Glyph Scale Array).
    const bool usePulseGlyphScaleMapper = this->PulseAffectsScale ||
      (this->ArrayAffectScale && EffectiveExtraScaleArrayName(this->ExtraScaleArray));
    if (usePulseGlyphScaleMapper)
    {
      ApplyPulseScaleToMapper(this->GlyphMapper);
      ApplyPulseScaleToMapper(this->LODGlyphMapper);
    }
    if (this->PulseAffectsRotation)
    {
      ApplyPulseRotationToMapper(this->GlyphMapper);
      ApplyPulseRotationToMapper(this->LODGlyphMapper);
    }

    // Do not call GlyphMapper->Update() here: vtkOpenGLGlyph3DMapper port 0 only accepts
    // vtkDataSet / vtkCompositeDataSet; ParaView may deliver vtkPartitionedDataSet etc. on
    // the geometry pipeline, and mapper Update() would fail type checks. Update the same
    // piece producers the superclass connects and fill arrays on that output.
    const bool touchPulseFields = this->PulseAffectsScale || this->PulseAffectsRotation ||
      (this->ArrayAffectScale && EffectiveExtraScaleArrayName(this->ExtraScaleArray));

    vtkAlgorithmOutput* producerPort = vtkPVRenderView::GetPieceProducer(inInfo, this, 0);
    vtkAlgorithmOutput* producerPortLOD = vtkPVRenderView::GetPieceProducerLOD(inInfo, this, 0);

    vtkDataObject* mainPiece = nullptr;
    vtkDataObject* lodPiece = nullptr;
    if (producerPort && producerPort->GetProducer())
    {
      producerPort->GetProducer()->Update();
      mainPiece = producerPort->GetProducer()->GetOutputDataObject(producerPort->GetIndex());
      if (touchPulseFields)
      {
        this->UpdatePulseScaleArrays(mainPiece);
      }
    }
    if (producerPortLOD && producerPortLOD->GetProducer())
    {
      producerPortLOD->GetProducer()->Update();
      lodPiece = producerPortLOD->GetProducer()->GetOutputDataObject(producerPortLOD->GetIndex());
      if (touchPulseFields && lodPiece != mainPiece)
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

    if (this->Shuffle && this->Animate)
    {
      ++this->RenderFrame;
    }
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
  os << indent << "ExtraScaleArray: " << (this->ExtraScaleArray ? this->ExtraScaleArray : "(null)")
     << "\n";
  os << indent << "ArrayAffectScale: " << this->ArrayAffectScale << "\n";
  os << indent << "ArrayAffectScaleRatio: " << this->ArrayAffectScaleRatio << "\n";
  os << indent << "PulseAffectsScale: " << this->PulseAffectsScale << "\n";
  os << indent << "PulseAffectsRotation: " << this->PulseAffectsRotation << "\n";
  os << indent << "Shuffle: " << this->Shuffle << "\n";
  os << indent << "RenderFrame: " << this->RenderFrame << "\n";
  os << indent << "RotationSweep: (" << this->RotationSweep[0] << ", " << this->RotationSweep[1]
     << ", " << this->RotationSweep[2] << ")\n";
}
