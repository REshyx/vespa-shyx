#include "vtkSHYXPartitionedCollectionBoundaryAssignment.h"
#include "vtkSHYXBoundaryAssignmentOptionsTemplates.h"

#include <vtkAppendPolyData.h>
#include <vtkArrowSource.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataAssembly.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkGeometryFilter.h>
#include <vtkGlyph3D.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
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
#include <vtkStringArray.h>
#include <vtkTriangle.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXPartitionedCollectionBoundaryAssignment);

namespace
{
struct PartitionedCollectionLayout
{
  bool HasElementBlock = false;
  unsigned int ElementBlockPdcIndex = 0;
  std::vector<unsigned int> NodeSetPdcIndices;
  std::vector<unsigned int> SideSetPdcIndices;
};

enum class SideBoundaryRole
{
  Wall = 0,
  Inlet = 1,
  Outlet = 2,
};

struct LabelSite
{
  int EntityId = 0;
  double Cx = 0.0;
  double Cy = 0.0;
  double Cz = 0.0;
};

struct InletOptStats
{
  int EntityId = 0;
  double nx = 0.0;
  double ny = 0.0;
  double nz = 0.0;
  double xi = 0.0;
  double yi = 0.0;
  double zi = 0.0;
  double xf = 0.0;
  double yf = 0.0;
  double zf = 0.0;
  double area = 0.0;
};

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

int ReadEntityIdFromMeta(vtkPartitionedDataSetCollection* coll, unsigned int blockIndex, int fallback)
{
  vtkInformation* meta = coll ? coll->GetMetaData(blockIndex) : nullptr;
  if (!meta || !meta->Has(vtkIOSSReader::ENTITY_ID()))
  {
    return fallback;
  }
  return meta->Get(vtkIOSSReader::ENTITY_ID());
}

std::string ReadBlockNameFromMeta(
  vtkPartitionedDataSetCollection* coll, unsigned int blockIndex, const std::string& fallback)
{
  vtkInformation* meta = coll ? coll->GetMetaData(blockIndex) : nullptr;
  if (meta && meta->Has(vtkCompositeDataSet::NAME()))
  {
    const char* name = meta->Get(vtkCompositeDataSet::NAME());
    if (name && name[0] != '\0')
    {
      return name;
    }
  }
  return fallback;
}

void SetIossBlockMeta(
  vtkPartitionedDataSetCollection* coll, unsigned int pdsIdx, const char* name, int entityId)
{
  if (!coll)
  {
    return;
  }
  vtkInformation* meta = coll->GetMetaData(pdsIdx);
  if (!meta)
  {
    return;
  }
  meta->Set(vtkCompositeDataSet::NAME(), name);
  meta->Set(vtkIOSSReader::ENTITY_ID(), entityId);
}

void SetPartitionDataSetBlock(
  vtkPartitionedDataSetCollection* coll, unsigned int blockIndex, vtkDataObject* ds)
{
  if (!coll || !ds)
  {
    return;
  }
  vtkNew<vtkPartitionedDataSet> pds;
  pds->SetNumberOfPartitions(1);
  pds->SetPartition(0, ds);
  coll->SetPartitionedDataSet(blockIndex, pds);
}

void SetContiguousCellGlobalIdsPolyData(vtkPolyData* pd)
{
  if (!pd)
  {
    return;
  }
  const vtkIdType nc = pd->GetNumberOfCells();
  vtkNew<vtkIdTypeArray> cg;
  cg->SetName("GlobalIds");
  cg->SetNumberOfComponents(1);
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
  if (!sideSurface || !sideSurface->GetPoints())
  {
    return out;
  }
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

vtkSmartPointer<vtkPolyData> DeepCopyPolyDataOrEmpty(vtkDataSet* ds)
{
  vtkSmartPointer<vtkPolyData> pd = ForceDataSetToPolyData(ds);
  if (!pd)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }
  vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
  copy->DeepCopy(pd);
  return copy;
}

void BuildIossAssemblyForPairs(vtkPartitionedDataSetCollection* coll, bool hasElement,
  unsigned int elementIndex, const std::vector<unsigned int>& nodeIndices,
  const std::vector<unsigned int>& sideIndices, const std::vector<std::string>& sideNames,
  const std::vector<std::string>& nodeNames)
{
  vtkNew<vtkDataAssembly> rootAsm;
  rootAsm->SetRootNodeName("IOSS");
  const int elemBlocksNode = rootAsm->AddNode("element_blocks");
  const int sideSetsNode = rootAsm->AddNode("side_sets");
  const int nodeSetsNode = rootAsm->AddNode("node_sets");

  if (hasElement)
  {
    const std::string elemName = ReadBlockNameFromMeta(coll, elementIndex, "tetrahedra");
    const int leaf = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(elemName.c_str()).c_str(), elemBlocksNode);
    rootAsm->SetAttribute(leaf, "label", elemName.c_str());
    rootAsm->AddDataSetIndex(leaf, elementIndex);
  }

  const size_t nPairs = std::min(nodeIndices.size(), sideIndices.size());
  for (size_t i = 0; i < nPairs; ++i)
  {
    const std::string& nodeName =
      (i < nodeNames.size() && !nodeNames[i].empty()) ? nodeNames[i] : ("node_side" + std::to_string(i));
    const int leafN = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(nodeName.c_str()).c_str(), nodeSetsNode);
    rootAsm->SetAttribute(leafN, "label", nodeName.c_str());
    rootAsm->AddDataSetIndex(leafN, nodeIndices[i]);
  }
  for (size_t i = 0; i < nPairs; ++i)
  {
    const std::string& sideName =
      (i < sideNames.size() && !sideNames[i].empty()) ? sideNames[i] : ("side" + std::to_string(i));
    const int leafS = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(sideName.c_str()).c_str(), sideSetsNode);
    rootAsm->SetAttribute(leafS, "label", sideName.c_str());
    rootAsm->AddDataSetIndex(leafS, sideIndices[i]);
  }
  coll->SetDataAssembly(rootAsm);
}

/**
 * Rebuild coll so classified inlets become one side/node pair. Non-inlet pairs keep relative
 * area-sorted order; the merged inlet is appended. Returns false if fewer than two inlets.
 */
bool MergeInletPairsIntoOneSideSet(vtkPartitionedDataSetCollection* coll,
  const PartitionedCollectionLayout& sortedLayout, const std::vector<SideBoundaryRole>& roles,
  std::vector<int>* outSideEntityIds, std::vector<SideBoundaryRole>* outRoles,
  std::map<int, LabelSite>* centersByEntityId)
{
  if (!coll || !outSideEntityIds || !outRoles)
  {
    return false;
  }

  const unsigned int nPairs =
    static_cast<unsigned int>(std::min(sortedLayout.SideSetPdcIndices.size(), roles.size()));
  std::vector<unsigned int> keepIndices;
  std::vector<unsigned int> inletIndices;
  keepIndices.reserve(nPairs);
  inletIndices.reserve(nPairs);
  for (unsigned int i = 0; i < nPairs; ++i)
  {
    if (roles[i] == SideBoundaryRole::Inlet)
    {
      inletIndices.push_back(i);
    }
    else
    {
      keepIndices.push_back(i);
    }
  }
  if (inletIndices.size() < 2)
  {
    return false;
  }

  vtkNew<vtkAppendPolyData> appendSides;
  for (unsigned int idx : inletIndices)
  {
    vtkSmartPointer<vtkPolyData> sidePd = DeepCopyPolyDataOrEmpty(
      GetDataSetFromPdcBlock(coll, sortedLayout.SideSetPdcIndices[idx]));
    if (sidePd->GetNumberOfCells() > 0 || sidePd->GetNumberOfPoints() > 0)
    {
      appendSides->AddInputData(sidePd);
    }
  }
  vtkSmartPointer<vtkPolyData> mergedSide = vtkSmartPointer<vtkPolyData>::New();
  if (appendSides->GetNumberOfInputConnections(0) > 0)
  {
    appendSides->Update();
    mergedSide->DeepCopy(appendSides->GetOutput());
  }
  SetContiguousCellGlobalIdsPolyData(mergedSide);
  vtkSmartPointer<vtkPolyData> mergedNode = BuildNodeSetPolyData(mergedSide);

  const unsigned int firstInlet = inletIndices.front();
  const int mergedSideEntityId = ReadEntityIdFromMeta(
    coll, sortedLayout.SideSetPdcIndices[firstInlet], static_cast<int>(firstInlet + 1));
  const int mergedNodeEntityId = ReadEntityIdFromMeta(
    coll, sortedLayout.NodeSetPdcIndices[firstInlet], mergedSideEntityId);
  const std::string mergedSideName =
    ReadBlockNameFromMeta(coll, sortedLayout.SideSetPdcIndices[firstInlet], "inlets_merged");
  const std::string mergedNodeName =
    ReadBlockNameFromMeta(coll, sortedLayout.NodeSetPdcIndices[firstInlet], "node_" + mergedSideName);

  struct KeptPair
  {
    vtkSmartPointer<vtkPolyData> Side;
    vtkSmartPointer<vtkPolyData> Node;
    int SideEntityId = 0;
    int NodeEntityId = 0;
    std::string SideName;
    std::string NodeName;
    SideBoundaryRole Role = SideBoundaryRole::Wall;
  };

  std::vector<KeptPair> outPairs;
  outPairs.reserve(keepIndices.size() + 1);
  for (unsigned int idx : keepIndices)
  {
    KeptPair pair;
    pair.Side =
      DeepCopyPolyDataOrEmpty(GetDataSetFromPdcBlock(coll, sortedLayout.SideSetPdcIndices[idx]));
    pair.Node =
      DeepCopyPolyDataOrEmpty(GetDataSetFromPdcBlock(coll, sortedLayout.NodeSetPdcIndices[idx]));
    if (pair.Node->GetNumberOfPoints() == 0 && pair.Side->GetNumberOfPoints() > 0)
    {
      pair.Node = BuildNodeSetPolyData(pair.Side);
    }
    pair.SideEntityId =
      ReadEntityIdFromMeta(coll, sortedLayout.SideSetPdcIndices[idx], static_cast<int>(idx + 1));
    pair.NodeEntityId =
      ReadEntityIdFromMeta(coll, sortedLayout.NodeSetPdcIndices[idx], pair.SideEntityId);
    pair.SideName =
      ReadBlockNameFromMeta(coll, sortedLayout.SideSetPdcIndices[idx], "side" + std::to_string(idx));
    pair.NodeName = ReadBlockNameFromMeta(
      coll, sortedLayout.NodeSetPdcIndices[idx], "node_" + pair.SideName);
    pair.Role = roles[idx];
    outPairs.push_back(std::move(pair));
  }

  {
    KeptPair merged;
    merged.Side = mergedSide;
    merged.Node = mergedNode;
    merged.SideEntityId = mergedSideEntityId;
    merged.NodeEntityId = mergedNodeEntityId;
    merged.SideName = mergedSideName;
    merged.NodeName = mergedNodeName;
    merged.Role = SideBoundaryRole::Inlet;
    outPairs.push_back(std::move(merged));
  }

  vtkSmartPointer<vtkDataObject> elementCopy;
  unsigned int elementEntityId = 1;
  std::string elementName = "tetrahedra";
  if (sortedLayout.HasElementBlock)
  {
    vtkDataSet* elemDs = GetDataSetFromPdcBlock(coll, sortedLayout.ElementBlockPdcIndex);
    if (elemDs)
    {
      elementCopy.TakeReference(elemDs->NewInstance());
      elementCopy->DeepCopy(elemDs);
    }
    elementEntityId = static_cast<unsigned int>(
      ReadEntityIdFromMeta(coll, sortedLayout.ElementBlockPdcIndex, 1));
    elementName = ReadBlockNameFromMeta(coll, sortedLayout.ElementBlockPdcIndex, "tetrahedra");
  }

  const unsigned int nOutPairs = static_cast<unsigned int>(outPairs.size());
  const unsigned int nBlocks =
    (sortedLayout.HasElementBlock && elementCopy ? 1u : 0u) + 2u * nOutPairs;

  vtkNew<vtkPartitionedDataSetCollection> rebuilt;
  rebuilt->SetNumberOfPartitionedDataSets(nBlocks);

  unsigned int cursor = 0;
  unsigned int elementIndex = 0;
  const bool hasElement = sortedLayout.HasElementBlock && elementCopy != nullptr;
  if (hasElement)
  {
    elementIndex = cursor;
    SetPartitionDataSetBlock(rebuilt, cursor, elementCopy);
    SetIossBlockMeta(rebuilt, cursor, elementName.c_str(), static_cast<int>(elementEntityId));
    ++cursor;
  }

  std::vector<unsigned int> nodeIndices(nOutPairs);
  std::vector<unsigned int> sideIndices(nOutPairs);
  std::vector<std::string> sideNames(nOutPairs);
  std::vector<std::string> nodeNames(nOutPairs);
  for (unsigned int i = 0; i < nOutPairs; ++i)
  {
    nodeIndices[i] = cursor;
    SetPartitionDataSetBlock(rebuilt, cursor, outPairs[i].Node);
    SetIossBlockMeta(
      rebuilt, cursor, outPairs[i].NodeName.c_str(), outPairs[i].NodeEntityId);
    nodeNames[i] = outPairs[i].NodeName;
    ++cursor;
  }
  for (unsigned int i = 0; i < nOutPairs; ++i)
  {
    sideIndices[i] = cursor;
    SetPartitionDataSetBlock(rebuilt, cursor, outPairs[i].Side);
    SetIossBlockMeta(
      rebuilt, cursor, outPairs[i].SideName.c_str(), outPairs[i].SideEntityId);
    sideNames[i] = outPairs[i].SideName;
    ++cursor;
  }

  BuildIossAssemblyForPairs(
    rebuilt, hasElement, elementIndex, nodeIndices, sideIndices, sideNames, nodeNames);

  coll->DeepCopy(rebuilt);

  outSideEntityIds->clear();
  outRoles->clear();
  outSideEntityIds->reserve(nOutPairs);
  outRoles->reserve(nOutPairs);
  if (centersByEntityId)
  {
    // Keep pre-merge centers for OPT debug AABB pairing; refresh post-merge label sites.
  }
  for (unsigned int i = 0; i < nOutPairs; ++i)
  {
    outSideEntityIds->push_back(outPairs[i].SideEntityId);
    outRoles->push_back(outPairs[i].Role);
    if (centersByEntityId && outPairs[i].Side)
    {
      double bounds[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
      outPairs[i].Side->GetBounds(bounds);
      LabelSite site;
      site.EntityId = outPairs[i].SideEntityId;
      site.Cx = 0.5 * (bounds[0] + bounds[1]);
      site.Cy = 0.5 * (bounds[2] + bounds[3]);
      site.Cz = 0.5 * (bounds[4] + bounds[5]);
      (*centersByEntityId)[outPairs[i].SideEntityId] = site;
    }
  }
  return true;
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

double ComputeSurfaceArea(vtkPolyData* pd)
{
  if (!pd || !pd->GetPoints())
  {
    return 0.0;
  }

  double area = 0.0;
  vtkNew<vtkIdList> cpts;
  double p0[3], p1[3], p2[3];
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
      area += vtkTriangle::TriangleArea(p0, p1, p2);
    }
  }
  return area;
}

void SortSideNodePairsByAreaDescending(
  vtkPartitionedDataSetCollection* coll, PartitionedCollectionLayout* layout, unsigned int nPairs)
{
  if (!coll || !layout || nPairs <= 1)
  {
    return;
  }

  layout->SideSetPdcIndices.resize(nPairs);
  layout->NodeSetPdcIndices.resize(nPairs);

  std::vector<double> areas(nPairs, 0.0);
  for (unsigned int i = 0; i < nPairs; ++i)
  {
    vtkSmartPointer<vtkPolyData> sidePd =
      ForceDataSetToPolyData(GetDataSetFromPdcBlock(coll, layout->SideSetPdcIndices[i]));
    areas[i] = ComputeSurfaceArea(sidePd);
  }

  std::vector<size_t> order(nPairs);
  std::iota(order.begin(), order.end(), size_t{ 0 });
  std::stable_sort(order.begin(), order.end(),
    [&areas](size_t a, size_t b) { return areas[a] > areas[b]; });

  bool alreadySorted = true;
  for (unsigned int i = 0; i < nPairs; ++i)
  {
    if (order[i] != i)
    {
      alreadySorted = false;
      break;
    }
  }
  if (alreadySorted)
  {
    return;
  }

  std::vector<unsigned int> sortedSides(nPairs);
  std::vector<unsigned int> sortedNodes(nPairs);
  for (unsigned int newPos = 0; newPos < nPairs; ++newPos)
  {
    const size_t oldPos = order[newPos];
    sortedSides[newPos] = layout->SideSetPdcIndices[oldPos];
    sortedNodes[newPos] = layout->NodeSetPdcIndices[oldPos];
  }
  layout->SideSetPdcIndices = std::move(sortedSides);
  layout->NodeSetPdcIndices = std::move(sortedNodes);
}

SideBoundaryRole ClassifySideBoundaryRole(
  unsigned int sideIndex, unsigned int nSides, int flowBoundaryMode)
{
  if (sideIndex == 0 || nSides < 2)
  {
    return SideBoundaryRole::Wall;
  }
  const bool secondIsInlet =
    flowBoundaryMode == vtkSHYXPartitionedCollectionBoundaryAssignment::SINGLE_INLET;
  if (sideIndex == 1)
  {
    return secondIsInlet ? SideBoundaryRole::Inlet : SideBoundaryRole::Outlet;
  }
  return secondIsInlet ? SideBoundaryRole::Outlet : SideBoundaryRole::Inlet;
}

std::string FormatSpaceSeparatedInts(const std::vector<int>& values)
{
  std::ostringstream oss;
  for (size_t i = 0; i < values.size(); ++i)
  {
    if (i > 0)
    {
      oss << ' ';
    }
    oss << values[i];
  }
  return oss.str();
}

std::string FormatBoundaryAssignmentText(const std::vector<int>& sideEntityIds,
  const std::vector<SideBoundaryRole>& roles, bool customAdapter,
  const std::map<int, LabelSite>& centersByEntityId, std::map<int, LabelSite>* labelCentersOut)
{
  const size_t n = std::min(sideEntityIds.size(), roles.size());
  if (n == 0)
  {
    return "# no side/node sets for boundary assignment\n";
  }

  std::vector<int> inletSideIds;
  std::vector<int> wallSideIds;
  std::vector<int> outletSideIds;
  inletSideIds.reserve(n);
  wallSideIds.reserve(1);
  outletSideIds.reserve(n);

  for (size_t i = 0; i < n; ++i)
  {
    switch (roles[i])
    {
      case SideBoundaryRole::Inlet:
        inletSideIds.push_back(sideEntityIds[i]);
        break;
      case SideBoundaryRole::Wall:
        wallSideIds.push_back(sideEntityIds[i]);
        break;
      case SideBoundaryRole::Outlet:
        outletSideIds.push_back(sideEntityIds[i]);
        break;
    }
  }

  // Custom adapter data-row ids (summary line keeps real ENTITY_IDs).
  constexpr int kAdapterWallId = 3;
  constexpr int kAdapterInletId = 1;
  constexpr int kAdapterOutletIdStart = 21;

  std::ostringstream oss;
  oss << "id nodeset  sideset\n"
      << "nodeset: inlet " << FormatSpaceSeparatedInts(inletSideIds) << "  wall "
      << FormatSpaceSeparatedInts(wallSideIds) << "  outlet "
      << FormatSpaceSeparatedInts(outletSideIds) << "\n";

  if (labelCentersOut)
  {
    labelCentersOut->clear();
  }

  int nextOutletAdapterId = kAdapterOutletIdStart;
  for (size_t i = 0; i < n; ++i)
  {
    int displayId = sideEntityIds[i];
    if (customAdapter)
    {
      switch (roles[i])
      {
        case SideBoundaryRole::Wall:
          displayId = kAdapterWallId;
          break;
        case SideBoundaryRole::Inlet:
          displayId = kAdapterInletId;
          break;
        case SideBoundaryRole::Outlet:
          displayId = nextOutletAdapterId++;
          break;
      }
    }
    // Fresh map keyed by the ids written in data rows (avoids adapter 21+ colliding with
    // real ENTITY_IDs still needed as lookup sources).
    if (labelCentersOut)
    {
      auto it = centersByEntityId.find(sideEntityIds[i]);
      if (it != centersByEntityId.end())
      {
        LabelSite site = it->second;
        site.EntityId = displayId;
        (*labelCentersOut)[displayId] = site;
      }
    }
    oss << (i + 1) << ' ' << displayId << '\n';
  }
  return oss.str();
}

std::string FormatCommaSeparated(const std::vector<double>& values)
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6);
  for (size_t i = 0; i < values.size(); ++i)
  {
    if (i > 0)
    {
      oss << ',';
    }
    oss << values[i];
  }
  return oss.str();
}

std::string ReplaceNumOutletInOptions(const std::string& text, int numOutlet)
{
  std::istringstream iss(text);
  std::ostringstream oss;
  std::string line;
  while (std::getline(iss, line))
  {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    {
      ++i;
    }
    if (line.compare(i, 11, "-num_outlet") == 0)
    {
      const std::string indent = line.substr(0, i);
      oss << indent << "-num_outlet " << numOutlet << '\n';
      continue;
    }
    oss << line << '\n';
  }
  return oss.str();
}

std::string BuildInletOptionBlock(const std::vector<InletOptStats>& inlets, double normalScale,
  double boundsScale, double flowFactorScale, double areaScale)
{
  std::ostringstream oss;
  if (inlets.empty())
  {
    // Keep a commented placeholder so the options file stays complete and editable.
    oss << "\t#-inlet_nx 0.0\n"
           "\t#-inlet_ny 0.0\n"
           "\t#-inlet_nz 0.0\n"
           "\t#-inlet_xi 0.0\n"
           "\t#-inlet_yi 0.0\n"
           "\t#-inlet_zi 0.0\n"
           "\t#-inlet_xf 0.0\n"
           "\t#-inlet_yf 0.0\n"
           "\t#-inlet_zf 0.0\n"
           "\t#-inlet_flowfactor 0.0\n"
           "\t#-inlet_area 0.0\n"
           "\t#-inflow_time_interval 0.01\n";
    return oss.str();
  }

  double totalArea = 0.0;
  for (const InletOptStats& inlet : inlets)
  {
    totalArea += inlet.area;
  }

  std::vector<double> nx, ny, nz, xi, yi, zi, xf, yf, zf, flowfactor, area;
  nx.reserve(inlets.size());
  ny.reserve(inlets.size());
  nz.reserve(inlets.size());
  xi.reserve(inlets.size());
  yi.reserve(inlets.size());
  zi.reserve(inlets.size());
  xf.reserve(inlets.size());
  yf.reserve(inlets.size());
  zf.reserve(inlets.size());
  flowfactor.reserve(inlets.size());
  area.reserve(inlets.size());

  for (const InletOptStats& inlet : inlets)
  {
    nx.push_back(inlet.nx * normalScale);
    ny.push_back(inlet.ny * normalScale);
    nz.push_back(inlet.nz * normalScale);
    xi.push_back(inlet.xi * boundsScale);
    yi.push_back(inlet.yi * boundsScale);
    zi.push_back(inlet.zi * boundsScale);
    xf.push_back(inlet.xf * boundsScale);
    yf.push_back(inlet.yf * boundsScale);
    zf.push_back(inlet.zf * boundsScale);
    area.push_back(inlet.area * areaScale);
    flowfactor.push_back(
      (totalArea > 0.0 ? inlet.area / totalArea : 0.0) * flowFactorScale);
  }

  oss << "\t-inlet_nx " << FormatCommaSeparated(nx) << "\n"
      << "\t-inlet_ny " << FormatCommaSeparated(ny) << "\n"
      << "\t-inlet_nz " << FormatCommaSeparated(nz) << "\n"
      << "\t-inlet_xi " << FormatCommaSeparated(xi) << "\n"
      << "\t-inlet_yi " << FormatCommaSeparated(yi) << "\n"
      << "\t-inlet_zi " << FormatCommaSeparated(zi) << "\n"
      << "\t-inlet_xf " << FormatCommaSeparated(xf) << "\n"
      << "\t-inlet_yf " << FormatCommaSeparated(yf) << "\n"
      << "\t-inlet_zf " << FormatCommaSeparated(zf) << "\n"
      << "\t-inlet_flowfactor " << FormatCommaSeparated(flowfactor) << "\n"
      << "\t-inlet_area " << FormatCommaSeparated(area) << "\n"
      << "\t# -inflow_time_interval 0.01\n";
  return oss.str();
}

/**
 * Full solver options file: PV template for SINGLE_INLET, HV template for SINGLE_OUTLET.
 * Inlet_* lines (including -inlet_area) use pre-merge per-inlet stats; -num_outlet from classification.
 */
std::string FormatOptionsFileText(int flowBoundaryMode, const std::vector<InletOptStats>& inlets,
  double normalScale, double boundsScale, double flowFactorScale, double areaScale, int numOutlet)
{
  using namespace shyxBoundaryAssignmentOptionsTemplates;
  const bool singleInlet =
    flowBoundaryMode == vtkSHYXPartitionedCollectionBoundaryAssignment::SINGLE_INLET;
  const char* tmpl = singleInlet ? kSingleInlet : kSingleOutlet;
  const std::string inletBlock =
    BuildInletOptionBlock(inlets, normalScale, boundsScale, flowFactorScale, areaScale);

  std::istringstream iss(tmpl);
  std::ostringstream oss;
  std::string line;
  bool skippingInletValues = false;
  bool wroteInletBlock = false;
  while (std::getline(iss, line))
  {
    if (!skippingInletValues && line.find("inlet_*i are the lower bound") != std::string::npos)
    {
      oss << line << '\n';
      oss << inletBlock;
      wroteInletBlock = true;
      skippingInletValues = true;
      continue;
    }
    if (skippingInletValues)
    {
      if (line.find("#-----------") != std::string::npos)
      {
        skippingInletValues = false;
        oss << line << '\n';
      }
      // Drop template inlet_* / inflow_time_interval placeholders.
      continue;
    }
    oss << line << '\n';
  }
  if (!wroteInletBlock)
  {
    oss << inletBlock;
  }
  return ReplaceNumOutletInOptions(oss.str(), numOutlet);
}

void EnsurePointLabelArrays(vtkPolyData* pd, int entityId, const std::string& label)
{
  if (!pd || pd->GetNumberOfPoints() == 0)
  {
    return;
  }
  const vtkIdType n = pd->GetNumberOfPoints();
  vtkNew<vtkIntArray> ids;
  ids->SetName("SideSetEntityId");
  ids->SetNumberOfComponents(1);
  ids->SetNumberOfTuples(n);
  ids->FillComponent(0, entityId);
  pd->GetPointData()->AddArray(ids);

  vtkNew<vtkStringArray> labels;
  labels->SetName("Label");
  labels->SetNumberOfComponents(1);
  labels->SetNumberOfTuples(n);
  for (vtkIdType i = 0; i < n; ++i)
  {
    labels->SetValue(i, label);
  }
  pd->GetPointData()->AddArray(labels);
}

/** Parse "N entityId" rows from Boundary assignment text (skip header / nodeset summary). */
std::vector<int> ParseAssignmentEntityIds(const std::string& assignmentText)
{
  std::vector<int> ids;
  std::istringstream iss(assignmentText);
  std::string line;
  while (std::getline(iss, line))
  {
    if (line.empty() || line[0] == '#' || line.rfind("id ", 0) == 0 ||
      line.rfind("nodeset:", 0) == 0)
    {
      continue;
    }
    std::istringstream ls(line);
    int rowIndex = 0;
    int entityId = 0;
    if (ls >> rowIndex >> entityId)
    {
      ids.push_back(entityId);
    }
  }
  return ids;
}

bool ParseOptCommaDoubles(
  const std::string& optText, const char* key, std::vector<double>* values)
{
  if (!values || !key)
  {
    return false;
  }
  values->clear();
  const std::string prefix = std::string(key) + " ";
  std::istringstream iss(optText);
  std::string line;
  while (std::getline(iss, line))
  {
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
      ++start;
    }
    if (line.compare(start, prefix.size(), prefix) != 0)
    {
      continue;
    }
    std::string payload = line.substr(start + prefix.size());
    for (char& c : payload)
    {
      if (c == ',')
      {
        c = ' ';
      }
    }
    std::istringstream ls(payload);
    double v = 0.0;
    while (ls >> v)
    {
      values->push_back(v);
    }
    return !values->empty();
  }
  return false;
}

struct InletOptDraw
{
  double Nx = 0.0;
  double Ny = 0.0;
  double Nz = 0.0;
  double Xi = 0.0;
  double Yi = 0.0;
  double Zi = 0.0;
  double Xf = 0.0;
  double Yf = 0.0;
  double Zf = 0.0;
};

/**
 * Build port-1 debug geometry from the two panel texts (verification):
 * - Point Label verts for every sideset id in Boundary assignment text
 * - AABB + normal arrows for inlets from Inlet OPT text (bounds restored by 1/boundsScale)
 */
vtkSmartPointer<vtkPolyData> BuildDebugPolyDataFromTexts(const std::string& assignmentText,
  const std::string& optText, double boundsScale, const std::map<int, LabelSite>& centersByEntityId)
{
  const std::vector<int> allEntityIds = ParseAssignmentEntityIds(assignmentText);
  // Inlet OPT may list pre-merge inlets while assignment lists the post-merge id only.

  std::vector<double> nx, ny, nz, xi, yi, zi, xf, yf, zf;
  const bool haveOpt = ParseOptCommaDoubles(optText, "-inlet_nx", &nx) &&
    ParseOptCommaDoubles(optText, "-inlet_ny", &ny) &&
    ParseOptCommaDoubles(optText, "-inlet_nz", &nz) &&
    ParseOptCommaDoubles(optText, "-inlet_xi", &xi) &&
    ParseOptCommaDoubles(optText, "-inlet_yi", &yi) &&
    ParseOptCommaDoubles(optText, "-inlet_zi", &zi) &&
    ParseOptCommaDoubles(optText, "-inlet_xf", &xf) &&
    ParseOptCommaDoubles(optText, "-inlet_yf", &yf) &&
    ParseOptCommaDoubles(optText, "-inlet_zf", &zf);

  std::vector<InletOptDraw> inlets;
  if (haveOpt)
  {
    const size_t n =
      std::min({ nx.size(), ny.size(), nz.size(), xi.size(), yi.size(), zi.size(), xf.size(),
        yf.size(), zf.size() });
    const double invBounds = (std::abs(boundsScale) > 1e-18) ? (1.0 / boundsScale) : 1.0;
    inlets.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
      InletOptDraw d;
      d.Nx = nx[i];
      d.Ny = ny[i];
      d.Nz = nz[i];
      d.Xi = xi[i] * invBounds;
      d.Yi = yi[i] * invBounds;
      d.Zi = zi[i] * invBounds;
      d.Xf = xf[i] * invBounds;
      d.Yf = yf[i] * invBounds;
      d.Zf = zf[i] * invBounds;
      inlets.push_back(d);
    }
  }

  double charLen = 0.0;
  for (const InletOptDraw& inlet : inlets)
  {
    const double dx = inlet.Xf - inlet.Xi;
    const double dy = inlet.Yf - inlet.Yi;
    const double dz = inlet.Zf - inlet.Zi;
    charLen = std::max(charLen, std::sqrt(dx * dx + dy * dy + dz * dz));
  }
  if (charLen <= 0.0)
  {
    charLen = 1.0;
  }
  const double arrowLength = std::max(0.15 * charLen, 1e-3);

  static const int edges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 },
    { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

  vtkNew<vtkArrowSource> arrow;
  arrow->SetTipResolution(16);
  arrow->SetShaftResolution(16);

  vtkNew<vtkAppendPolyData> append;
  std::vector<int> vertEntityIds;
  vertEntityIds.reserve(allEntityIds.size());

  // Point Label: every sideset from Boundary assignment text
  for (const int entityId : allEntityIds)
  {
    auto it = centersByEntityId.find(entityId);
    if (it == centersByEntityId.end())
    {
      continue;
    }
    const LabelSite& site = it->second;
    vtkNew<vtkPoints> cPts;
    vtkNew<vtkCellArray> cVerts;
    const vtkIdType pid = cPts->InsertNextPoint(site.Cx, site.Cy, site.Cz);
    cVerts->InsertNextCell(1);
    cVerts->InsertCellPoint(pid);
    vtkNew<vtkPolyData> center;
    center->SetPoints(cPts);
    center->SetVerts(cVerts);
    EnsurePointLabelArrays(center, entityId, std::to_string(entityId));
    append->AddInputData(center);
    vertEntityIds.push_back(entityId);
  }

  // Inlet-only AABB + normals from Inlet OPT text
  for (const InletOptDraw& inlet : inlets)
  {
    const double cx = 0.5 * (inlet.Xi + inlet.Xf);
    const double cy = 0.5 * (inlet.Yi + inlet.Yf);
    const double cz = 0.5 * (inlet.Zi + inlet.Zf);

    {
      vtkNew<vtkPoints> boxPts;
      vtkNew<vtkCellArray> boxLines;
      const double corners[8][3] = { { inlet.Xi, inlet.Yi, inlet.Zi }, { inlet.Xf, inlet.Yi, inlet.Zi },
        { inlet.Xi, inlet.Yf, inlet.Zi }, { inlet.Xf, inlet.Yf, inlet.Zi },
        { inlet.Xi, inlet.Yi, inlet.Zf }, { inlet.Xf, inlet.Yi, inlet.Zf },
        { inlet.Xi, inlet.Yf, inlet.Zf }, { inlet.Xf, inlet.Yf, inlet.Zf } };
      for (int c = 0; c < 8; ++c)
      {
        boxPts->InsertNextPoint(corners[c]);
      }
      for (const auto& e : edges)
      {
        boxLines->InsertNextCell(2);
        boxLines->InsertCellPoint(e[0]);
        boxLines->InsertCellPoint(e[1]);
      }
      vtkNew<vtkPolyData> boxes;
      boxes->SetPoints(boxPts);
      boxes->SetLines(boxLines);
      append->AddInputData(boxes);
    }

    {
      vtkNew<vtkPoints> cPts;
      cPts->InsertNextPoint(cx, cy, cz);
      vtkNew<vtkDoubleArray> normals;
      normals->SetName("Normal");
      normals->SetNumberOfComponents(3);
      normals->InsertNextTuple3(inlet.Nx, inlet.Ny, inlet.Nz);
      vtkNew<vtkPolyData> seed;
      seed->SetPoints(cPts);
      seed->GetPointData()->SetVectors(normals);

      vtkNew<vtkGlyph3D> glyph;
      glyph->SetInputData(seed);
      glyph->SetSourceConnection(arrow->GetOutputPort());
      glyph->SetVectorModeToUseVector();
      glyph->SetScaleModeToDataScalingOff();
      glyph->SetScaleFactor(arrowLength);
      glyph->OrientOn();
      glyph->Update();

      vtkNew<vtkPolyData> arrows;
      arrows->DeepCopy(glyph->GetOutput());
      arrows->SetVerts(nullptr);
      append->AddInputData(arrows);
    }
  }

  if (append->GetNumberOfInputConnections(0) == 0)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }

  append->Update();
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  out->DeepCopy(append->GetOutput());

  // Guarantee Label / SideSetEntityId on verts for Point Label + Information panel.
  {
    const vtkIdType nPts = out->GetNumberOfPoints();
    vtkNew<vtkIntArray> ids;
    ids->SetName("SideSetEntityId");
    ids->SetNumberOfComponents(1);
    ids->SetNumberOfTuples(nPts);
    ids->FillComponent(0, 0);
    vtkNew<vtkStringArray> labels;
    labels->SetName("Label");
    labels->SetNumberOfComponents(1);
    labels->SetNumberOfTuples(nPts);
    for (vtkIdType i = 0; i < nPts; ++i)
    {
      labels->SetValue(i, "");
    }

    if (vtkCellArray* va = out->GetVerts())
    {
      vtkIdType npts = 0;
      const vtkIdType* pts = nullptr;
      size_t vertIndex = 0;
      va->InitTraversal();
      while (va->GetNextCell(npts, pts) && vertIndex < vertEntityIds.size())
      {
        if (npts >= 1 && pts)
        {
          const int entityId = vertEntityIds[vertIndex];
          ids->SetValue(pts[0], entityId);
          labels->SetValue(pts[0], std::to_string(entityId));
        }
        ++vertIndex;
      }
    }

    out->GetPointData()->RemoveArray("SideSetEntityId");
    out->GetPointData()->RemoveArray("Label");
    out->GetPointData()->AddArray(ids);
    out->GetPointData()->AddArray(labels);
  }

  return out;
}

void StampTextFieldData(vtkDataObject* obj, int flowMode, const std::string& assignmentText,
  const std::string& optText)
{
  if (!obj)
  {
    return;
  }
  vtkFieldData* fd = obj->GetFieldData();

  vtkNew<vtkStringArray> modeStamp;
  modeStamp->SetName("SHYXFlowBoundaryMode");
  modeStamp->SetNumberOfValues(1);
  modeStamp->SetValue(0,
    flowMode == vtkSHYXPartitionedCollectionBoundaryAssignment::SINGLE_INLET ? "single_inlet"
                                                                             : "single_outlet");
  fd->RemoveArray("SHYXFlowBoundaryMode");
  fd->AddArray(modeStamp);

  vtkNew<vtkStringArray> assignStamp;
  assignStamp->SetName("SHYXBoundaryAssignmentText");
  assignStamp->SetNumberOfValues(1);
  assignStamp->SetValue(0, assignmentText.c_str());
  fd->RemoveArray("SHYXBoundaryAssignmentText");
  fd->AddArray(assignStamp);

  vtkNew<vtkStringArray> optStamp;
  optStamp->SetName("SHYXInletOptText");
  optStamp->SetNumberOfValues(1);
  optStamp->SetValue(0, optText.c_str());
  fd->RemoveArray("SHYXInletOptText");
  fd->AddArray(optStamp);
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXPartitionedCollectionBoundaryAssignment::vtkSHYXPartitionedCollectionBoundaryAssignment()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(2);
  this->CopyInfoString(this->BoundaryAssignmentText, "");
  this->CopyInfoString(this->InletOptText, "");
}

//------------------------------------------------------------------------------
vtkSHYXPartitionedCollectionBoundaryAssignment::~vtkSHYXPartitionedCollectionBoundaryAssignment()
{
  this->CopyInfoString(this->BoundaryAssignmentText, nullptr);
  this->CopyInfoString(this->InletOptText, nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXPartitionedCollectionBoundaryAssignment::CopyInfoString(char*& dest, const char* text)
{
  delete[] dest;
  dest = nullptr;
  if (!text)
  {
    return;
  }
  const size_t n = std::strlen(text) + 1;
  dest = new char[n];
  std::memcpy(dest, text, n);
}

//------------------------------------------------------------------------------
const char* vtkSHYXPartitionedCollectionBoundaryAssignment::GetBoundaryAssignmentText()
{
  // After Apply, ParaView calls UpdatePropertyInformation → Get*. Read from the current
  // output FieldData so the panel cannot stay one Apply behind a stale ivar.
  if (vtkDataObject* out = this->GetOutputDataObject(0))
  {
    if (vtkStringArray* arr = vtkStringArray::SafeDownCast(
          out->GetFieldData()->GetAbstractArray("SHYXBoundaryAssignmentText")))
    {
      if (arr->GetNumberOfValues() >= 1)
      {
        this->CopyInfoString(this->BoundaryAssignmentText, arr->GetValue(0).c_str());
      }
    }
  }
  return this->BoundaryAssignmentText ? this->BoundaryAssignmentText : "";
}

//------------------------------------------------------------------------------
const char* vtkSHYXPartitionedCollectionBoundaryAssignment::GetInletOptText()
{
  if (vtkDataObject* out = this->GetOutputDataObject(0))
  {
    if (vtkStringArray* arr =
          vtkStringArray::SafeDownCast(out->GetFieldData()->GetAbstractArray("SHYXInletOptText")))
    {
      if (arr->GetNumberOfValues() >= 1)
      {
        this->CopyInfoString(this->InletOptText, arr->GetValue(0).c_str());
      }
    }
  }
  return this->InletOptText ? this->InletOptText : "";
}

//------------------------------------------------------------------------------
void vtkSHYXPartitionedCollectionBoundaryAssignment::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FlowBoundaryMode: " << this->FlowBoundaryMode << "\n";
  os << indent << "MergeInletsIntoOneSideSet: " << this->MergeInletsIntoOneSideSet << "\n";
  os << indent << "CustomAdapter: " << this->CustomAdapter << "\n";
  os << indent << "BoundaryAssignmentText: "
     << (this->BoundaryAssignmentText ? this->BoundaryAssignmentText : "(null)") << "\n";
  os << indent << "InletOptText: " << (this->InletOptText ? this->InletOptText : "(null)") << "\n";
  os << indent << "InletOptNormalScale: " << this->InletOptNormalScale << "\n";
  os << indent << "InletOptBoundsScale: " << this->InletOptBoundsScale << "\n";
  os << indent << "InletOptFlowFactorScale: " << this->InletOptFlowFactorScale << "\n";
  os << indent << "InletOptAreaScale: " << this->InletOptAreaScale << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionBoundaryAssignment::FillInputPortInformation(
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
int vtkSHYXPartitionedCollectionBoundaryAssignment::FillOutputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  if (port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionBoundaryAssignment::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector,
  vtkInformationVector* outputVector)
{
  vtkPartitionedDataSetCollection* input =
    vtkPartitionedDataSetCollection::GetData(inputVector[0], 0);
  vtkPartitionedDataSetCollection* output =
    vtkPartitionedDataSetCollection::GetData(outputVector, 0);
  vtkPolyData* debugOutput = vtkPolyData::GetData(outputVector, 1);

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
  if (!debugOutput)
  {
    vtkErrorMacro(<< "Output debug vtkPolyData is null.");
    return 0;
  }

  // Port 0: start from input; may rewrite assembly when merging inlets.
  output->DeepCopy(input);
  debugOutput->Initialize();

  auto finishEmpty = [&](const char* assignMsg) {
    const std::string optMsg = FormatOptionsFileText(this->GetFlowBoundaryMode(), {},
      this->InletOptNormalScale, this->InletOptBoundsScale, this->InletOptFlowFactorScale,
      this->InletOptAreaScale, 0);
    this->CopyInfoString(this->BoundaryAssignmentText, assignMsg);
    this->CopyInfoString(this->InletOptText, optMsg.c_str());
    StampTextFieldData(output, this->GetFlowBoundaryMode(), assignMsg, optMsg);
    return 1;
  };

  vtkDataAssembly* assembly = input->GetDataAssembly();
  PartitionedCollectionLayout layout;
  if (!ParsePartitionedCollectionLayout(assembly, &layout))
  {
    vtkWarningMacro(<< "Input vtkDataAssembly is missing or does not describe element/side/node sets. "
                    << "Passing through unchanged.");
    return finishEmpty("# no side/node sets (missing assembly layout)\n");
  }

  const unsigned int nSideSets = static_cast<unsigned int>(layout.SideSetPdcIndices.size());
  if (nSideSets == 0)
  {
    vtkWarningMacro(<< "No side sets found in vtkDataAssembly. Passing through unchanged.");
    return finishEmpty("# no side/node sets (no side sets)\n");
  }

  if (layout.NodeSetPdcIndices.size() != layout.SideSetPdcIndices.size())
  {
    vtkWarningMacro(<< "Node set count (" << layout.NodeSetPdcIndices.size()
                    << ") differs from side set count (" << layout.SideSetPdcIndices.size()
                    << "). Pairing by minimum count.");
  }
  const unsigned int nPairs =
    static_cast<unsigned int>(std::min(layout.NodeSetPdcIndices.size(), layout.SideSetPdcIndices.size()));

  // Area-descending order for classification (layout indices only until optional merge).
  SortSideNodePairsByAreaDescending(output, &layout, nPairs);
  layout.SideSetPdcIndices.resize(nPairs);
  layout.NodeSetPdcIndices.resize(nPairs);

  const int flowMode = this->GetFlowBoundaryMode();

  if (nPairs < 2)
  {
    vtkWarningMacro(<< "Need at least 2 side sets for wall + opening classification "
                    << "(got " << nPairs << "). Boundary assignment / inlet OPT may be incomplete.");
  }

  std::vector<int> assignmentSideEntityIds;
  std::vector<SideBoundaryRole> assignmentRoles;
  std::vector<InletOptStats> inletOptStats;
  std::map<int, LabelSite> centersByEntityId;
  assignmentSideEntityIds.reserve(nPairs);
  assignmentRoles.reserve(nPairs);

  for (unsigned int i = 0; i < nPairs; ++i)
  {
    const SideBoundaryRole role = ClassifySideBoundaryRole(i, nPairs, flowMode);
    const int entityId =
      ReadEntityIdFromMeta(output, layout.SideSetPdcIndices[i], static_cast<int>(i + 1));
    assignmentRoles.push_back(role);
    assignmentSideEntityIds.push_back(entityId);

    vtkSmartPointer<vtkPolyData> sidePd =
      ForceDataSetToPolyData(GetDataSetFromPdcBlock(output, layout.SideSetPdcIndices[i]));
    if (sidePd)
    {
      double bounds[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
      sidePd->GetBounds(bounds);
      LabelSite site;
      site.EntityId = entityId;
      site.Cx = 0.5 * (bounds[0] + bounds[1]);
      site.Cy = 0.5 * (bounds[2] + bounds[3]);
      site.Cz = 0.5 * (bounds[4] + bounds[5]);
      centersByEntityId[entityId] = site;
    }

    if (role != SideBoundaryRole::Inlet)
    {
      continue;
    }
    if (!sidePd)
    {
      vtkWarningMacro(<< "Inlet side set at PDC index " << layout.SideSetPdcIndices[i]
                      << " has no geometry; skipping OPT stats.");
      continue;
    }

    // Inlet OPT (file 2): always per-inlet stats before any merge.
    InletOptStats stats;
    stats.EntityId = entityId;
    double normal[3] = { 0.0, 0.0, 0.0 };
    ComputeAverageCellNormal(sidePd, normal);
    stats.nx = normal[0];
    stats.ny = normal[1];
    stats.nz = normal[2];
    double bounds[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    sidePd->GetBounds(bounds);
    stats.xi = bounds[0];
    stats.xf = bounds[1];
    stats.yi = bounds[2];
    stats.yf = bounds[3];
    stats.zi = bounds[4];
    stats.zf = bounds[5];
    stats.area = ComputeSurfaceArea(sidePd);
    inletOptStats.push_back(stats);
  }

  int numOutlet = 0;
  for (const SideBoundaryRole role : assignmentRoles)
  {
    if (role == SideBoundaryRole::Outlet)
    {
      ++numOutlet;
    }
  }

  const std::string optText = FormatOptionsFileText(flowMode, inletOptStats,
    this->InletOptNormalScale, this->InletOptBoundsScale, this->InletOptFlowFactorScale,
    this->InletOptAreaScale, numOutlet);

  // Boundary assignment (file 1): after optional merge of inlets in SINGLE_OUTLET mode.
  const bool wantMerge = this->GetMergeInletsIntoOneSideSet() != 0 &&
    flowMode == vtkSHYXPartitionedCollectionBoundaryAssignment::SINGLE_OUTLET;
  if (wantMerge)
  {
    std::vector<int> mergedEntityIds;
    std::vector<SideBoundaryRole> mergedRoles;
    if (MergeInletPairsIntoOneSideSet(
          output, layout, assignmentRoles, &mergedEntityIds, &mergedRoles, &centersByEntityId))
    {
      assignmentSideEntityIds = std::move(mergedEntityIds);
      assignmentRoles = std::move(mergedRoles);
    }
  }

  std::map<int, LabelSite> labelCenters;
  const std::string assignmentText = FormatBoundaryAssignmentText(assignmentSideEntityIds,
    assignmentRoles, this->GetCustomAdapter() != 0, centersByEntityId, &labelCenters);

  this->CopyInfoString(this->BoundaryAssignmentText, assignmentText.c_str());
  this->CopyInfoString(this->InletOptText, optText.c_str());
  StampTextFieldData(output, flowMode, assignmentText, optText);

  // Port 1: parse the two texts so the draw verifies what the panel shows.
  // labelCenters is keyed by the ids in assignment data rows (adapter or real ENTITY_ID).
  vtkSmartPointer<vtkPolyData> debugPd = BuildDebugPolyDataFromTexts(
    assignmentText, optText, this->InletOptBoundsScale, labelCenters);
  debugOutput->ShallowCopy(debugPd);
  StampTextFieldData(debugOutput, flowMode, assignmentText, optText);

  return 1;
}

VTK_ABI_NAMESPACE_END
