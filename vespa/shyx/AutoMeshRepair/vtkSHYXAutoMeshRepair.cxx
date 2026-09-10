#include "vtkSHYXAutoMeshRepair.h"

#include "vtkCGALAlphaWrapping.h"
#include "vtkCGALHelper.h"
#include "vtkSHYXHoleFillFilter.h"
#include "vtkSHYXMeshChecker.h"
#include "vtkSHYXSelectionFillAlphaReunionFilter.h"

#include <vtkAlgorithm.h>
#include <vtkAppendPolyData.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkSmartPointer.h>
#include <vtkTriangleFilter.h>

#include <CGAL/Polygon_mesh_processing/intersection.h>
#include <CGAL/boost/graph/helpers.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pmp = CGAL::Polygon_mesh_processing;

vtkStandardNewMacro(vtkSHYXAutoMeshRepair);

namespace
{

void SetFieldInt(vtkPolyData* pd, const char* name, int value)
{
  if (!pd || !name)
  {
    return;
  }
  vtkNew<vtkIntArray> arr;
  arr->SetName(name);
  arr->SetNumberOfComponents(1);
  arr->SetNumberOfTuples(1);
  arr->SetValue(0, value);
  pd->GetFieldData()->AddArray(arr);
}

void DilateCellMask(vtkPolyData* mesh, std::vector<char>& mask, int layers)
{
  if (!mesh || layers <= 0 || mask.empty())
  {
    return;
  }

  const vtkIdType nCells = mesh->GetNumberOfCells();
  if (static_cast<vtkIdType>(mask.size()) != nCells)
  {
    return;
  }

  mesh->BuildLinks();
  vtkNew<vtkIdList> cellPts;
  vtkNew<vtkIdList> neighborCells;
  std::vector<char> next = mask;

  for (int layer = 0; layer < layers; ++layer)
  {
    next = mask;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!mask[static_cast<size_t>(cid)])
      {
        continue;
      }
      mesh->GetCellPoints(cid, cellPts);
      for (vtkIdType k = 0; k < cellPts->GetNumberOfIds(); ++k)
      {
        mesh->GetPointCells(cellPts->GetId(k), neighborCells);
        for (vtkIdType j = 0; j < neighborCells->GetNumberOfIds(); ++j)
        {
          const vtkIdType nid = neighborCells->GetId(j);
          if (nid >= 0 && nid < nCells)
          {
            next[static_cast<size_t>(nid)] = 1;
          }
        }
      }
    }
    mask.swap(next);
  }
}

vtkSmartPointer<vtkSelection> SelectionFromCells(const std::vector<vtkIdType>& cells)
{
  vtkNew<vtkIdTypeArray> list;
  list->SetNumberOfTuples(static_cast<vtkIdType>(cells.size()));
  for (std::size_t i = 0; i < cells.size(); ++i)
  {
    list->SetValue(static_cast<vtkIdType>(i), cells[i]);
  }

  vtkNew<vtkSelectionNode> node;
  node->SetFieldType(vtkSelectionNode::CELL);
  node->SetContentType(vtkSelectionNode::INDICES);
  node->SetSelectionList(list);

  vtkNew<vtkSelection> sel;
  sel->AddNode(node);
  return sel;
}

struct UnionFind
{
  std::vector<vtkIdType> parent;
  explicit UnionFind(vtkIdType n)
    : parent(static_cast<size_t>(n))
  {
    for (vtkIdType i = 0; i < n; ++i)
    {
      parent[static_cast<size_t>(i)] = i;
    }
  }
  vtkIdType Find(vtkIdType x)
  {
    vtkIdType r = x;
    while (parent[static_cast<size_t>(r)] != r)
    {
      r = parent[static_cast<size_t>(r)];
    }
    vtkIdType cur = x;
    while (parent[static_cast<size_t>(cur)] != r)
    {
      const vtkIdType nxt = parent[static_cast<size_t>(cur)];
      parent[static_cast<size_t>(cur)] = r;
      cur = nxt;
    }
    return r;
  }
  void Unite(vtkIdType a, vtkIdType b)
  {
    const vtkIdType ra = Find(a);
    const vtkIdType rb = Find(b);
    if (ra != rb)
    {
      parent[static_cast<size_t>(ra)] = rb;
    }
  }
};

/**
 * Detect CGAL self-intersections and group VTK cells into connected locations.
 * Faces in an intersecting pair are always the same cluster even if they do not share a vertex.
 * Seed faces that share a vertex are also merged (nearby intersections at one pinch).
 */
bool DetectIntersectionClusters(
  vtkPolyData* mesh, std::vector<std::vector<vtkIdType>>& clusters, std::string& error)
{
  clusters.clear();
  error.clear();
  if (!mesh || mesh->GetNumberOfCells() == 0)
  {
    error = "empty mesh.";
    return false;
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> surf = std::make_unique<vtkCGALHelper::Vespa_surface>();
  std::vector<Graph_Faces> vtkCellToCgalFace;
  if (!vtkCGALHelper::toCGAL(mesh, surf.get(), &vtkCellToCgalFace))
  {
    error = "VTK to CGAL conversion failed.";
    return false;
  }
  if (!CGAL::is_triangle_mesh(surf->surface))
  {
    error = "not a pure triangle mesh (PMP::self_intersections requires triangles).";
    return false;
  }

  std::vector<std::pair<Graph_Faces, Graph_Faces>> pairs;
  try
  {
    (void)pmp::self_intersections(surf->surface, std::back_inserter(pairs));
  }
  catch (const std::exception& e)
  {
    error = std::string("CGAL self_intersections failed: ") + e.what();
    return false;
  }

  if (pairs.empty())
  {
    return true;
  }

  const vtkIdType nCells = mesh->GetNumberOfCells();
  const vtkIdType nMap = static_cast<vtkIdType>(vtkCellToCgalFace.size());
  if (nMap != nCells)
  {
    error = "cell-id mismatch after VTK to CGAL conversion (vtkPolyDataNormals).";
    return false;
  }

  std::unordered_map<std::size_t, vtkIdType> faceIdxToCell;
  faceIdxToCell.reserve(vtkCellToCgalFace.size());
  for (vtkIdType cid = 0; cid < nMap; ++cid)
  {
    const Graph_Faces f = vtkCellToCgalFace[static_cast<size_t>(cid)];
    if (!surf->surface.is_valid(f))
    {
      continue;
    }
    faceIdxToCell[static_cast<std::size_t>(f.idx())] = cid;
  }

  auto cellOf = [&](Graph_Faces f) -> vtkIdType {
    const auto it = faceIdxToCell.find(static_cast<std::size_t>(f.idx()));
    if (it == faceIdxToCell.end())
    {
      return -1;
    }
    return (it->second >= 0 && it->second < nCells) ? it->second : -1;
  };

  std::vector<char> seed(static_cast<size_t>(nCells), 0);
  UnionFind uf(nCells);
  for (const auto& pr : pairs)
  {
    const vtkIdType a = cellOf(pr.first);
    const vtkIdType b = cellOf(pr.second);
    if (a >= 0)
    {
      seed[static_cast<size_t>(a)] = 1;
    }
    if (b >= 0)
    {
      seed[static_cast<size_t>(b)] = 1;
    }
    if (a >= 0 && b >= 0)
    {
      uf.Unite(a, b);
    }
  }

  mesh->BuildLinks();
  vtkNew<vtkIdList> cellPts;
  vtkNew<vtkIdList> neighborCells;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (!seed[static_cast<size_t>(cid)])
    {
      continue;
    }
    mesh->GetCellPoints(cid, cellPts);
    for (vtkIdType k = 0; k < cellPts->GetNumberOfIds(); ++k)
    {
      mesh->GetPointCells(cellPts->GetId(k), neighborCells);
      for (vtkIdType j = 0; j < neighborCells->GetNumberOfIds(); ++j)
      {
        const vtkIdType nid = neighborCells->GetId(j);
        if (nid >= 0 && nid < nCells && seed[static_cast<size_t>(nid)])
        {
          uf.Unite(cid, nid);
        }
      }
    }
  }

  std::unordered_map<vtkIdType, std::size_t> rootToCluster;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (!seed[static_cast<size_t>(cid)])
    {
      continue;
    }
    const vtkIdType root = uf.Find(cid);
    auto it = rootToCluster.find(root);
    if (it == rootToCluster.end())
    {
      it = rootToCluster.emplace(root, clusters.size()).first;
      clusters.emplace_back();
    }
    clusters[it->second].push_back(cid);
  }

  std::sort(clusters.begin(), clusters.end(),
    [](const std::vector<vtkIdType>& a, const std::vector<vtkIdType>& b) { return a.size() > b.size(); });
  return true;
}

vtkSmartPointer<vtkPolyData> MakeConsistentSurface(vtkPolyData* input)
{
  if (!input)
  {
    return nullptr;
  }
  vtkNew<vtkPolyDataNormals> consistency;
  consistency->SetInputData(input);
  consistency->SplittingOff();
  consistency->NonManifoldTraversalOff();
  consistency->Update();
  vtkPolyData* out = consistency->GetOutput();
  if (!out)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkPolyData> pd = vtkSmartPointer<vtkPolyData>::New();
  pd->ShallowCopy(out);
  return pd;
}

vtkSmartPointer<vtkPolyData> ExtractMaskedCells(
  vtkPolyData* mesh, const std::vector<char>& mask, bool keepMarked)
{
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  if (!mesh)
  {
    return out;
  }

  const vtkIdType nPts = mesh->GetNumberOfPoints();
  const vtkIdType nCells = mesh->GetNumberOfCells();
  std::vector<vtkIdType> old2new(static_cast<size_t>(nPts), -1);
  vtkNew<vtkPoints> newPts;
  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkIdList> cellPts;
  vtkNew<vtkIdList> remapped;
  mesh->BuildCells();

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    const bool marked =
      (cid < static_cast<vtkIdType>(mask.size()) && mask[static_cast<size_t>(cid)] != 0);
    if (marked != keepMarked)
    {
      continue;
    }
    mesh->GetCellPoints(cid, cellPts);
    const vtkIdType npts = cellPts->GetNumberOfIds();
    remapped->SetNumberOfIds(npts);
    bool ok = true;
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const vtkIdType pid = cellPts->GetId(k);
      if (pid < 0 || pid >= nPts)
      {
        ok = false;
        break;
      }
      if (old2new[static_cast<size_t>(pid)] < 0)
      {
        double x[3];
        mesh->GetPoint(pid, x);
        old2new[static_cast<size_t>(pid)] = newPts->InsertNextPoint(x);
      }
      remapped->SetId(k, old2new[static_cast<size_t>(pid)]);
    }
    if (ok)
    {
      newPolys->InsertNextCell(remapped);
    }
  }

  out->SetPoints(newPts);
  out->SetPolys(newPolys);
  out->Squeeze();
  return out;
}

vtkSmartPointer<vtkPolyData> FillThenWrap(vtkPolyData* patch, int fairingContinuity, bool skipWrap,
  bool absoluteThresholds, double alpha, double offset)
{
  if (!patch || patch->GetNumberOfCells() == 0)
  {
    return nullptr;
  }

  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(patch);
  tri->PassLinesOff();
  tri->PassVertsOff();
  tri->Update();
  vtkPolyData* triOut = tri->GetOutput();
  if (!triOut || triOut->GetNumberOfCells() == 0)
  {
    return nullptr;
  }

  vtkNew<vtkSHYXHoleFillFilter> fill;
  fill->SetFairingContinuity(fairingContinuity);
  fill->SetInputData(triOut);
  fill->SetUpdateAttributes(false);
  fill->Update();
  vtkPolyData* filled = fill->GetOutput();
  if (!filled || filled->GetNumberOfCells() == 0)
  {
    return nullptr;
  }

  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  if (skipWrap)
  {
    out->ShallowCopy(filled);
    return out;
  }

  vtkNew<vtkCGALAlphaWrapping> aw;
  aw->SetAbsoluteThresholds(absoluteThresholds);
  aw->SetAlpha(alpha);
  aw->SetOffset(offset);
  aw->SetInputData(filled);
  aw->SetUpdateAttributes(false);
  aw->Update();
  vtkPolyData* wrapped = aw->GetOutput();
  if (!wrapped || wrapped->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  out->ShallowCopy(wrapped);
  return out;
}

/**
 * Dilate each seed cluster, then merge clusters whose dilated masks overlap.
 * Output lists are dilated cell ids, largest first.
 */
void MergeDilatedClusters(vtkPolyData* mesh, const std::vector<std::vector<vtkIdType>>& seeds,
  int layers, std::vector<std::vector<vtkIdType>>& dilated)
{
  dilated.clear();
  if (!mesh || seeds.empty())
  {
    return;
  }

  const vtkIdType nCells = mesh->GetNumberOfCells();
  const int n = static_cast<int>(seeds.size());
  UnionFind uf(n);
  std::vector<std::vector<char>> masks(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    masks[static_cast<size_t>(i)].assign(static_cast<size_t>(nCells), 0);
    for (vtkIdType cid : seeds[static_cast<size_t>(i)])
    {
      if (cid >= 0 && cid < nCells)
      {
        masks[static_cast<size_t>(i)][static_cast<size_t>(cid)] = 1;
      }
    }
    DilateCellMask(mesh, masks[static_cast<size_t>(i)], layers);
  }

  std::vector<int> owner(static_cast<size_t>(nCells), -1);
  for (int i = 0; i < n; ++i)
  {
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!masks[static_cast<size_t>(i)][static_cast<size_t>(cid)])
      {
        continue;
      }
      if (owner[static_cast<size_t>(cid)] >= 0)
      {
        uf.Unite(i, owner[static_cast<size_t>(cid)]);
      }
      else
      {
        owner[static_cast<size_t>(cid)] = i;
      }
    }
  }

  std::unordered_map<vtkIdType, std::vector<char>> rootMask;
  for (int i = 0; i < n; ++i)
  {
    const vtkIdType root = uf.Find(i);
    auto it = rootMask.find(root);
    if (it == rootMask.end())
    {
      it = rootMask.emplace(root, masks[static_cast<size_t>(i)]).first;
    }
    else
    {
      for (vtkIdType cid = 0; cid < nCells; ++cid)
      {
        if (masks[static_cast<size_t>(i)][static_cast<size_t>(cid)])
        {
          it->second[static_cast<size_t>(cid)] = 1;
        }
      }
    }
  }

  dilated.reserve(rootMask.size());
  for (auto& kv : rootMask)
  {
    std::vector<vtkIdType> cells;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (kv.second[static_cast<size_t>(cid)])
      {
        cells.push_back(cid);
      }
    }
    if (!cells.empty())
    {
      dilated.push_back(std::move(cells));
    }
  }

  std::sort(dilated.begin(), dilated.end(),
    [](const std::vector<vtkIdType>& a, const std::vector<vtkIdType>& b) {
      return a.size() > b.size();
    });
}

void ConfigureReunion(vtkSHYXSelectionFillAlphaReunionFilter* reunion, vtkSHYXAutoMeshRepair* self)
{
  reunion->SetFairingContinuity(self->GetFairingContinuity());
  reunion->SetAbsoluteThresholds(self->GetAbsoluteThresholds());
  reunion->SetAlpha(self->GetAlpha());
  reunion->SetOffset(self->GetOffset());
  reunion->SetSkipAlphaWrapping(self->GetSkipAlphaWrapping());
  reunion->SetThrowOnSelfIntersection(self->GetThrowOnSelfIntersection());
  reunion->SetOrientToBoundVolumeWhenNeeded(self->GetOrientToBoundVolumeWhenNeeded());
  reunion->SetEnableBridgeCleanup(self->GetEnableBridgeCleanup());
  reunion->SetEnableBridgeRemesh(self->GetEnableBridgeRemesh());
  reunion->SetEnableBridgeSmooth(self->GetEnableBridgeSmooth());
  reunion->SetBridgeDilateLayers(self->GetBridgeDilateLayers());
  reunion->SetBridgeDilateFromSeam(self->GetBridgeDilateFromSeam());
  reunion->SetBridgeTargetEdgeLength(self->GetBridgeTargetEdgeLength());
  reunion->SetBridgeRemeshIterations(self->GetBridgeRemeshIterations());
  reunion->SetBridgeRemeshRelaxationSteps(self->GetBridgeRemeshRelaxationSteps());
  reunion->SetBridgeSmoothMethod(self->GetBridgeSmoothMethod());
  reunion->SetBridgeSmoothIterations(self->GetBridgeSmoothIterations());
  reunion->SetBridgeSmoothTimeStep(self->GetBridgeSmoothTimeStep());
  reunion->SetBridgeFairContinuity(self->GetBridgeFairContinuity());
  reunion->SetUpdateAttributes(false);
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXAutoMeshRepair::vtkSHYXAutoMeshRepair()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(3);
}

//------------------------------------------------------------------------------
int vtkSHYXAutoMeshRepair::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  if (port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAutoMeshRepair::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1 || port == 2)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXAutoMeshRepair::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CheckSoupEdges: " << (this->CheckSoupEdges ? "on" : "off") << "\n";
  os << indent << "CheckBoundary: " << (this->CheckBoundary ? "on" : "off") << "\n";
  os << indent << "CheckSelfIntersection: " << (this->CheckSelfIntersection ? "on" : "off") << "\n";
  os << indent << "CheckOrient: " << (this->CheckOrient ? "on" : "off") << "\n";
  os << indent << "AttemptOrientRepair: " << (this->AttemptOrientRepair ? "on" : "off") << "\n";
  os << indent << "RepairSelfIntersections: " << (this->RepairSelfIntersections ? "on" : "off") << "\n";
  os << indent << "RepairStage: " << this->RepairStage << "\n";
  os << indent << "DilateLayers: " << this->DilateLayers << "\n";
  os << indent << "MaxPasses: " << this->MaxPasses << "\n";
  os << indent << "LogSteps: " << (this->LogSteps ? "on" : "off") << "\n";
  os << indent << "FairingContinuity: " << this->FairingContinuity << "\n";
  os << indent << "AbsoluteThresholds: " << (this->AbsoluteThresholds ? "on" : "off") << "\n";
  os << indent << "Alpha: " << this->Alpha << "\n";
  os << indent << "Offset: " << this->Offset << "\n";
  os << indent << "SkipAlphaWrapping: " << (this->SkipAlphaWrapping ? "on" : "off") << "\n";
  os << indent << "EnableBridgeCleanup: " << (this->EnableBridgeCleanup ? "on" : "off") << "\n";
  os << indent << "EnableBridgeRemesh: " << (this->EnableBridgeRemesh ? "on" : "off") << "\n";
  os << indent << "EnableBridgeSmooth: " << (this->EnableBridgeSmooth ? "on" : "off") << "\n";
  os << indent << "BridgeDilateLayers: " << this->BridgeDilateLayers << "\n";
  os << indent << "BridgeSmoothMethod: " << this->BridgeSmoothMethod << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXAutoMeshRepair::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* wrappedInput = vtkPolyData::GetData(inputVector[1], 0);
  vtkPolyData* outMesh = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outDiag = vtkPolyData::GetData(outputVector, 1);
  vtkPolyData* outWrapped = vtkPolyData::GetData(outputVector, 2);
  if (!input || !outMesh || !outDiag || !outWrapped)
  {
    vtkErrorMacro("Missing input or output (expect three vtkPolyData output ports).");
    return 0;
  }

  outDiag->Initialize();
  outWrapped->Initialize();

  int clustersRepaired = 0;
  int firstPassClusters = 0;
  int remaining = -1;

  auto finish = [&](vtkPolyData* mesh, vtkPolyData* attrSrc) -> int {
    outMesh->ShallowCopy(mesh);
    SetFieldInt(outMesh, "SHYXAutoMeshRepairStage", this->RepairStage);
    SetFieldInt(outMesh, "SHYXAutoMeshRepairFirstPassClusters", firstPassClusters);
    SetFieldInt(outMesh, "SHYXAutoMeshRepairClustersRepaired", clustersRepaired);
    SetFieldInt(outMesh, "SHYXAutoMeshRepairRemainingClusters", remaining < 0 ? -1 : remaining);
    if (this->UpdateAttributes && attrSrc)
    {
      this->interpolateAttributes(attrSrc, outMesh);
    }
    if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] done; stage=" << this->RepairStage
                       << " first-pass clusters=" << firstPassClusters
                       << " repaired=" << clustersRepaired << " remaining=" << remaining
                       << " out points=" << outMesh->GetNumberOfPoints()
                       << " cells=" << outMesh->GetNumberOfCells()
                       << " wrapped cells=" << outWrapped->GetNumberOfCells());
    }
    return 1;
  };

  if (this->RepairSelfIntersections && this->RepairStage == vtkSHYXAutoMeshRepair::UNION)
  {
    if (!wrappedInput || wrappedInput->GetNumberOfCells() == 0)
    {
      vtkErrorMacro("Union stage needs Wrapped patches (input port 1): connect port 2 of an "
                    "Extract and Alpha Wrap Auto Mesh Repair.");
      return 0;
    }

    const vtkIdType nRem = input->GetNumberOfCells();
    const vtkIdType nWrap = wrappedInput->GetNumberOfCells();
    if (nRem == 0)
    {
      if (this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXAutoMeshRepair] union: empty remainder; returning wrapped patches.");
      }
      remaining = 0;
      clustersRepaired = 1;
      return finish(wrappedInput, wrappedInput);
    }

    vtkNew<vtkAppendPolyData> append;
    append->AddInputData(input);
    append->AddInputData(wrappedInput);
    append->Update();
    vtkPolyData* appended = append->GetOutput();
    if (!appended || appended->GetNumberOfCells() == 0)
    {
      vtkErrorMacro("Union stage: append of remainder + wrapped patches failed.");
      return 0;
    }

    std::vector<vtkIdType> wrapCells(static_cast<size_t>(nWrap));
    for (vtkIdType i = 0; i < nWrap; ++i)
    {
      wrapCells[static_cast<size_t>(i)] = nRem + i;
    }

    vtkNew<vtkSHYXSelectionFillAlphaReunionFilter> reunion;
    ConfigureReunion(reunion, this);
    reunion->SetSkipAlphaWrapping(true);
    reunion->SetInputData(0, appended);
    reunion->SetInputData(1, SelectionFromCells(wrapCells));
    reunion->Update();

    vtkPolyData* united = reunion->GetOutput();
    if (!united || united->GetNumberOfCells() == 0)
    {
      vtkErrorMacro("Union stage: boolean union of remainder + wrapped patches produced an empty mesh.");
      return 0;
    }

    vtkSmartPointer<vtkPolyData> result = vtkSmartPointer<vtkPolyData>::New();
    result->ShallowCopy(united);
    vtkSmartPointer<vtkPolyData> consistent = MakeConsistentSurface(result);
    if (consistent && consistent->GetNumberOfCells() > 0)
    {
      result = consistent;
    }

    clustersRepaired = 1;
    remaining = 0;
    if (this->CheckSelfIntersection)
    {
      vtkNew<vtkSHYXMeshChecker> checker;
      checker->SetCheckSoupEdges(false);
      checker->SetCheckBoundary(false);
      checker->SetCheckSelfIntersection(true);
      checker->SetCheckOrient(false);
      checker->SetAttemptOrientRepair(false);
      checker->SetAttemptRepairSelfIntersections(false);
      checker->SetLogSteps(false);
      checker->SetUpdateAttributes(false);
      checker->SetInputData(result);
      checker->Update();
      if (vtkPolyData* diag = checker->GetOutput(1))
      {
        outDiag->ShallowCopy(diag);
      }
    }

    return finish(result, appended);
  }

  vtkNew<vtkSHYXMeshChecker> checker;
  checker->SetCheckSoupEdges(this->CheckSoupEdges);
  checker->SetCheckBoundary(this->CheckBoundary);
  checker->SetCheckSelfIntersection(this->CheckSelfIntersection);
  checker->SetCheckOrient(this->CheckOrient);
  checker->SetAttemptOrientRepair(this->AttemptOrientRepair);
  checker->SetAttemptRepairSelfIntersections(false);
  checker->SetLogSteps(this->LogSteps);
  checker->SetUpdateAttributes(false);
  checker->SetInputData(input);
  checker->Update();

  vtkPolyData* checkedMesh = checker->GetOutput(0);
  vtkPolyData* checkedDiag = checker->GetOutput(1);
  if (!checkedMesh || checkedMesh->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Mesh Checker front-end produced an empty mesh.");
    return 0;
  }
  if (checkedDiag)
  {
    outDiag->ShallowCopy(checkedDiag);
  }

  vtkSmartPointer<vtkPolyData> working = vtkSmartPointer<vtkPolyData>::New();
  working->ShallowCopy(checkedMesh);

  if (!this->RepairSelfIntersections)
  {
    if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] RepairSelfIntersections off: Mesh Checker soup "
                         "repair / diagnostics only.");
    }
    remaining = -1;
    return finish(working, input);
  }

  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(working);
  tri->PassLinesOff();
  tri->PassVertsOff();
  tri->Update();
  vtkPolyData* triOut = tri->GetOutput();
  if (!triOut || triOut->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Empty mesh after triangulation (self-intersection repair).");
    return 0;
  }
  working->ShallowCopy(triOut);
  {
    vtkSmartPointer<vtkPolyData> consistent = MakeConsistentSurface(working);
    if (consistent && consistent->GetNumberOfCells() > 0)
    {
      working = consistent;
    }
  }

  if (this->RepairStage != vtkSHYXAutoMeshRepair::EXTRACT_WRAP_AND_UNION)
  {
    std::vector<std::vector<vtkIdType>> seeds;
    std::string detectErr;
    if (!DetectIntersectionClusters(working, seeds, detectErr))
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] extract: detection failed (" << detectErr
                       << "). Returning Mesh Checker mesh.");
      remaining = -1;
      return finish(working, input);
    }
    firstPassClusters = static_cast<int>(seeds.size());
    if (seeds.empty())
    {
      remaining = 0;
      if (this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXAutoMeshRepair] extract: no self-intersections.");
      }
      return finish(working, input);
    }

    std::vector<std::vector<vtkIdType>> dilated;
    MergeDilatedClusters(working, seeds, this->DilateLayers, dilated);
    if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] extract: " << seeds.size() << " seed cluster(s) -> "
                       << dilated.size() << " dilated region(s); wrapping up to " << this->MaxPasses
                       << ", dilate=" << this->DilateLayers);
    }

    const vtkIdType nCells = working->GetNumberOfCells();
    std::vector<char> removed(static_cast<size_t>(nCells), 0);
    vtkNew<vtkAppendPolyData> wrappedAppend;
    const int nToWrap = std::min(this->MaxPasses, static_cast<int>(dilated.size()));
    for (int i = 0; i < nToWrap; ++i)
    {
      if (this->CheckAbort())
      {
        break;
      }
      const auto& cells = dilated[static_cast<size_t>(i)];
      if (cells.empty())
      {
        continue;
      }
      std::vector<char> mask(static_cast<size_t>(nCells), 0);
      for (vtkIdType cid : cells)
      {
        if (cid >= 0 && cid < nCells)
        {
          mask[static_cast<size_t>(cid)] = 1;
        }
      }
      vtkSmartPointer<vtkPolyData> patch = ExtractMaskedCells(working, mask, true);
      if (!patch || patch->GetNumberOfCells() == 0)
      {
        continue;
      }
      if (patch->GetNumberOfCells() >= nCells && this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXAutoMeshRepair] extract: region " << i
                         << " covers the whole mesh; Alpha Wrap will run on the entire surface.");
      }

      vtkSmartPointer<vtkPolyData> wrapped = FillThenWrap(patch, this->FairingContinuity,
        this->SkipAlphaWrapping, this->AbsoluteThresholds, this->Alpha, this->Offset);
      if (!wrapped || wrapped->GetNumberOfCells() == 0)
      {
        if (this->LogSteps)
        {
          vtkWarningMacro(<< "[SHYXAutoMeshRepair] extract: region " << i
                           << " fill/Alpha Wrap failed; leaving it on the remainder.");
        }
        continue;
      }

      vtkNew<vtkIntArray> clusterIds;
      clusterIds->SetName("SHYXAutoMeshRepairClusterId");
      clusterIds->SetNumberOfComponents(1);
      clusterIds->SetNumberOfTuples(wrapped->GetNumberOfCells());
      for (vtkIdType c = 0; c < wrapped->GetNumberOfCells(); ++c)
      {
        clusterIds->SetValue(c, i);
      }
      wrapped->GetCellData()->AddArray(clusterIds);
      wrappedAppend->AddInputData(wrapped);
      for (vtkIdType cid = 0; cid < nCells; ++cid)
      {
        if (mask[static_cast<size_t>(cid)])
        {
          removed[static_cast<size_t>(cid)] = 1;
        }
      }
      ++clustersRepaired;
      this->UpdateProgress(static_cast<double>(i + 1) / static_cast<double>(std::max(1, nToWrap)));
    }

    remaining = static_cast<int>(dilated.size()) - clustersRepaired;
    if (remaining < 0)
    {
      remaining = 0;
    }

    if (wrappedAppend->GetNumberOfInputConnections(0) > 0)
    {
      wrappedAppend->Update();
      if (vtkPolyData* wout = wrappedAppend->GetOutput())
      {
        outWrapped->ShallowCopy(wout);
      }
    }

    std::size_t nRemoved = 0;
    for (char c : removed)
    {
      nRemoved += (c != 0) ? 1u : 0u;
    }
    if (nRemoved == 0)
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] extract: no cluster was wrapped.");
      return finish(working, input);
    }
    if (nRemoved >= static_cast<std::size_t>(nCells))
    {
      if (this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXAutoMeshRepair] extract: remainder is empty (whole mesh wrapped).");
      }
      vtkNew<vtkPolyData> emptyRem;
      remaining = 0;
      return finish(emptyRem, input);
    }

    vtkSmartPointer<vtkPolyData> remainder = ExtractMaskedCells(working, removed, false);
    vtkNew<vtkSHYXHoleFillFilter> fillRem;
    fillRem->SetFairingContinuity(this->FairingContinuity);
    fillRem->SetInputData(remainder);
    fillRem->SetUpdateAttributes(false);
    fillRem->Update();
    vtkPolyData* filledRem = fillRem->GetOutput();
    if (filledRem && filledRem->GetNumberOfCells() > 0)
    {
      remainder->ShallowCopy(filledRem);
    }
    {
      vtkSmartPointer<vtkPolyData> consistent = MakeConsistentSurface(remainder);
      if (consistent && consistent->GetNumberOfCells() > 0)
      {
        remainder = consistent;
      }
    }
    return finish(remainder, input);
  }

  for (int pass = 0; pass < this->MaxPasses; ++pass)
  {
    if (this->CheckAbort())
    {
      break;
    }

    std::vector<std::vector<vtkIdType>> clusters;
    std::string detectErr;
    if (!DetectIntersectionClusters(working, clusters, detectErr))
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] pass " << pass << ": detection failed (" << detectErr
                       << "). Returning current mesh.");
      break;
    }

    if (pass == 0)
    {
      firstPassClusters = static_cast<int>(clusters.size());
    }

    if (clusters.empty())
    {
      remaining = 0;
      if (this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXAutoMeshRepair] pass " << pass << ": no self-intersections.");
      }
      break;
    }

    if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] pass " << pass << ": " << clusters.size()
                       << " intersecting cluster(s); repairing largest (" << clusters.front().size()
                       << " faces), dilate=" << this->DilateLayers);
    }

    bool repairedThisPass = false;
    for (const auto& cluster : clusters)
    {
      if (this->CheckAbort())
      {
        break;
      }
      if (cluster.empty())
      {
        continue;
      }

      std::vector<char> mask(static_cast<size_t>(working->GetNumberOfCells()), 0);
      for (vtkIdType cid : cluster)
      {
        if (cid >= 0 && cid < working->GetNumberOfCells())
        {
          mask[static_cast<size_t>(cid)] = 1;
        }
      }
      DilateCellMask(working, mask, this->DilateLayers);

      std::vector<vtkIdType> selected;
      selected.reserve(cluster.size());
      for (vtkIdType cid = 0; cid < working->GetNumberOfCells(); ++cid)
      {
        if (mask[static_cast<size_t>(cid)])
        {
          selected.push_back(cid);
        }
      }
      if (selected.empty() || static_cast<vtkIdType>(selected.size()) >= working->GetNumberOfCells())
      {
        if (selected.size() >= static_cast<size_t>(working->GetNumberOfCells()) && this->LogSteps)
        {
          vtkWarningMacro(<< "[SHYXAutoMeshRepair] dilated cluster covers the whole mesh; "
                             "Alpha Wrap will run on the entire surface.");
        }
      }
      if (selected.empty())
      {
        continue;
      }

      vtkSmartPointer<vtkSelection> sel = SelectionFromCells(selected);
      vtkNew<vtkSHYXSelectionFillAlphaReunionFilter> reunion;
      ConfigureReunion(reunion, this);
      reunion->SetInputData(0, working);
      reunion->SetInputData(1, sel);
      reunion->Update();

      vtkPolyData* repaired = reunion->GetOutput();
      if (!repaired || repaired->GetNumberOfCells() == 0)
      {
        if (this->LogSteps)
        {
          vtkWarningMacro(<< "[SHYXAutoMeshRepair] local fill/alpha-wrap/union produced an empty mesh; "
                             "trying next cluster.");
        }
        continue;
      }

      vtkSmartPointer<vtkPolyData> next = vtkSmartPointer<vtkPolyData>::New();
      next->DeepCopy(repaired);
      vtkSmartPointer<vtkPolyData> consistent = MakeConsistentSurface(next);
      working = (consistent && consistent->GetNumberOfCells() > 0) ? consistent : next;
      ++clustersRepaired;
      repairedThisPass = true;
      this->UpdateProgress(static_cast<double>(pass + 1) / static_cast<double>(this->MaxPasses));
      break;
    }

    if (!repairedThisPass)
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] remaining " << clusters.size()
                       << " cluster(s) failed local repair; stopping.");
      remaining = static_cast<int>(clusters.size());
      break;
    }
  }

  if (remaining < 0)
  {
    std::vector<std::vector<vtkIdType>> leftover;
    std::string leftoverErr;
    if (DetectIntersectionClusters(working, leftover, leftoverErr))
    {
      remaining = static_cast<int>(leftover.size());
    }
  }

  return finish(working, input);
}
