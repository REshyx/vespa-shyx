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
  return !s || !s[0] || strcmp(s, "None") == 0 || strcmp(s, "(Uniform)") == 0;
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
  this->SetAnimationCoordinateYArray(nullptr);
}

void vtkAnimatedStreamlineCompositePolyDataMapper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "AnimationCoordinateArray: "
     << (this->AnimationCoordinateArray ? this->AnimationCoordinateArray : "(null)") << "\n";
  os << indent << "AnimationCoordinateYArray: "
     << (this->AnimationCoordinateYArray ? this->AnimationCoordinateYArray : "(null)") << "\n";
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
  vtkDataObject* dobj, const char* xArrayName, const char* yArrayName)
{
  if (!dobj || IsNoneOrEmpty(xArrayName))
  {
    return;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(dobj))
  {
    vtkDataArray* srcX = pd->GetPointData()->GetArray(xArrayName);
    if (!srcX || srcX->GetNumberOfTuples() < 1)
    {
      return;
    }
    const int numCompX = srcX->GetNumberOfComponents();
    if (numCompX < 1)
    {
      return;
    }
    vtkDataArray* srcY = nullptr;
    int numCompY = 0;
    if (!IsNoneOrEmpty(yArrayName))
    {
      srcY = pd->GetPointData()->GetArray(yArrayName);
      if (srcY && srcY->GetNumberOfTuples() >= 1)
      {
        numCompY = srcY->GetNumberOfComponents();
        if (numCompY < 1)
        {
          srcY = nullptr;
          numCompY = 0;
        }
      }
      else
      {
        srcY = nullptr;
      }
    }

    const vtkIdType nX = srcX->GetNumberOfTuples();
    const vtkIdType nY = srcY ? srcY->GetNumberOfTuples() : nX;
    const vtkIdType nUse = std::min(nX, nY);

    vtkNew<vtkFloatArray> tc;
    tc->SetNumberOfComponents(2);
    tc->SetNumberOfTuples(nUse);
    std::vector<double> tupleX(static_cast<size_t>(numCompX));
    std::vector<double> tupleY;
    if (numCompY > 0)
    {
      tupleY.resize(static_cast<size_t>(numCompY));
    }
    for (vtkIdType i = 0; i < nUse; ++i)
    {
      srcX->GetTuple(i, tupleX.data());
      const double vx = AnimationCoordScalar(tupleX.data(), numCompX);
      float vy = 1.0f;
      if (srcY && numCompY > 0)
      {
        srcY->GetTuple(i, tupleY.data());
        vy = static_cast<float>(AnimationCoordScalar(tupleY.data(), numCompY));
      }
      tc->SetTuple2(i, static_cast<float>(vx), vy);
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
      vtkAnimatedStreamlineCompositePolyDataMapper::FillPointArrayAsTextureCoordinates(
        child, xArrayName, yArrayName);
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
  const char* yname = this->GetAnimationCoordinateYArray();

  const std::string arrayKey(std::string(aname) + "|" + (yname ? yname : ""));
  const vtkMTimeType srcMX = input ? TreeMaxPointArrayMTime(input, aname) : 0;
  const vtkMTimeType srcMY =
    (input && !IsNoneOrEmpty(yname)) ? TreeMaxPointArrayMTime(input, yname) : 0;
  const vtkMTimeType srcM = std::max(srcMX, srcMY);
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
      vtkAnimatedStreamlineCompositePolyDataMapper::FillPointArrayAsTextureCoordinates(
        input, aname, yname);
    }
    this->LastTextureCoordArrayKey = arrayKey;
    this->LastAnimationSourceArrayMTime = srcM;
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
