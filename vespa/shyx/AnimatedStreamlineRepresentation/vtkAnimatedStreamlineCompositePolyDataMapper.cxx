// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkAnimatedStreamlineCompositePolyDataMapper.h"

#include "vtkCellArray.h"
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

/**
 * Deterministic hash of an integer to [0, 1). Used for the per-streamline random phase so the same
 * input always produces the same offsets (no flicker when tcoords are rebuilt, e.g. on slider drag).
 */
double HashToUnitInterval(unsigned int h)
{
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return static_cast<double>(h) / 4294967296.0;
}

/**
 * Seed the per-streamline random phase from the component's anchor point position (its smallest
 * point id) so it stays stable when tcoords are rebuilt but differs between streamlines, including
 * streamlines living in separate blocks of a composite dataset.
 */
unsigned int ComponentPhaseHash(vtkPolyData* pd, vtkIdType anchorId)
{
  unsigned int h = 2166136261u; // FNV-1a offset basis
  const double* p = pd->GetPoint(anchorId);
  if (!p)
  {
    return h;
  }
  for (int c = 0; c < 3; ++c)
  {
    const long long q = static_cast<long long>(std::llround(p[c] * 1000.0));
    h = (h ^ static_cast<unsigned int>(q)) * 16777619u;
    h = (h ^ static_cast<unsigned int>(q >> 32)) * 16777619u;
  }
  return h ^ (static_cast<unsigned int>(anchorId) * 2654435761u);
}

/**
 * Union-find over point ids through every cell (lines, polys, strips, verts). Each connected
 * component - typically one streamline polyline or one tube around it - shares one root id, so the
 * random phase offset is constant along a streamline but different between streamlines.
 * Root id is always the smallest point id of the component, which keeps the hash input stable.
 */
std::vector<vtkIdType> ConnectedPointComponentRoots(vtkPolyData* pd)
{
  const vtkIdType nPts = pd->GetNumberOfPoints();
  std::vector<vtkIdType> roots(static_cast<size_t>(std::max<vtkIdType>(nPts, 0)), 0);
  if (nPts <= 0)
  {
    return roots;
  }

  std::vector<vtkIdType> parent(static_cast<size_t>(nPts));
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    parent[static_cast<size_t>(i)] = i;
  }
  auto find = [&parent](vtkIdType i) {
    while (parent[static_cast<size_t>(i)] != i)
    {
      parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
      i = parent[static_cast<size_t>(i)];
    }
    return i;
  };

  vtkCellArray* cellArrays[] = { pd->GetLines(), pd->GetPolys(), pd->GetStrips(), pd->GetVerts() };
  for (vtkCellArray* cells : cellArrays)
  {
    if (!cells || cells->GetNumberOfCells() < 1)
    {
      continue;
    }
    cells->InitTraversal();
    vtkIdType npts = 0;
    vtkIdType const* pts = nullptr;
    while (cells->GetNextCell(npts, pts))
    {
      if (!pts || npts < 2)
      {
        continue;
      }
      vtkIdType root = find(pts[0]);
      for (vtkIdType k = 1; k < npts; ++k)
      {
        const vtkIdType b = find(pts[k]);
        if (b == root)
        {
          continue;
        }
        // Keep the smallest point id as the component root.
        if (root < b)
        {
          parent[static_cast<size_t>(b)] = root;
        }
        else
        {
          parent[static_cast<size_t>(root)] = b;
          root = b;
        }
      }
    }
  }

  for (vtkIdType i = 0; i < nPts; ++i)
  {
    roots[static_cast<size_t>(i)] = find(i);
  }
  return roots;
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
  this->Randomize = false;
  this->RandomizeScale = 1.0;
  this->IntegrationScale = 100.0;
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
  os << indent << "Randomize: " << this->Randomize << "\n";
  os << indent << "RandomizeScale: " << this->RandomizeScale << "\n";
  os << indent << "IntegrationScale: " << this->IntegrationScale << "\n";
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
  vtkDataObject* dobj, const char* xArrayName, const char* yArrayName, bool randomize,
  double randomizeScale, double integrationScale)
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

    // Per-streamline random phase: one offset per connected component, converted from pulse cycles
    // to tcoord.x units (the shader multiplies animCoordx by integrationScale).
    std::vector<float> randomOffsets;
    const bool applyRandomize =
      randomize && randomizeScale > 0.0 && std::abs(integrationScale) > 1e-12;
    if (applyRandomize)
    {
      const std::vector<vtkIdType> compRoots = ConnectedPointComponentRoots(pd);
      randomOffsets.assign(static_cast<size_t>(nUse), 0.0f);
      for (vtkIdType i = 0; i < nUse; ++i)
      {
        const vtkIdType root = (i < static_cast<vtkIdType>(compRoots.size())) ? compRoots[i] : i;
        const double cycles =
          HashToUnitInterval(ComponentPhaseHash(pd, root)) * randomizeScale;
        randomOffsets[static_cast<size_t>(i)] = static_cast<float>(cycles / integrationScale);
      }
    }

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
      double vx = AnimationCoordScalar(tupleX.data(), numCompX);
      if (!randomOffsets.empty())
      {
        vx += randomOffsets[static_cast<size_t>(i)];
      }
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
        child, xArrayName, yArrayName, randomize, randomizeScale, integrationScale);
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

  const std::string arrayKey(std::string(aname) + "|" + (yname ? yname : "") + "|" +
    (this->Randomize ? "rand" : "norand") + "|" + std::to_string(this->RandomizeScale) + "|" +
    std::to_string(this->IntegrationScale));
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
        input, aname, yname, this->Randomize, this->RandomizeScale, this->IntegrationScale);
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
