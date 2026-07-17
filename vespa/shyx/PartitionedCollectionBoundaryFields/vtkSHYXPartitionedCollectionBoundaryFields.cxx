#include "vtkSHYXPartitionedCollectionBoundaryFields.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataArray.h>
#include <vtkDataAssembly.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIOSSReader.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXPartitionedCollectionBoundaryFields);

namespace
{
constexpr const char* kBoundaryRadialValueArrayName = "BoundaryRadialValue";
constexpr const char* kBoundaryRadialNormalArrayName = "BoundaryRadialValueNormal";
constexpr const char* kBoundaryVariableArrayPrefix = "BoundaryVariable";
constexpr const char* kNodeSetNamePrefix = "node_";

struct VolumeBoundaryDuplicateWrites
{
  vtkIdType normal = 0;
  vtkIdType variables = 0;
};

struct PartitionedCollectionLayout
{
  bool HasElementBlock = false;
  unsigned int ElementBlockPdcIndex = 0;
  std::vector<unsigned int> NodeSetPdcIndices;
  std::vector<unsigned int> SideSetPdcIndices;
};

std::string JoinArrayNames(const std::vector<std::string>& names)
{
  std::ostringstream joined;
  for (size_t i = 0; i < names.size(); ++i)
  {
    if (i > 0)
    {
      joined << ", ";
    }
    joined << names[i];
  }
  return joined.str();
}

int FirstAssemblyNodeByPath(vtkDataAssembly* assembly, const char* pathA, const char* pathB)
{
  if (!assembly)
  {
    return -1;
  }
  int node = assembly->GetFirstNodeByPath(pathA);
  if (node < 0)
  {
    node = assembly->GetFirstNodeByPath(pathB);
  }
  return node;
}

unsigned int FirstDataSetIndexForAssemblyChild(vtkDataAssembly* assembly, int childNode)
{
  if (!assembly || childNode < 0)
  {
    return static_cast<unsigned int>(-1);
  }
  const std::vector<unsigned int> indices = assembly->GetDataSetIndices(childNode, false);
  if (indices.empty())
  {
    return static_cast<unsigned int>(-1);
  }
  return indices.front();
}

void CollectAssemblyChildDataSetIndices(
  vtkDataAssembly* assembly, int parentNode, std::vector<unsigned int>* outIndices)
{
  if (!assembly || parentNode < 0 || !outIndices)
  {
    return;
  }
  const int nChildren = assembly->GetNumberOfChildren(parentNode);
  for (int i = 0; i < nChildren; ++i)
  {
    const int child = assembly->GetChild(parentNode, i);
    const unsigned int pdcIndex = FirstDataSetIndexForAssemblyChild(assembly, child);
    if (pdcIndex != static_cast<unsigned int>(-1))
    {
      outIndices->push_back(pdcIndex);
    }
  }
}

bool ParsePartitionedCollectionLayout(
  vtkDataAssembly* assembly, PartitionedCollectionLayout* layout)
{
  if (!assembly || !layout)
  {
    return false;
  }

  layout->HasElementBlock = false;
  layout->ElementBlockPdcIndex = 0;
  layout->NodeSetPdcIndices.clear();
  layout->SideSetPdcIndices.clear();

  const int elemBlocks =
    FirstAssemblyNodeByPath(assembly, "/IOSS/element_blocks", "/element_blocks");
  const int nodeSets = FirstAssemblyNodeByPath(assembly, "/IOSS/node_sets", "/node_sets");
  const int sideSets = FirstAssemblyNodeByPath(assembly, "/IOSS/side_sets", "/side_sets");

  if (elemBlocks >= 0)
  {
    const int nChildren = assembly->GetNumberOfChildren(elemBlocks);
    for (int i = 0; i < nChildren; ++i)
    {
      const int child = assembly->GetChild(elemBlocks, i);
      const unsigned int pdcIndex = FirstDataSetIndexForAssemblyChild(assembly, child);
      if (pdcIndex != static_cast<unsigned int>(-1))
      {
        layout->HasElementBlock = true;
        layout->ElementBlockPdcIndex = pdcIndex;
        break;
      }
    }
  }

  CollectAssemblyChildDataSetIndices(assembly, nodeSets, &layout->NodeSetPdcIndices);
  CollectAssemblyChildDataSetIndices(assembly, sideSets, &layout->SideSetPdcIndices);
  return layout->HasElementBlock || !layout->SideSetPdcIndices.empty();
}

vtkDataSet* GetDataSetFromPdcBlock(vtkPartitionedDataSetCollection* coll, unsigned int blockIndex)
{
  if (!coll)
  {
    return nullptr;
  }
  vtkPartitionedDataSet* pds = coll->GetPartitionedDataSet(blockIndex);
  if (!pds || pds->GetNumberOfPartitions() == 0)
  {
    return nullptr;
  }
  return pds->GetPartition(0);
}

/** Return side-set geometry as vtkPolyData. Already-PolyData inputs are returned as-is;
 * otherwise vtkGeometryFilter converts (e.g. IOSS/Exodus vtkUnstructuredGrid side sets). */
vtkSmartPointer<vtkPolyData> ForceDataSetToPolyData(vtkDataSet* ds)
{
  if (!ds)
  {
    return nullptr;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    return pd;
  }

  vtkNew<vtkGeometryFilter> geometry;
  geometry->SetInputData(ds);
  geometry->Update();
  vtkPolyData* out = geometry->GetOutput();
  if (!out)
  {
    return nullptr;
  }

  vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
  copy->DeepCopy(out);
  return copy;
}

void SetPartitionDataSetBlock(
  vtkPartitionedDataSetCollection* coll, unsigned int blockIndex, vtkDataSet* ds)
{
  if (!coll || !ds)
  {
    return;
  }
  vtkPartitionedDataSet* pds = coll->GetPartitionedDataSet(blockIndex);
  if (!pds)
  {
    vtkNew<vtkPartitionedDataSet> newPds;
    newPds->SetPartition(0, ds);
    coll->SetPartitionedDataSet(blockIndex, newPds);
    return;
  }
  pds->SetPartition(0, ds);
}

void SetContiguousCellGlobalIdsPolyData(vtkPolyData* pd)
{
  if (!pd)
  {
    return;
  }
  vtkNew<vtkIdTypeArray> cg;
  cg->SetName("GlobalIds");
  const vtkIdType nc = pd->GetNumberOfCells();
  cg->SetNumberOfTuples(nc);
  for (vtkIdType i = 0; i < nc; ++i)
  {
    cg->SetValue(i, i + 1);
  }
  pd->GetCellData()->SetGlobalIds(cg);
}

vtkSmartPointer<vtkPolyData> BuildNodeSetPolyData(vtkPolyData* sideSurface)
{
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  vtkNew<vtkPoints> pts;
  pts->DeepCopy(sideSurface->GetPoints());
  out->SetPoints(pts);
  vtkNew<vtkCellArray> verts;
  const vtkIdType n = pts->GetNumberOfPoints();
  for (vtkIdType p = 0; p < n; ++p)
  {
    verts->InsertNextCell(1, &p);
  }
  out->SetVerts(verts);
  out->GetPointData()->DeepCopy(sideSurface->GetPointData());
  out->GetCellData()->Initialize();
  SetContiguousCellGlobalIdsPolyData(out);
  return out;
}

double Cross2D(const double a[2], const double b[2])
{
  return a[0] * b[1] - a[1] * b[0];
}

void AddCountedEdge(std::map<std::pair<vtkIdType, vtkIdType>, int>& edgeCounts, vtkIdType a, vtkIdType b)
{
  if (a == b)
  {
    return;
  }
  if (b < a)
  {
    std::swap(a, b);
  }
  ++edgeCounts[std::make_pair(a, b)];
}

bool ComputeAverageCellNormal(vtkPolyData* pd, double normal[3])
{
  normal[0] = normal[1] = normal[2] = 0.0;
  if (!pd || !pd->GetPoints())
  {
    return false;
  }

  vtkNew<vtkIdList> cpts;
  double p0[3], p1[3], p2[3], e1[3], e2[3], n[3];
  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    if (cpts->GetNumberOfIds() < 3)
    {
      continue;
    }
    pd->GetPoint(cpts->GetId(0), p0);
    for (vtkIdType k = 1; k + 1 < cpts->GetNumberOfIds(); ++k)
    {
      pd->GetPoint(cpts->GetId(k), p1);
      pd->GetPoint(cpts->GetId(k + 1), p2);
      vtkMath::Subtract(p1, p0, e1);
      vtkMath::Subtract(p2, p0, e2);
      vtkMath::Cross(e1, e2, n);
      normal[0] += n[0];
      normal[1] += n[1];
      normal[2] += n[2];
    }
  }

  const double len = vtkMath::Norm(normal);
  if (len <= 1e-30 || !vtkMath::IsFinite(len))
  {
    return false;
  }
  normal[0] /= len;
  normal[1] /= len;
  normal[2] /= len;
  return true;
}

void AddBoundaryRadialValueArray(vtkPolyData* pd, const char* arrayName, double exponent)
{
  if (!pd || !arrayName || arrayName[0] == '\0')
  {
    return;
  }

  const vtkIdType nPts = pd->GetNumberOfPoints();
  const vtkIdType nCells = pd->GetNumberOfCells();
  vtkNew<vtkDoubleArray> values;
  values->SetName(arrayName);
  values->SetNumberOfComponents(1);
  values->SetNumberOfTuples(nPts);
  const double nanv = std::numeric_limits<double>::quiet_NaN();
  values->Fill(nanv);

  vtkNew<vtkDoubleArray> cellValues;
  cellValues->SetName(arrayName);
  cellValues->SetNumberOfComponents(1);
  cellValues->SetNumberOfTuples(nCells);
  cellValues->Fill(nanv);

  if (nPts == 0 || nCells == 0 || !pd->GetPoints())
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }

  std::map<std::pair<vtkIdType, vtkIdType>, int> edgeCounts;
  vtkNew<vtkIdList> cpts;
  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    const vtkIdType ncp = cpts->GetNumberOfIds();
    if (ncp == 2)
    {
      AddCountedEdge(edgeCounts, cpts->GetId(0), cpts->GetId(1));
      continue;
    }
    if (ncp < 3)
    {
      continue;
    }
    for (vtkIdType k = 0; k < ncp; ++k)
    {
      AddCountedEdge(edgeCounts, cpts->GetId(k), cpts->GetId((k + 1) % ncp));
    }
  }

  std::vector<std::pair<vtkIdType, vtkIdType>> boundaryEdges;
  std::vector<unsigned char> isBoundaryPoint(static_cast<size_t>(nPts), 0);
  for (const auto& item : edgeCounts)
  {
    if (item.second != 1)
    {
      continue;
    }
    const vtkIdType a = item.first.first;
    const vtkIdType b = item.first.second;
    if (a < 0 || a >= nPts || b < 0 || b >= nPts)
    {
      continue;
    }
    boundaryEdges.push_back(item.first);
    isBoundaryPoint[static_cast<size_t>(a)] = 1;
    isBoundaryPoint[static_cast<size_t>(b)] = 1;
  }

  if (boundaryEdges.empty())
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }

  double center[3] = { 0.0, 0.0, 0.0 };
  vtkIdType nBoundaryPts = 0;
  double x[3];
  for (vtkIdType p = 0; p < nPts; ++p)
  {
    if (!isBoundaryPoint[static_cast<size_t>(p)])
    {
      continue;
    }
    pd->GetPoint(p, x);
    center[0] += x[0];
    center[1] += x[1];
    center[2] += x[2];
    ++nBoundaryPts;
  }
  if (nBoundaryPts == 0)
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }
  center[0] /= static_cast<double>(nBoundaryPts);
  center[1] /= static_cast<double>(nBoundaryPts);
  center[2] /= static_cast<double>(nBoundaryPts);

  double normal[3];
  if (!ComputeAverageCellNormal(pd, normal))
  {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
    return;
  }

  double axisU[3], axisV[3];
  vtkMath::Perpendiculars(normal, axisU, axisV, 0.0);

  std::vector<std::array<double, 2>> uv(static_cast<size_t>(nPts));
  double scale = 0.0;
  for (vtkIdType p = 0; p < nPts; ++p)
  {
    pd->GetPoint(p, x);
    const double rel[3] = { x[0] - center[0], x[1] - center[1], x[2] - center[2] };
    uv[static_cast<size_t>(p)] = { vtkMath::Dot(rel, axisU), vtkMath::Dot(rel, axisV) };
    scale = std::max(scale, std::abs(uv[static_cast<size_t>(p)][0]));
    scale = std::max(scale, std::abs(uv[static_cast<size_t>(p)][1]));
  }

  const double geomTol = 1e-12 * std::max(1.0, scale);
  const double rayTol = 1e-9;
  for (vtkIdType p = 0; p < nPts; ++p)
  {
    if (isBoundaryPoint[static_cast<size_t>(p)])
    {
      values->SetValue(p, 0.0);
      continue;
    }

    const double q[2] = { uv[static_cast<size_t>(p)][0], uv[static_cast<size_t>(p)][1] };
    const double qNorm = std::sqrt(q[0] * q[0] + q[1] * q[1]);
    if (qNorm <= geomTol)
    {
      values->SetValue(p, 1.0);
      continue;
    }

    double bestS = std::numeric_limits<double>::infinity();
    for (const auto& edge : boundaryEdges)
    {
      const double a[2] = { uv[static_cast<size_t>(edge.first)][0],
        uv[static_cast<size_t>(edge.first)][1] };
      const double b[2] = { uv[static_cast<size_t>(edge.second)][0],
        uv[static_cast<size_t>(edge.second)][1] };
      const double e[2] = { b[0] - a[0], b[1] - a[1] };
      const double denom = Cross2D(q, e);
      if (std::abs(denom) <= geomTol * qNorm)
      {
        continue;
      }
      const double s = Cross2D(a, e) / denom;
      const double u = Cross2D(a, q) / denom;
      if (s >= 1.0 - rayTol && u >= -rayTol && u <= 1.0 + rayTol && s < bestS)
      {
        bestS = s;
      }
    }

    if (vtkMath::IsFinite(bestS) && bestS > 0.0)
    {
      double raw = 1.0 / bestS;
      raw = std::max(0.0, std::min(1.0, raw));
      values->SetValue(p, 1.0 - std::pow(raw, exponent));
    }
  }

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    double sum = 0.0;
    vtkIdType count = 0;
    for (vtkIdType k = 0; k < cpts->GetNumberOfIds(); ++k)
    {
      const vtkIdType pid = cpts->GetId(k);
      if (pid < 0 || pid >= nPts)
      {
        continue;
      }
      const double v = values->GetValue(pid);
      if (!vtkMath::IsFinite(v))
      {
        continue;
      }
      sum += v;
      ++count;
    }
    if (count > 0)
    {
      cellValues->SetValue(cid, sum / static_cast<double>(count));
    }
  }

  pd->GetPointData()->RemoveArray(arrayName);
  pd->GetPointData()->AddArray(values);
  pd->GetCellData()->RemoveArray(arrayName);
  pd->GetCellData()->AddArray(cellValues);
}

void InitializeVolumeBoundaryRadialNormalArray(vtkUnstructuredGrid* volume, const char* arrayName)
{
  if (!volume || !arrayName || arrayName[0] == '\0')
  {
    return;
  }

  vtkNew<vtkDoubleArray> values;
  values->SetName(arrayName);
  values->SetNumberOfComponents(3);
  values->SetNumberOfTuples(volume->GetNumberOfPoints());
  values->Fill(std::numeric_limits<double>::quiet_NaN());

  volume->GetPointData()->RemoveArray(arrayName);
  volume->GetPointData()->AddArray(values);
}

std::vector<std::string> BoundaryVariableArrayNames(size_t nVariables)
{
  std::vector<std::string> names;
  names.reserve(nVariables);
  for (size_t i = 0; i < nVariables; ++i)
  {
    names.push_back(std::string(kBoundaryVariableArrayPrefix) + std::to_string(i + 1));
  }
  return names;
}

void InitializeVolumeBoundaryVariableArrays(
  vtkUnstructuredGrid* volume, const std::vector<std::string>& arrayNames)
{
  if (!volume)
  {
    return;
  }

  for (const std::string& arrayName : arrayNames)
  {
    if (arrayName.empty())
    {
      continue;
    }

    vtkNew<vtkDoubleArray> values;
    values->SetName(arrayName.c_str());
    values->SetNumberOfComponents(1);
    values->SetNumberOfTuples(volume->GetNumberOfPoints());
    values->Fill(std::numeric_limits<double>::quiet_NaN());

    volume->GetPointData()->RemoveArray(arrayName.c_str());
    volume->GetPointData()->AddArray(values);
  }
}

VolumeBoundaryDuplicateWrites AccumulateBoundaryRadialNormalToVolume(vtkPolyData* side,
  vtkUnstructuredGrid* volume, const char* radialArrayName, const char* vectorArrayName,
  const std::vector<std::string>& variableArrayNames, const std::vector<double>& variables,
  bool writeNormal, bool writeVariables, bool useRadialValueForNormal,
  std::vector<unsigned int>& volumeNormalWriteCounts,
  std::vector<std::vector<unsigned int>>& volumeVariableWriteCounts)
{
  VolumeBoundaryDuplicateWrites duplicates;
  if (!side || !volume || (!writeNormal && !writeVariables))
  {
    return duplicates;
  }
  if (writeNormal && (!vectorArrayName || vectorArrayName[0] == '\0'))
  {
    return duplicates;
  }
  if (writeVariables && (variableArrayNames.empty() || variables.empty()))
  {
    return duplicates;
  }

  double normal[3] = { 0.0, 0.0, 0.0 };
  if (writeNormal && !ComputeAverageCellNormal(side, normal))
  {
    return duplicates;
  }

  vtkDataArray* sideValues = (writeNormal && useRadialValueForNormal)
    ? vtkDataArray::SafeDownCast(side->GetPointData()->GetArray(radialArrayName))
    : nullptr;
  vtkDataArray* sideGids = vtkDataArray::SafeDownCast(side->GetPointData()->GetGlobalIds());
  vtkDoubleArray* volumeVectors = writeNormal
    ? vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(vectorArrayName))
    : nullptr;
  if (!sideGids || sideGids->GetNumberOfTuples() != side->GetNumberOfPoints())
  {
    return duplicates;
  }
  if (writeNormal &&
    (!volumeVectors || volumeVectors->GetNumberOfComponents() != 3 ||
      volumeVectors->GetNumberOfTuples() != volume->GetNumberOfPoints()))
  {
    return duplicates;
  }
  if (writeNormal && useRadialValueForNormal &&
    (!sideValues || sideValues->GetNumberOfComponents() < 1 ||
      sideValues->GetNumberOfTuples() != side->GetNumberOfPoints()))
  {
    return duplicates;
  }

  std::vector<vtkDoubleArray*> volumeVariables;
  if (writeVariables)
  {
    volumeVariables.reserve(variableArrayNames.size());
    for (const std::string& arrayName : variableArrayNames)
    {
      vtkDoubleArray* arr =
        vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(arrayName.c_str()));
      if (!arr || arr->GetNumberOfComponents() != 1 ||
        arr->GetNumberOfTuples() != volume->GetNumberOfPoints())
      {
        return duplicates;
      }
      volumeVariables.push_back(arr);
    }
  }

  const vtkIdType nVolumePts = volume->GetNumberOfPoints();
  if (static_cast<vtkIdType>(volumeNormalWriteCounts.size()) != nVolumePts)
  {
    volumeNormalWriteCounts.assign(static_cast<size_t>(nVolumePts), 0);
  }
  if (writeVariables)
  {
    if (volumeVariableWriteCounts.size() != volumeVariables.size())
    {
      volumeVariableWriteCounts.assign(volumeVariables.size(), {});
    }
    for (std::vector<unsigned int>& counts : volumeVariableWriteCounts)
    {
      if (static_cast<vtkIdType>(counts.size()) != nVolumePts)
      {
        counts.assign(static_cast<size_t>(nVolumePts), 0);
      }
    }
  }

  vtkIdType duplicateNormalWrites = 0;
  vtkIdType duplicateVariableWrites = 0;
  for (vtkIdType p = 0; p < side->GetNumberOfPoints(); ++p)
  {
    const vtkIdType volumePointId = static_cast<vtkIdType>(sideGids->GetComponent(p, 0)) - 1;
    if (volumePointId < 0 || volumePointId >= nVolumePts)
    {
      continue;
    }

    if (writeNormal)
    {
      const double v = useRadialValueForNormal ? sideValues->GetComponent(p, 0) : 1.0;
      if (!vtkMath::IsFinite(v))
      {
        continue;
      }

      if (volumeNormalWriteCounts[static_cast<size_t>(volumePointId)] > 0)
      {
        ++duplicateNormalWrites;
      }

      const double normalWeight = useRadialValueForNormal ? v : 1.0;
      const double contribution[3] = { normalWeight * normal[0], normalWeight * normal[1],
        normalWeight * normal[2] };
      unsigned int& writeCount = volumeNormalWriteCounts[static_cast<size_t>(volumePointId)];
      if (writeCount == 0)
      {
        volumeVectors->SetTuple(volumePointId, contribution);
      }
      else
      {
        double out[3];
        volumeVectors->GetTuple(volumePointId, out);
        out[0] += contribution[0];
        out[1] += contribution[1];
        out[2] += contribution[2];
        volumeVectors->SetTuple(volumePointId, out);
      }
      ++writeCount;
    }

    if (writeVariables)
    {
      bool wroteAnyVariable = false;
      bool duplicateThisPoint = false;
      for (size_t i = 0; i < volumeVariables.size(); ++i)
      {
        if (i >= variables.size() || !vtkMath::IsFinite(variables[i]))
        {
          continue;
        }

        unsigned int& writeCount = volumeVariableWriteCounts[i][static_cast<size_t>(volumePointId)];
        if (writeCount > 0)
        {
          duplicateThisPoint = true;
        }

        const double value = variables[i];
        const double current = volumeVariables[i]->GetValue(volumePointId);
        if (!vtkMath::IsFinite(current) || writeCount == 0)
        {
          volumeVariables[i]->SetValue(volumePointId, value);
        }
        else
        {
          volumeVariables[i]->SetValue(volumePointId, current + value);
        }
        ++writeCount;
        wroteAnyVariable = true;
      }
      if (wroteAnyVariable && duplicateThisPoint)
      {
        ++duplicateVariableWrites;
      }
    }
  }
  duplicates.normal = duplicateNormalWrites;
  duplicates.variables = duplicateVariableWrites;
  return duplicates;
}

void AverageRepeatedBoundaryRadialNormalWrites(vtkUnstructuredGrid* volume, const char* vectorArrayName,
  const std::vector<std::string>& variableArrayNames,
  const std::vector<unsigned int>& normalWriteCounts,
  const std::vector<std::vector<unsigned int>>& variableWriteCounts)
{
  if (!volume || !vectorArrayName || vectorArrayName[0] == '\0')
  {
    return;
  }

  vtkDoubleArray* volumeVectors =
    vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(vectorArrayName));
  if (!volumeVectors || volumeVectors->GetNumberOfComponents() != 3 ||
    volumeVectors->GetNumberOfTuples() != volume->GetNumberOfPoints() ||
    static_cast<vtkIdType>(normalWriteCounts.size()) != volume->GetNumberOfPoints())
  {
    return;
  }

  std::vector<vtkDoubleArray*> volumeVariables;
  volumeVariables.reserve(variableArrayNames.size());
  for (const std::string& arrayName : variableArrayNames)
  {
    vtkDoubleArray* arr = vtkDoubleArray::SafeDownCast(volume->GetPointData()->GetArray(arrayName.c_str()));
    if (arr && arr->GetNumberOfComponents() == 1 &&
      arr->GetNumberOfTuples() == volume->GetNumberOfPoints())
    {
      volumeVariables.push_back(arr);
    }
  }

  for (vtkIdType p = 0; p < volume->GetNumberOfPoints(); ++p)
  {
    const unsigned int normalCount = normalWriteCounts[static_cast<size_t>(p)];
    if (normalCount > 1)
    {
      double out[3];
      volumeVectors->GetTuple(p, out);
      out[0] /= static_cast<double>(normalCount);
      out[1] /= static_cast<double>(normalCount);
      out[2] /= static_cast<double>(normalCount);
      volumeVectors->SetTuple(p, out);
    }

    for (size_t i = 0; i < volumeVariables.size(); ++i)
    {
      if (i >= variableWriteCounts.size() ||
        static_cast<vtkIdType>(variableWriteCounts[i].size()) != volume->GetNumberOfPoints())
      {
        continue;
      }
      const unsigned int variableCount = variableWriteCounts[i][static_cast<size_t>(p)];
      if (variableCount <= 1)
      {
        continue;
      }
      volumeVariables[i]->SetValue(
        p, volumeVariables[i]->GetValue(p) / static_cast<double>(variableCount));
    }
  }
}

void AddBoundaryVolumeFieldArraysToSide(vtkPolyData* side, const char* radialArrayName,
  const char* vectorArrayName, const std::vector<std::string>& variableArrayNames,
  const std::vector<double>& variables, bool useRadialValueForNormal)
{
  if (!side || !vectorArrayName || vectorArrayName[0] == '\0')
  {
    return;
  }

  vtkDataArray* radial = useRadialValueForNormal
    ? vtkDataArray::SafeDownCast(side->GetPointData()->GetArray(radialArrayName))
    : nullptr;
  if (useRadialValueForNormal &&
    (!radial || radial->GetNumberOfComponents() < 1 ||
      radial->GetNumberOfTuples() != side->GetNumberOfPoints()))
  {
    return;
  }

  double normal[3];
  if (!ComputeAverageCellNormal(side, normal))
  {
    normal[0] = normal[1] = normal[2] = 0.0;
  }

  vtkNew<vtkDoubleArray> normalArray;
  normalArray->SetName(vectorArrayName);
  normalArray->SetNumberOfComponents(3);
  normalArray->SetNumberOfTuples(side->GetNumberOfPoints());
  for (vtkIdType p = 0; p < side->GetNumberOfPoints(); ++p)
  {
    const double v = useRadialValueForNormal ? radial->GetComponent(p, 0) : 1.0;
    const double weight = useRadialValueForNormal && vtkMath::IsFinite(v) ? v : 1.0;
    const double tuple[3] = { weight * normal[0], weight * normal[1], weight * normal[2] };
    normalArray->SetTuple(p, tuple);
  }
  side->GetPointData()->RemoveArray(vectorArrayName);
  side->GetPointData()->AddArray(normalArray);

  for (size_t i = 0; i < variableArrayNames.size(); ++i)
  {
    vtkNew<vtkDoubleArray> arr;
    arr->SetName(variableArrayNames[i].c_str());
    arr->SetNumberOfComponents(1);
    arr->SetNumberOfTuples(side->GetNumberOfPoints());
    const double value = i < variables.size() && vtkMath::IsFinite(variables[i])
      ? variables[i]
      : std::numeric_limits<double>::quiet_NaN();
    arr->Fill(value);
    side->GetPointData()->RemoveArray(variableArrayNames[i].c_str());
    side->GetPointData()->AddArray(arr);
  }
}

std::vector<std::string> ParseBlockNames(const char* names)
{
  std::vector<std::string> result;
  if (!names || names[0] == '\0')
  {
    return result;
  }

  std::stringstream stream(names);
  std::string line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    result.push_back(line);
  }
  return result;
}

std::vector<std::vector<double>> ParseLineDoubleMatrix(const char* values)
{
  const double nanv = std::numeric_limits<double>::quiet_NaN();
  std::vector<std::vector<double>> result;
  if (!values || values[0] == '\0')
  {
    return result;
  }

  std::stringstream stream(values);
  std::string line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    std::vector<double> row;
    size_t start = 0;
    while (start <= line.size())
    {
      const size_t tab = line.find('\t', start);
      std::string token =
        tab == std::string::npos ? line.substr(start) : line.substr(start, tab - start);
      while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
      {
        token.erase(token.begin());
      }
      while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
      {
        token.pop_back();
      }

      if (token.empty())
      {
        row.push_back(nanv);
      }
      else
      {
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() && end && *end == '\0' && vtkMath::IsFinite(value))
        {
          row.push_back(value);
        }
        else
        {
          row.push_back(nanv);
        }
      }

      if (tab == std::string::npos)
      {
        break;
      }
      start = tab + 1;
    }
    if (row.empty())
    {
      row.push_back(nanv);
    }
    result.push_back(row);
  }
  return result;
}

std::vector<double> ResolveLineDoubles(
  const std::vector<std::vector<double>>& values, unsigned int blockIndex, size_t nVariables)
{
  std::vector<double> result(nVariables, std::numeric_limits<double>::quiet_NaN());
  if (blockIndex >= values.size())
  {
    return result;
  }
  for (size_t i = 0; i < nVariables && i < values[blockIndex].size(); ++i)
  {
    result[i] = values[blockIndex][i];
  }
  return result;
}

size_t CountBoundaryVariables(const std::vector<std::vector<double>>& values)
{
  size_t nVariables = 1;
  for (const auto& row : values)
  {
    nVariables = std::max(nVariables, row.size());
  }
  return nVariables;
}

bool HasFiniteBoundaryVariableRow(const std::vector<double>& values)
{
  for (double value : values)
  {
    if (vtkMath::IsFinite(value))
    {
      return true;
    }
  }
  return false;
}

std::vector<int> ParseLineIntFlags(const char* values)
{
  std::vector<int> result;
  if (!values || values[0] == '\0')
  {
    return result;
  }

  std::stringstream stream(values);
  std::string line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    std::stringstream lineStream(line);
    int flag = 0;
    lineStream >> flag;
    result.push_back(flag != 0 ? 1 : 0);
  }
  return result;
}

bool ResolveWriteNormal(const std::vector<int>& flags, unsigned int blockIndex)
{
  return blockIndex < flags.size() && flags[blockIndex] != 0;
}

std::string ResolveBlockNameOrExisting(vtkPartitionedDataSetCollection* coll, unsigned int pdcIndex,
  const std::vector<std::string>& customNames, unsigned int blockIndex, const std::string& fallback)
{
  if (blockIndex < customNames.size() && !customNames[blockIndex].empty())
  {
    return customNames[blockIndex];
  }
  if (coll)
  {
    if (vtkInformation* meta = coll->GetMetaData(pdcIndex))
    {
      const char* existing = meta->Get(vtkCompositeDataSet::NAME());
      if (existing && existing[0] != '\0')
      {
        return existing;
      }
    }
  }
  return fallback;
}

std::string ResolveNodeSetBlockNameOrExisting(vtkPartitionedDataSetCollection* coll,
  unsigned int nodePdcIndex, const std::vector<std::string>& customNames,
  unsigned int customSideOffset, unsigned int nSideNodePairs, unsigned int pairIndex,
  const std::string& sideName)
{
  const unsigned int nodeNameIndex = customSideOffset + nSideNodePairs + pairIndex;
  const std::string fallback = std::string(kNodeSetNamePrefix) + sideName;
  return ResolveBlockNameOrExisting(
    coll, nodePdcIndex, customNames, nodeNameIndex, fallback);
}

int ReadEntityIdFromMeta(vtkPartitionedDataSetCollection* coll, unsigned int blockIndex, int fallback)
{
  vtkInformation* meta = coll ? coll->GetMetaData(blockIndex) : nullptr;
  if (!meta || !meta->Has(vtkIOSSReader::ENTITY_ID()))
  {
    return fallback;
  }
  return meta->Get(vtkIOSSReader::ENTITY_ID());
}

void SetIossBlockMeta(
  vtkPartitionedDataSetCollection* coll, unsigned int pdsIdx, const char* name, int entityId)
{
  vtkInformation* meta = coll->GetMetaData(pdsIdx);
  if (!meta)
  {
    return;
  }
  meta->Set(vtkCompositeDataSet::NAME(), name);
  meta->Set(vtkIOSSReader::ENTITY_ID(), entityId);
}

void BuildIossAssemblyFromLayout(vtkPartitionedDataSetCollection* coll,
  const PartitionedCollectionLayout& layout, const std::vector<std::string>& blockNames)
{
  vtkNew<vtkDataAssembly> rootAsm;
  rootAsm->SetRootNodeName("IOSS");

  const int elemBlocksNode = rootAsm->AddNode("element_blocks");
  const int sideSetsNode = rootAsm->AddNode("side_sets");
  const int nodeSetsNode = rootAsm->AddNode("node_sets");

  const unsigned int nSideNodePairs = static_cast<unsigned int>(layout.SideSetPdcIndices.size());
  const unsigned int customSideOffset = layout.HasElementBlock ? 1u : 0u;

  if (layout.HasElementBlock)
  {
    const std::string elemName =
      ResolveBlockNameOrExisting(coll, layout.ElementBlockPdcIndex, blockNames, 0, "tetrahedra");
    const int leaf = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(elemName.c_str()).c_str(), elemBlocksNode);
    rootAsm->SetAttribute(leaf, "label", elemName.c_str());
    rootAsm->AddDataSetIndex(leaf, layout.ElementBlockPdcIndex);
  }

  for (unsigned int i = 0; i < nSideNodePairs; ++i)
  {
    const std::string sideName = ResolveBlockNameOrExisting(
      coll, layout.SideSetPdcIndices[i], blockNames, customSideOffset + i, "side" + std::to_string(i));
    const std::string nodeName = ResolveNodeSetBlockNameOrExisting(coll,
      layout.NodeSetPdcIndices[i], blockNames, customSideOffset, nSideNodePairs, i, sideName);
    const int leafN = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(nodeName.c_str()).c_str(), nodeSetsNode);
    rootAsm->SetAttribute(leafN, "label", nodeName.c_str());
    rootAsm->AddDataSetIndex(leafN, layout.NodeSetPdcIndices[i]);
  }
  for (unsigned int i = 0; i < nSideNodePairs; ++i)
  {
    const std::string sideName = ResolveBlockNameOrExisting(
      coll, layout.SideSetPdcIndices[i], blockNames, customSideOffset + i, "side" + std::to_string(i));
    const int leafS = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(sideName.c_str()).c_str(), sideSetsNode);
    rootAsm->SetAttribute(leafS, "label", sideName.c_str());
    rootAsm->AddDataSetIndex(leafS, layout.SideSetPdcIndices[i]);
  }

  coll->SetDataAssembly(rootAsm);
}

void ApplyBlockNamesFromLayout(vtkPartitionedDataSetCollection* coll,
  const PartitionedCollectionLayout& layout, const std::vector<std::string>& blockNames)
{
  const unsigned int nSideNodePairs = static_cast<unsigned int>(layout.SideSetPdcIndices.size());
  const unsigned int customSideOffset = layout.HasElementBlock ? 1u : 0u;
  int nextEntityId = 1;

  if (layout.HasElementBlock)
  {
    const std::string elemName =
      ResolveBlockNameOrExisting(coll, layout.ElementBlockPdcIndex, blockNames, 0, "tetrahedra");
    const int entityId =
      ReadEntityIdFromMeta(coll, layout.ElementBlockPdcIndex, nextEntityId++);
    SetIossBlockMeta(coll, layout.ElementBlockPdcIndex, elemName.c_str(), entityId);
  }

  for (unsigned int i = 0; i < nSideNodePairs; ++i)
  {
    const std::string sideName = ResolveBlockNameOrExisting(
      coll, layout.SideSetPdcIndices[i], blockNames, customSideOffset + i, "side" + std::to_string(i));
    const std::string nodeName = ResolveNodeSetBlockNameOrExisting(coll,
      layout.NodeSetPdcIndices[i], blockNames, customSideOffset, nSideNodePairs, i, sideName);
    const int nodeEntityId = ReadEntityIdFromMeta(coll, layout.NodeSetPdcIndices[i], nextEntityId++);
    SetIossBlockMeta(coll, layout.NodeSetPdcIndices[i], nodeName.c_str(), nodeEntityId);
    const int sideEntityId = ReadEntityIdFromMeta(coll, layout.SideSetPdcIndices[i], nextEntityId++);
    SetIossBlockMeta(coll, layout.SideSetPdcIndices[i], sideName.c_str(), sideEntityId);
  }

  BuildIossAssemblyFromLayout(coll, layout, blockNames);
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXPartitionedCollectionBoundaryFields::vtkSHYXPartitionedCollectionBoundaryFields()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetBoundaryVariables("");
  this->SetBoundaryWriteNormals("");
}

//------------------------------------------------------------------------------
vtkSHYXPartitionedCollectionBoundaryFields::~vtkSHYXPartitionedCollectionBoundaryFields()
{
  this->SetBoundaryVariables(nullptr);
  this->SetBoundaryWriteNormals(nullptr);
  this->SetBlockNames(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXPartitionedCollectionBoundaryFields::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ComputeBoundaryRadialValue: " << this->ComputeBoundaryRadialValue << "\n";
  os << indent << "BoundaryRadialNormalFalloffFactor: "
     << this->BoundaryRadialNormalFalloffFactor << "\n";
  os << indent << "BoundaryVariables: "
     << (this->BoundaryVariables ? this->BoundaryVariables : "(null)") << "\n";
  os << indent << "BoundaryWriteNormals: "
     << (this->BoundaryWriteNormals ? this->BoundaryWriteNormals : "(null)") << "\n";
  os << indent << "BlockNames: " << (this->BlockNames ? this->BlockNames : "(null)") << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionBoundaryFields::FillInputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionBoundaryFields::FillOutputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionBoundaryFields::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPartitionedDataSetCollection* input =
    vtkPartitionedDataSetCollection::GetData(inputVector[0], 0);
  vtkPartitionedDataSetCollection* output =
    vtkPartitionedDataSetCollection::GetData(outputVector, 0);

  if (!input)
  {
    vtkErrorMacro(<< "Input is null.");
    return 0;
  }
  if (!output)
  {
    vtkErrorMacro(<< "Output PartitionedDataSetCollection is null.");
    return 0;
  }

  vtkNew<vtkPartitionedDataSetCollection> result;
  result->DeepCopy(input);

  vtkDataAssembly* assembly = input->GetDataAssembly();
  PartitionedCollectionLayout layout;
  if (!ParsePartitionedCollectionLayout(assembly, &layout))
  {
    vtkWarningMacro(<< "Input vtkDataAssembly is missing or does not describe element/side/node sets. "
                    << "Passing through unchanged.");
    output->ShallowCopy(result);
    return 1;
  }

  const unsigned int nSideSets = static_cast<unsigned int>(layout.SideSetPdcIndices.size());
  if (nSideSets == 0)
  {
    vtkWarningMacro(<< "No side sets found in vtkDataAssembly. Passing through unchanged.");
    output->ShallowCopy(result);
    return 1;
  }

  if (layout.NodeSetPdcIndices.size() != layout.SideSetPdcIndices.size())
  {
    vtkWarningMacro(<< "Node set count (" << layout.NodeSetPdcIndices.size()
                    << ") differs from side set count (" << layout.SideSetPdcIndices.size()
                    << "). Pairing by minimum count.");
  }
  const unsigned int nPairs =
    static_cast<unsigned int>(std::min(layout.NodeSetPdcIndices.size(), layout.SideSetPdcIndices.size()));

  const std::vector<std::string> customBlockNames = ParseBlockNames(this->BlockNames);
  const std::vector<std::vector<double>> boundaryVariables =
    ParseLineDoubleMatrix(this->BoundaryVariables);
  const std::vector<int> boundaryWriteNormals = ParseLineIntFlags(this->BoundaryWriteNormals);
  const bool computeBoundaryRadialValue = this->ComputeBoundaryRadialValue != 0;
  const size_t nBoundaryVariables = CountBoundaryVariables(boundaryVariables);
  const std::vector<std::string> boundaryVariableArrayNames =
    BoundaryVariableArrayNames(nBoundaryVariables);
  const unsigned int customSideOffset = layout.HasElementBlock ? 1u : 0u;

  vtkUnstructuredGrid* volumeGrid = nullptr;
  if (layout.HasElementBlock)
  {
    volumeGrid =
      vtkUnstructuredGrid::SafeDownCast(GetDataSetFromPdcBlock(result, layout.ElementBlockPdcIndex));
    if (!volumeGrid)
    {
      vtkWarningMacro(<< "Element block at index " << layout.ElementBlockPdcIndex
                      << " is not vtkUnstructuredGrid; volume accumulation is disabled.");
    }
  }

  std::vector<unsigned int> volumeNormalWriteCounts;
  std::vector<std::vector<unsigned int>> volumeVariableWriteCounts;
  if (volumeGrid)
  {
    InitializeVolumeBoundaryRadialNormalArray(volumeGrid, kBoundaryRadialNormalArrayName);
    InitializeVolumeBoundaryVariableArrays(volumeGrid, boundaryVariableArrayNames);
    volumeNormalWriteCounts.assign(static_cast<size_t>(volumeGrid->GetNumberOfPoints()), 0);
    volumeVariableWriteCounts.assign(nBoundaryVariables, {});
    for (std::vector<unsigned int>& counts : volumeVariableWriteCounts)
    {
      counts.assign(static_cast<size_t>(volumeGrid->GetNumberOfPoints()), 0);
    }
    SetPartitionDataSetBlock(result, layout.ElementBlockPdcIndex, volumeGrid);
  }

  for (unsigned int i = 0; i < nPairs; ++i)
  {
    vtkDataSet* sideDs = GetDataSetFromPdcBlock(result, layout.SideSetPdcIndices[i]);
    vtkSmartPointer<vtkPolyData> sideInput = ForceDataSetToPolyData(sideDs);
    if (!sideInput)
    {
      vtkWarningMacro(<< "Side set at PDC index " << layout.SideSetPdcIndices[i]
                      << " could not be converted to vtkPolyData; skipping.");
      continue;
    }

    vtkNew<vtkPolyData> sideCopy;
    sideCopy->DeepCopy(sideInput);
    if (computeBoundaryRadialValue)
    {
      AddBoundaryRadialValueArray(
        sideCopy, kBoundaryRadialValueArrayName, this->BoundaryRadialNormalFalloffFactor);
    }

    const unsigned int sideBlockIndex = customSideOffset + i;
    const std::vector<double> variables =
      ResolveLineDoubles(boundaryVariables, sideBlockIndex, nBoundaryVariables);
    AddBoundaryVolumeFieldArraysToSide(sideCopy,
      computeBoundaryRadialValue ? kBoundaryRadialValueArrayName : nullptr,
      kBoundaryRadialNormalArrayName, boundaryVariableArrayNames, variables,
      computeBoundaryRadialValue);

    if (volumeGrid)
    {
      const bool writeNormal = ResolveWriteNormal(boundaryWriteNormals, sideBlockIndex);
      const bool writeVariables = HasFiniteBoundaryVariableRow(variables);
      if (writeNormal || writeVariables)
      {
        const VolumeBoundaryDuplicateWrites duplicateWrites =
          AccumulateBoundaryRadialNormalToVolume(sideCopy, volumeGrid,
            computeBoundaryRadialValue ? kBoundaryRadialValueArrayName : nullptr,
            kBoundaryRadialNormalArrayName, boundaryVariableArrayNames, variables, writeNormal,
            writeVariables, computeBoundaryRadialValue, volumeNormalWriteCounts,
            volumeVariableWriteCounts);
        if (duplicateWrites.normal > 0 || duplicateWrites.variables > 0)
        {
          std::ostringstream msg;
          msg << "Averaging repeated volume boundary point writes for side" << i << ":";
          if (duplicateWrites.normal > 0)
          {
            msg << " " << duplicateWrites.normal << " for " << kBoundaryRadialNormalArrayName;
          }
          if (duplicateWrites.variables > 0)
          {
            msg << (duplicateWrites.normal > 0 ? "; " : " ") << duplicateWrites.variables << " for "
                << JoinArrayNames(boundaryVariableArrayNames);
          }
          msg << ".";
          vtkWarningMacro(<< msg.str());
        }
      }
    }

    SetContiguousCellGlobalIdsPolyData(sideCopy);
    SetPartitionDataSetBlock(result, layout.SideSetPdcIndices[i], sideCopy);

    vtkSmartPointer<vtkPolyData> nodeCopy = BuildNodeSetPolyData(sideCopy);
    SetPartitionDataSetBlock(result, layout.NodeSetPdcIndices[i], nodeCopy);
  }

  if (volumeGrid)
  {
    AverageRepeatedBoundaryRadialNormalWrites(volumeGrid, kBoundaryRadialNormalArrayName,
      boundaryVariableArrayNames, volumeNormalWriteCounts, volumeVariableWriteCounts);
    SetPartitionDataSetBlock(result, layout.ElementBlockPdcIndex, volumeGrid);
  }

  layout.SideSetPdcIndices.resize(nPairs);
  layout.NodeSetPdcIndices.resize(nPairs);
  ApplyBlockNamesFromLayout(result, layout, customBlockNames);

  output->ShallowCopy(result);
  return 1;
}

VTK_ABI_NAMESPACE_END
