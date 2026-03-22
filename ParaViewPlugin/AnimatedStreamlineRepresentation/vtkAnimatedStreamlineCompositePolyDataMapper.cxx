// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkAnimatedStreamlineCompositePolyDataMapper.h"

#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataObjectTree.h"
#include "vtkDataObjectTreeRange.h"
#include "vtkFloatArray.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkOpenGLPolyDataMapper.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkPoints.h"
#include "vtkSmartPointer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
constexpr const char kLegacyNormArray[] = "ASR_IntegrationTimeNorm";

bool IsNoneOrEmpty(const char* s)
{
  return !s || !s[0] || strcmp(s, "None") == 0;
}

/** Scalar for animation: 1 component = raw value; 2+ = one sqrt(x*x+y*y+z*z) (z=0 if only 2). */
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

vtkMTimeType TreeMaxPointArrayMTime(vtkDataObject* dobj, const char* arrayName)
{
  if (!dobj || IsNoneOrEmpty(arrayName))
  {
    return 0;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(dobj))
  {
    if (pd->GetNumberOfPoints() < 1)
    {
      return 0;
    }
    vtkDataArray* arr = pd->GetPointData()->GetArray(arrayName);
    return arr ? arr->GetMTime() : 0;
  }
  if (auto* tree = vtkDataObjectTree::SafeDownCast(dobj))
  {
    vtkMTimeType m = 0;
    using Opts = vtk::DataObjectTreeOptions;
    for (vtkDataObject* child : vtk::Range(tree, Opts::SkipEmptyNodes))
    {
      m = std::max(m, TreeMaxPointArrayMTime(child, arrayName));
    }
    return m;
  }
  return 0;
}

vtkMTimeType TreeMaxPointsMTime(vtkDataObject* dobj)
{
  if (!dobj)
  {
    return 0;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(dobj))
  {
    vtkPoints* pts = pd->GetPoints();
    return pts ? pts->GetMTime() : 0;
  }
  if (auto* tree = vtkDataObjectTree::SafeDownCast(dobj))
  {
    vtkMTimeType m = 0;
    using Opts = vtk::DataObjectTreeOptions;
    for (vtkDataObject* child : vtk::Range(tree, Opts::SkipEmptyNodes))
    {
      m = std::max(m, TreeMaxPointsMTime(child));
    }
    return m;
  }
  return 0;
}
}

vtkStandardNewMacro(vtkAnimatedStreamlineCompositePolyDataMapper);

vtkAnimatedStreamlineCompositePolyDataMapper::vtkAnimatedStreamlineCompositePolyDataMapper()
{
  this->SetAnimationCoordinateArray("IntegrationTime");
}

vtkAnimatedStreamlineCompositePolyDataMapper::~vtkAnimatedStreamlineCompositePolyDataMapper()
{
  this->SetAnimationCoordinateArray(nullptr);
}

void vtkAnimatedStreamlineCompositePolyDataMapper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "AnimationCoordinateArray: "
     << (this->AnimationCoordinateArray ? this->AnimationCoordinateArray : "(null)") << "\n";
  os << indent << "LastInputHadAnimationCoordinateArray: " << this->LastInputHadAnimationCoordinateArray
     << "\n";
  os << indent << "LastAnimationSourceArrayMTime: " << this->LastAnimationSourceArrayMTime << "\n";
  os << indent << "LastAnimationPointsMTime: " << this->LastAnimationPointsMTime << "\n";
  os << indent << "LastTextureCoordArrayKey: " << this->LastTextureCoordArrayKey << "\n";
}

bool vtkAnimatedStreamlineCompositePolyDataMapper::DatasetTreeHasPointArray(
  vtkDataObject* dobj, const char* arrayName)
{
  if (!dobj || IsNoneOrEmpty(arrayName))
  {
    return false;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(dobj))
  {
    if (pd->GetNumberOfPoints() > 0 && pd->GetPointData()->GetArray(arrayName) != nullptr)
    {
      return true;
    }
    return false;
  }
  if (auto* tree = vtkDataObjectTree::SafeDownCast(dobj))
  {
    using Opts = vtk::DataObjectTreeOptions;
    for (vtkDataObject* child : vtk::Range(tree, Opts::SkipEmptyNodes))
    {
      if (vtkAnimatedStreamlineCompositePolyDataMapper::DatasetTreeHasPointArray(child, arrayName))
      {
        return true;
      }
    }
  }
  return false;
}

void vtkAnimatedStreamlineCompositePolyDataMapper::RemoveLegacyAnimatedStreamlineArrays(vtkDataObject* dobj)
{
  if (!dobj)
  {
    return;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(dobj))
  {
    pd->GetPointData()->RemoveArray(kLegacyNormArray);
    return;
  }
  if (auto* tree = vtkDataObjectTree::SafeDownCast(dobj))
  {
    using Opts = vtk::DataObjectTreeOptions;
    for (vtkDataObject* child : vtk::Range(tree, Opts::SkipEmptyNodes))
    {
      vtkAnimatedStreamlineCompositePolyDataMapper::RemoveLegacyAnimatedStreamlineArrays(child);
    }
  }
}

void vtkAnimatedStreamlineCompositePolyDataMapper::FillPointArrayAsTextureCoordinates(
  vtkDataObject* dobj, const char* arrayName)
{
  if (!dobj || IsNoneOrEmpty(arrayName))
  {
    return;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(dobj))
  {
    vtkDataArray* src = pd->GetPointData()->GetArray(arrayName);
    if (!src || src->GetNumberOfTuples() < 1)
    {
      return;
    }
    const int numComp = src->GetNumberOfComponents();
    if (numComp < 1)
    {
      return;
    }
    const vtkIdType n = src->GetNumberOfTuples();

    vtkNew<vtkFloatArray> tc;
    tc->SetNumberOfComponents(2);
    tc->SetNumberOfTuples(n);
    std::vector<double> tuple(static_cast<size_t>(numComp));
    for (vtkIdType i = 0; i < n; ++i)
    {
      src->GetTuple(i, tuple.data());
      const double v = AnimationCoordScalar(tuple.data(), numComp);
      tc->SetTuple2(i, static_cast<float>(v), 0.0f);
    }
    pd->GetPointData()->SetTCoords(tc);
    pd->GetPointData()->Modified();
    pd->Modified();
    return;
  }
  if (auto* tree = vtkDataObjectTree::SafeDownCast(dobj))
  {
    using Opts = vtk::DataObjectTreeOptions;
    for (vtkDataObject* child : vtk::Range(tree, Opts::SkipEmptyNodes))
    {
      vtkAnimatedStreamlineCompositePolyDataMapper::FillPointArrayAsTextureCoordinates(child, arrayName);
    }
  }
}

void vtkAnimatedStreamlineCompositePolyDataMapper::PreRender(
  const std::vector<vtkSmartPointer<vtkCompositePolyDataMapperDelegator>>& delegators,
  vtkRenderer* ren, vtkActor* act)
{
  (void)ren;
  (void)act;

  this->Update();
  vtkDataObject* input = this->GetInputDataObject(0, 0);

  const char* aname = this->GetAnimationCoordinateArray();
  if (IsNoneOrEmpty(aname))
  {
    aname = "IntegrationTime";
  }

  const std::string arrayKey(aname);
  const vtkMTimeType srcM = input ? TreeMaxPointArrayMTime(input, aname) : 0;
  const vtkMTimeType ptsM = input ? TreeMaxPointsMTime(input) : 0;
  // Do not use input->GetMTime(): writing tcoords bumps dataset time and causes per-frame rebuilds.
  const bool textureCoordsNeedUpdate =
    input && (this->LastTextureCoordArrayKey != arrayKey ||
      srcM > this->LastAnimationSourceArrayMTime || ptsM > this->LastAnimationPointsMTime);

  if (textureCoordsNeedUpdate)
  {
    vtkAnimatedStreamlineCompositePolyDataMapper::RemoveLegacyAnimatedStreamlineArrays(input);
    if (vtkAnimatedStreamlineCompositePolyDataMapper::DatasetTreeHasPointArray(input, aname))
    {
      vtkAnimatedStreamlineCompositePolyDataMapper::FillPointArrayAsTextureCoordinates(input, aname);
    }
    this->LastTextureCoordArrayKey = arrayKey;
    this->LastAnimationSourceArrayMTime = TreeMaxPointArrayMTime(input, aname);
    this->LastAnimationPointsMTime = TreeMaxPointsMTime(input);
  }

  const bool has =
    vtkAnimatedStreamlineCompositePolyDataMapper::DatasetTreeHasPointArray(input, aname);
  this->LastInputHadAnimationCoordinateArray = has;

  for (const auto& del : delegators)
  {
    if (!del)
    {
      continue;
    }
    auto* glMapper = vtkOpenGLPolyDataMapper::SafeDownCast(del->GetDelegate());
    if (!glMapper)
    {
      continue;
    }
    glMapper->RemoveVertexAttributeMapping("integrationTime");
  }

  this->Superclass::PreRender(delegators, ren, act);
}
