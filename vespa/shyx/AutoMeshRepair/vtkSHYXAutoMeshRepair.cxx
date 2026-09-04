#include "vtkSHYXAutoMeshRepair.h"

#include "vtkCGALHelper.h"
#include "vtkSHYXSelectionFillAlphaReunionFilter.h"

#include <vtkAppendPolyData.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractCells.h>
#include <vtkFieldData.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkSmartPointer.h>
#include <vtkTriangleFilter.h>

#include <CGAL/Polygon_mesh_processing/intersection.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
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

vtkSmartPointer<vtkPolyData> ExtractCellsAsPolyData(
  vtkPolyData* mesh, const std::vector<vtkIdType>& cells, int clusterId)
{
  if (!mesh || cells.empty())
  {
    return nullptr;
  }

  vtkNew<vtkIdList> ids;
  ids->SetNumberOfIds(static_cast<vtkIdType>(cells.size()));
  for (std::size_t i = 0; i < cells.size(); ++i)
  {
    ids->SetId(static_cast<vtkIdType>(i), cells[i]);
  }

  vtkNew<vtkExtractCells> extract;
  extract->SetInputData(mesh);
  extract->SetCellList(ids);
  extract->Update();
  vtkDataSet* extracted = vtkDataSet::SafeDownCast(extract->GetOutput());
  if (!extracted || extracted->GetNumberOfCells() == 0)
  {
    return nullptr;
  }

  vtkNew<vtkGeometryFilter> geom;
  geom->SetInputData(extracted);
  geom->Update();
  vtkPolyData* surface = geom->GetOutput();
  if (!surface || surface->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkPolyData> pd = vtkSmartPointer<vtkPolyData>::New();
  pd->ShallowCopy(surface);

  vtkNew<vtkIntArray> cluster;
  cluster->SetName("SHYX_ClusterId");
  cluster->SetNumberOfComponents(1);
  cluster->SetNumberOfTuples(pd->GetNumberOfCells());
  for (vtkIdType i = 0; i < pd->GetNumberOfCells(); ++i)
  {
    cluster->SetValue(i, clusterId);
  }
  pd->GetCellData()->AddArray(cluster);
  pd->GetCellData()->SetActiveScalars(cluster->GetName());

  vtkNew<vtkIntArray> reason;
  reason->SetName("SHYX_CheckReason");
  reason->SetNumberOfComponents(1);
  reason->SetNumberOfTuples(pd->GetNumberOfCells());
  for (vtkIdType i = 0; i < pd->GetNumberOfCells(); ++i)
  {
    reason->SetValue(i, 3);
  }
  pd->GetCellData()->AddArray(reason);

  return pd;
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

vtkSmartPointer<vtkPolyData> SoupRepairToPolyData(vtkPolyData* input, std::string& note)
{
  note.clear();
  vtkCGALHelper::Vespa_soup soup;
  vtkCGALHelper::toCGAL(input, &soup);
  try
  {
    (void)pmp::orient_polygon_soup(soup.points, soup.faces);
    pmp::repair_polygon_soup(soup.points, soup.faces);
    if (!pmp::is_polygon_soup_a_polygon_mesh(soup.faces))
    {
      note = "soup is not a polygon mesh after repair; continuing with triangulated VTK input.";
      return nullptr;
    }
    vtkCGALHelper::Vespa_surface surf;
    pmp::polygon_soup_to_polygon_mesh(soup.points, soup.faces, surf.surface);
    surf.coords = get(CGAL::vertex_point, surf.surface);
    vtkNew<vtkPolyData> out;
    if (!vtkCGALHelper::toVTK(&surf, out))
    {
      note = "toVTK after soup repair failed; continuing with triangulated VTK input.";
      return nullptr;
    }
    vtkNew<vtkTriangleFilter> triSoup;
    triSoup->SetInputData(out);
    triSoup->PassLinesOff();
    triSoup->PassVertsOff();
    triSoup->Update();
    vtkPolyData* triOut = triSoup->GetOutput();
    if (!triOut || triOut->GetNumberOfCells() == 0)
    {
      note = "soup repair produced no triangles; continuing with triangulated VTK input.";
      return nullptr;
    }
    vtkSmartPointer<vtkPolyData> pd = vtkSmartPointer<vtkPolyData>::New();
    pd->ShallowCopy(triOut);
    return pd;
  }
  catch (const std::exception& e)
  {
    note = std::string("soup orient/repair threw: ") + e.what() + "; continuing with triangulated VTK input.";
    return nullptr;
  }
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXAutoMeshRepair::vtkSHYXAutoMeshRepair()
{
  this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
int vtkSHYXAutoMeshRepair::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
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
  os << indent << "DilateLayers: " << this->DilateLayers << "\n";
  os << indent << "MaxPasses: " << this->MaxPasses << "\n";
  os << indent << "AttemptSoupRepair: " << (this->AttemptSoupRepair ? "on" : "off") << "\n";
  os << indent << "LogSteps: " << (this->LogSteps ? "on" : "off") << "\n";
  os << indent << "FairingContinuity: " << this->FairingContinuity << "\n";
  os << indent << "AbsoluteThresholds: " << (this->AbsoluteThresholds ? "on" : "off") << "\n";
  os << indent << "Alpha: " << this->Alpha << "\n";
  os << indent << "Offset: " << this->Offset << "\n";
  os << indent << "SkipAlphaWrapping: " << (this->SkipAlphaWrapping ? "on" : "off") << "\n";
  os << indent << "EnableBridgeCleanup: " << (this->EnableBridgeCleanup ? "on" : "off") << "\n";
  os << indent << "BridgeDilateLayers: " << this->BridgeDilateLayers << "\n";
  os << indent << "BridgeSmoothMethod: " << this->BridgeSmoothMethod << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXAutoMeshRepair::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* outMesh = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outDiag = vtkPolyData::GetData(outputVector, 1);
  if (!input || !outMesh || !outDiag)
  {
    vtkErrorMacro("Missing input or output (expect two vtkPolyData output ports).");
    return 0;
  }

  outDiag->Initialize();

  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(input);
  tri->PassLinesOff();
  tri->PassVertsOff();
  tri->Update();
  vtkPolyData* triOut = tri->GetOutput();
  if (!triOut || triOut->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Empty mesh after triangulation.");
    return 0;
  }

  vtkSmartPointer<vtkPolyData> working = vtkSmartPointer<vtkPolyData>::New();
  working->ShallowCopy(triOut);

  if (this->AttemptSoupRepair)
  {
    std::string soupNote;
    vtkSmartPointer<vtkPolyData> repaired = SoupRepairToPolyData(working, soupNote);
    if (repaired && repaired->GetNumberOfCells() > 0)
    {
      working = repaired;
      if (this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXAutoMeshRepair] soup repair OK; points=" << working->GetNumberOfPoints()
                         << " cells=" << working->GetNumberOfCells());
      }
    }
    else if (this->LogSteps && !soupNote.empty())
    {
      vtkWarningMacro(<< "[SHYXAutoMeshRepair] " << soupNote);
    }
  }

  {
    vtkSmartPointer<vtkPolyData> consistent = MakeConsistentSurface(working);
    if (consistent && consistent->GetNumberOfCells() > 0)
    {
      working = consistent;
    }
  }

  int clustersRepaired = 0;
  int firstPassClusters = 0;
  int remaining = -1;

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
      if (!clusters.empty())
      {
        vtkNew<vtkAppendPolyData> append;
        for (std::size_t i = 0; i < clusters.size(); ++i)
        {
          vtkSmartPointer<vtkPolyData> part =
            ExtractCellsAsPolyData(working, clusters[i], static_cast<int>(i + 1));
          if (part)
          {
            append->AddInputData(part);
          }
        }
        if (append->GetNumberOfInputConnections(0) > 0)
        {
          append->Update();
          if (vtkPolyData* diag = append->GetOutput())
          {
            outDiag->ShallowCopy(diag);
          }
        }
      }
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
      reunion->SetFairingContinuity(this->FairingContinuity);
      reunion->SetAbsoluteThresholds(this->AbsoluteThresholds);
      reunion->SetAlpha(this->Alpha);
      reunion->SetOffset(this->Offset);
      reunion->SetSkipAlphaWrapping(this->SkipAlphaWrapping);
      reunion->SetThrowOnSelfIntersection(this->ThrowOnSelfIntersection);
      reunion->SetOrientToBoundVolumeWhenNeeded(this->OrientToBoundVolumeWhenNeeded);
      reunion->SetEnableBridgeCleanup(this->EnableBridgeCleanup);
      reunion->SetBridgeDilateLayers(this->BridgeDilateLayers);
      reunion->SetBridgeDilateFromSeam(this->BridgeDilateFromSeam);
      reunion->SetBridgeTargetEdgeLength(this->BridgeTargetEdgeLength);
      reunion->SetBridgeRemeshIterations(this->BridgeRemeshIterations);
      reunion->SetBridgeRemeshRelaxationSteps(this->BridgeRemeshRelaxationSteps);
      reunion->SetBridgeSmoothMethod(this->BridgeSmoothMethod);
      reunion->SetBridgeSmoothIterations(this->BridgeSmoothIterations);
      reunion->SetBridgeSmoothTimeStep(this->BridgeSmoothTimeStep);
      reunion->SetBridgeFairContinuity(this->BridgeFairContinuity);
      reunion->SetUpdateAttributes(false);
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

  outMesh->ShallowCopy(working);
  SetFieldInt(outMesh, "SHYXAutoMeshRepairFirstPassClusters", firstPassClusters);
  SetFieldInt(outMesh, "SHYXAutoMeshRepairClustersRepaired", clustersRepaired);
  SetFieldInt(outMesh, "SHYXAutoMeshRepairRemainingClusters", remaining < 0 ? -1 : remaining);

  if (this->UpdateAttributes)
  {
    this->interpolateAttributes(input, outMesh);
  }

  if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXAutoMeshRepair] done; first-pass clusters=" << firstPassClusters
                     << " repaired=" << clustersRepaired << " remaining=" << remaining
                     << " out points=" << outMesh->GetNumberOfPoints()
                     << " cells=" << outMesh->GetNumberOfCells());
  }

  return 1;
}
