#include "vtkSHYXDataSetToPartitionedCollection.h"

#include <vtkAbstractArray.h>
#include <vtkCellArray.h>
#include <vtkCellType.h>
#include <vtkCellData.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataAssembly.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkIOSSReader.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkSmartPointer.h>
#include <vtkTetra.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXDataSetToPartitionedCollection);

namespace
{
/** vtkIOSSWriter expects contiguous global ids (1..N) per volume block. */
void SetContiguousGlobalIds(vtkUnstructuredGrid* ug)
{
  if (!ug)
  {
    return;
  }
  vtkNew<vtkIdTypeArray> pg;
  const vtkIdType np = ug->GetNumberOfPoints();
  pg->SetNumberOfTuples(np);
  for (vtkIdType i = 0; i < np; ++i)
  {
    pg->SetValue(i, i + 1);
  }
  ug->GetPointData()->SetGlobalIds(pg);

  vtkNew<vtkIdTypeArray> cg;
  const vtkIdType nc = ug->GetNumberOfCells();
  cg->SetNumberOfTuples(nc);
  for (vtkIdType i = 0; i < nc; ++i)
  {
    cg->SetValue(i, i + 1);
  }
  ug->GetCellData()->SetGlobalIds(cg);
}

/** Side sets still need contiguous cell GlobalIds; point GlobalIds must stay volume-unique for IOSS. */
void SetContiguousCellGlobalIdsPolyData(vtkPolyData* pd)
{
  if (!pd)
  {
    return;
  }
  vtkNew<vtkIdTypeArray> cg;
  const vtkIdType nc = pd->GetNumberOfCells();
  cg->SetNumberOfTuples(nc);
  for (vtkIdType i = 0; i < nc; ++i)
  {
    cg->SetValue(i, i + 1);
  }
  pd->GetCellData()->SetGlobalIds(cg);
}

void RestoreSurfacePointGlobalIdsFromVolume(vtkPolyData* surf, vtkUnstructuredGrid* tetVol)
{
  if (!surf || !tetVol)
  {
    return;
  }
  auto* origPt = vtkIdTypeArray::SafeDownCast(surf->GetPointData()->GetArray("vtkOriginalPointIds"));
  auto* volPtG = vtkIdTypeArray::SafeDownCast(tetVol->GetPointData()->GetGlobalIds());
  if (!origPt || !volPtG)
  {
    return;
  }
  const vtkIdType np = surf->GetNumberOfPoints();
  vtkNew<vtkIdTypeArray> pg;
  pg->SetNumberOfTuples(np);
  for (vtkIdType p = 0; p < np; ++p)
  {
    pg->SetValue(p, volPtG->GetValue(origPt->GetValue(p)));
  }
  surf->GetPointData()->SetGlobalIds(pg);
}

void AssignStandalonePatchPointGlobalIds(vtkPolyData* pd, vtkIdType& nextGid)
{
  const vtkIdType npt = pd->GetNumberOfPoints();
  vtkNew<vtkIdTypeArray> pg;
  pg->SetNumberOfTuples(npt);
  for (vtkIdType p = 0; p < npt; ++p)
  {
    pg->SetValue(p, nextGid + p);
  }
  nextGid += npt;
  pd->GetPointData()->SetGlobalIds(pg);
}

void AddCellObjectId(vtkDataSet* ds, int objectId)
{
  if (!ds || ds->GetNumberOfCells() == 0)
  {
    return;
  }
  vtkNew<vtkIntArray> oid;
  oid->SetName("object_id");
  oid->SetNumberOfTuples(ds->GetNumberOfCells());
  oid->FillComponent(0, objectId);
  ds->GetCellData()->AddArray(oid);
}

bool UnorderedTriMatch(vtkIdType a0, vtkIdType a1, vtkIdType a2, vtkIdType b0, vtkIdType b1, vtkIdType b2)
{
  std::array<vtkIdType, 3> A = { a0, a1, a2 };
  std::array<vtkIdType, 3> B = { b0, b1, b2 };
  std::sort(A.begin(), A.end());
  std::sort(B.begin(), B.end());
  return A[0] == B[0] && A[1] == B[1] && A[2] == B[2];
}

/** Which tet face (0..3) matches the surface triangle (volume point ids). */
int FindTetFaceIndexForTriangle(vtkUnstructuredGrid* tet, vtkIdType tetCellId, vtkIdType t0, vtkIdType t1,
  vtkIdType t2)
{
  if (tet->GetCellType(tetCellId) != VTK_TETRA)
  {
    return -1;
  }
  vtkNew<vtkIdList> cpts;
  tet->GetCellPoints(tetCellId, cpts);
  if (cpts->GetNumberOfIds() != 4)
  {
    return -1;
  }
  const vtkIdType* p = cpts->GetPointer(0);
  for (int face = 0; face < 4; ++face)
  {
    const vtkIdType* fl = vtkTetra::GetFaceArray(face);
    const vtkIdType f0 = p[fl[0]];
    const vtkIdType f1 = p[fl[1]];
    const vtkIdType f2 = p[fl[2]];
    if (UnorderedTriMatch(t0, t1, t2, f0, f1, f2))
    {
      return face;
    }
  }
  return -1;
}

/** Side-set cell array element_side: (volume cell GlobalId, Exodus tet face 1..4). PassThrough ids. */
void PrepareTetBoundarySurfaceForIoss(vtkUnstructuredGrid* tetVol, vtkPolyData* surf)
{
  if (!tetVol || !surf || surf->GetNumberOfCells() == 0)
  {
    return;
  }

  auto* origCell = vtkIdTypeArray::SafeDownCast(surf->GetCellData()->GetArray("vtkOriginalCellIds"));
  auto* origPt = vtkIdTypeArray::SafeDownCast(surf->GetPointData()->GetArray("vtkOriginalPointIds"));
  auto* volCellG = vtkIdTypeArray::SafeDownCast(tetVol->GetCellData()->GetGlobalIds());
  auto* volPtG = vtkIdTypeArray::SafeDownCast(tetVol->GetPointData()->GetGlobalIds());
  if (!origCell || !origPt || !volCellG || !volPtG)
  {
    return;
  }

  const vtkIdType nf = surf->GetNumberOfCells();
  vtkNew<vtkIntArray> es;
  es->SetName("element_side");
  es->SetNumberOfComponents(2);
  es->SetNumberOfTuples(nf);

  vtkNew<vtkIdList> cpts;
  for (vtkIdType fi = 0; fi < nf; ++fi)
  {
    const vtkIdType ocid = origCell->GetValue(fi);
    const vtkIdType globalElem = volCellG->GetValue(ocid);
    surf->GetCellPoints(fi, cpts);
    int exodusFace = 1;
    if (cpts->GetNumberOfIds() == 3)
    {
      const vtkIdType s0 = origPt->GetValue(cpts->GetId(0));
      const vtkIdType s1 = origPt->GetValue(cpts->GetId(1));
      const vtkIdType s2 = origPt->GetValue(cpts->GetId(2));
      const int found = FindTetFaceIndexForTriangle(tetVol, ocid, s0, s1, s2);
      if (found >= 0)
      {
        exodusFace = found + 1;
      }
    }
    es->SetTuple2(fi, static_cast<int>(globalElem), exodusFace);
  }
  surf->GetCellData()->AddArray(es);
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

/**
 * vtkPolyDataConnectivityFilter often keeps the full input point list; only some points are
 * referenced by extracted cells. Drop unreferenced points and remap connectivity so each side{i} /
 * node{i} contains only vertices used by that patch's cells.
 */
void StripUnreferencedPoints(vtkPolyData* pd)
{
  if (!pd || pd->GetNumberOfPoints() == 0 || pd->GetNumberOfCells() == 0)
  {
    return;
  }

  pd->BuildCells();
  const vtkIdType np = pd->GetNumberOfPoints();
  std::vector<unsigned char> used(static_cast<size_t>(np), 0);
  vtkNew<vtkIdList> cpts;

  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    pd->GetCellPoints(cid, cpts);
    for (vtkIdType i = 0; i < cpts->GetNumberOfIds(); ++i)
    {
      const vtkIdType pid = cpts->GetId(i);
      if (pid >= 0 && pid < np)
      {
        used[static_cast<size_t>(pid)] = 1;
      }
    }
  }

  vtkIdType nUsed = 0;
  for (vtkIdType p = 0; p < np; ++p)
  {
    if (used[static_cast<size_t>(p)])
    {
      ++nUsed;
    }
  }

  if (nUsed == np)
  {
    return;
  }

  std::vector<vtkIdType> oldToNew(static_cast<size_t>(np), -1);
  vtkIdType newIx = 0;
  for (vtkIdType p = 0; p < np; ++p)
  {
    if (used[static_cast<size_t>(p)])
    {
      oldToNew[static_cast<size_t>(p)] = newIx++;
    }
  }

  vtkNew<vtkPoints> newPts;
  newPts->SetNumberOfPoints(nUsed);
  for (vtkIdType p = 0; p < np; ++p)
  {
    if (!used[static_cast<size_t>(p)])
    {
      continue;
    }
    double x[3];
    pd->GetPoint(p, x);
    newPts->SetPoint(oldToNew[static_cast<size_t>(p)], x);
  }

  vtkPointData* ipd = pd->GetPointData();
  vtkNew<vtkPointData> opd;
  for (int ai = 0; ai < ipd->GetNumberOfArrays(); ++ai)
  {
    vtkAbstractArray* arr = ipd->GetAbstractArray(ai);
    if (!arr || static_cast<vtkIdType>(arr->GetNumberOfTuples()) != np)
    {
      continue;
    }
    vtkAbstractArray* na = arr->NewInstance();
    na->SetName(arr->GetName());
    na->SetNumberOfComponents(arr->GetNumberOfComponents());
    na->SetNumberOfTuples(nUsed);
    for (vtkIdType p = 0; p < np; ++p)
    {
      if (used[static_cast<size_t>(p)])
      {
        na->SetTuple(oldToNew[static_cast<size_t>(p)], p, arr);
      }
    }
    opd->AddArray(na);
    na->Delete();
  }

  vtkNew<vtkCellArray> newVerts;
  vtkNew<vtkCellArray> newLines;
  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkCellArray> newStrips;

  for (vtkIdType cid = 0; cid < pd->GetNumberOfCells(); ++cid)
  {
    const int ct = pd->GetCellType(cid);
    pd->GetCellPoints(cid, cpts);
    const int ncp = cpts->GetNumberOfIds();
    std::vector<vtkIdType> mapped(static_cast<size_t>(ncp));
    for (vtkIdType i = 0; i < ncp; ++i)
    {
      mapped[static_cast<size_t>(i)] = oldToNew[static_cast<size_t>(cpts->GetId(i))];
    }

    switch (ct)
    {
      case VTK_VERTEX:
      case VTK_POLY_VERTEX:
        newVerts->InsertNextCell(ncp, mapped.data());
        break;
      case VTK_LINE:
      case VTK_POLY_LINE:
        newLines->InsertNextCell(ncp, mapped.data());
        break;
      case VTK_TRIANGLE:
      case VTK_QUAD:
      case VTK_POLYGON:
      case VTK_PIXEL:
        newPolys->InsertNextCell(ncp, mapped.data());
        break;
      case VTK_TRIANGLE_STRIP:
        newStrips->InsertNextCell(ncp, mapped.data());
        break;
      default:
        if (ncp == 1)
        {
          newVerts->InsertNextCell(1, mapped.data());
        }
        else if (ncp == 2)
        {
          newLines->InsertNextCell(2, mapped.data());
        }
        else if (ncp >= 3)
        {
          newPolys->InsertNextCell(ncp, mapped.data());
        }
        break;
    }
  }

  pd->SetPoints(newPts);
  pd->SetVerts(newVerts);
  pd->SetLines(newLines);
  pd->SetPolys(newPolys);
  pd->SetStrips(newStrips);
  vtkPointData* outp = pd->GetPointData();
  outp->ShallowCopy(opd);
  if (vtkIdTypeArray* g = vtkIdTypeArray::SafeDownCast(outp->GetAbstractArray("GlobalIds")))
  {
    outp->SetGlobalIds(g);
  }
  if (vtkDataArray* nrm = outp->GetArray("Normals"))
  {
    outp->SetNormals(nrm);
  }
  pd->Modified();
}

void CollectCuspConnectedSurfacePieces(vtkPolyData* surface, double featureAngle,
  std::vector<vtkSmartPointer<vtkPolyData>>* outPieces)
{
  outPieces->clear();
  if (!surface || surface->GetNumberOfCells() == 0)
  {
    return;
  }

  vtkNew<vtkPolyDataNormals> nrm;
  nrm->SetInputData(surface);
  nrm->SplittingOn();
  nrm->SetFeatureAngle(featureAngle);
  nrm->ConsistencyOn();
  nrm->AutoOrientNormalsOn();
  nrm->Update();

  vtkPolyData* split = nrm->GetOutput();

  vtkNew<vtkPolyDataConnectivityFilter> probe;
  probe->SetInputData(split);
  probe->SetExtractionModeToAllRegions();
  probe->ColorRegionsOn();
  probe->Update();

  const int nRegions = static_cast<int>(probe->GetNumberOfExtractedRegions());
  if (nRegions <= 0)
  {
    return;
  }

  vtkNew<vtkPolyDataConnectivityFilter> conn;
  conn->SetInputData(split);
  conn->SetExtractionModeToSpecifiedRegions();
  conn->ColorRegionsOff();

  for (int r = 0; r < nRegions; ++r)
  {
    conn->InitializeSpecifiedRegionList();
    conn->AddSpecifiedRegion(r);
    conn->Update();
    vtkPolyData* out = conn->GetOutput();
    if (!out || out->GetNumberOfCells() == 0)
    {
      continue;
    }
    vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
    copy->DeepCopy(out);
    StripUnreferencedPoints(copy);
    outPieces->push_back(copy);
  }
}

/** IOSS-style names + Exodus-style entity id. vtkIOSSReader::ENTITY_TYPE() is protected in PV 6, so
 *  entity kind is conveyed by vtkDataAssembly (element_blocks / side_sets / node_sets). */
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

void AppendPdcBlock(vtkPartitionedDataSetCollection* coll, unsigned int& blockIndex, vtkDataSet* ds,
  const char* iossName, int entityId)
{
  vtkNew<vtkPartitionedDataSet> pds;
  pds->SetPartition(0, ds);
  coll->SetPartitionedDataSet(blockIndex, pds);
  SetIossBlockMeta(coll, blockIndex, iossName, entityId);
  ++blockIndex;
}

void BuildIossAssembly(vtkPartitionedDataSetCollection* coll, bool hasTet,
  unsigned int nSideNodePairs)
{
  vtkNew<vtkDataAssembly> rootAsm;
  rootAsm->SetRootNodeName("IOSS");

  const int elemBlocksNode = rootAsm->AddNode("element_blocks");
  const int sideSetsNode = rootAsm->AddNode("side_sets");
  const int nodeSetsNode = rootAsm->AddNode("node_sets");

  unsigned int dsCursor = 0;

  if (hasTet)
  {
    const char* elemName = "tetrahedra";
    const int leaf =
      rootAsm->AddNode(vtkDataAssembly::MakeValidNodeName(elemName).c_str(), elemBlocksNode);
    rootAsm->SetAttribute(leaf, "label", elemName);
    rootAsm->AddDataSetIndex(leaf, dsCursor++);
  }

  for (unsigned int i = 0; i < nSideNodePairs; ++i)
  {
    const std::string sideName = "side" + std::to_string(i);
    const int leafS = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(sideName.c_str()).c_str(), sideSetsNode);
    rootAsm->SetAttribute(leafS, "label", sideName.c_str());
    rootAsm->AddDataSetIndex(leafS, dsCursor++);

    const std::string nodeName = "node" + std::to_string(i);
    const int leafN = rootAsm->AddNode(
      vtkDataAssembly::MakeValidNodeName(nodeName.c_str()).c_str(), nodeSetsNode);
    rootAsm->SetAttribute(leafN, "label", nodeName.c_str());
    rootAsm->AddDataSetIndex(leafN, dsCursor++);
  }

  coll->SetDataAssembly(rootAsm);
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXDataSetToPartitionedCollection::vtkSHYXDataSetToPartitionedCollection()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkSHYXDataSetToPartitionedCollection::~vtkSHYXDataSetToPartitionedCollection() = default;

//------------------------------------------------------------------------------
void vtkSHYXDataSetToPartitionedCollection::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FeatureAngle: " << this->FeatureAngle << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXDataSetToPartitionedCollection::FillInputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXDataSetToPartitionedCollection::FillOutputPortInformation(
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
int vtkSHYXDataSetToPartitionedCollection::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
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
  unsigned int blockIndex = 0;
  int nextSurfaceObjectId = 1;
  vtkSmartPointer<vtkUnstructuredGrid> volumeGridForIoss;

  vtkUnstructuredGrid* ug = vtkUnstructuredGrid::SafeDownCast(input);
  vtkNew<vtkPolyData> surfaceWork;

  if (ug)
  {
    const vtkIdType nCells = ug->GetNumberOfCells();
    vtkNew<vtkIdList> tetIds;
    tetIds->Allocate(nCells);
    for (vtkIdType c = 0; c < nCells; ++c)
    {
      if (ug->GetCellType(c) == VTK_TETRA)
      {
        tetIds->InsertNextId(c);
      }
    }

    if (tetIds->GetNumberOfIds() > 0)
    {
      vtkNew<vtkExtractCells> exTet;
      exTet->SetInputData(ug);
      exTet->SetCellList(tetIds);
      exTet->Update();
      vtkUnstructuredGrid* tetOut = exTet->GetOutput();
      if (tetOut && tetOut->GetNumberOfCells() > 0)
      {
        volumeGridForIoss = vtkSmartPointer<vtkUnstructuredGrid>::New();
        volumeGridForIoss->DeepCopy(tetOut);
        SetContiguousGlobalIds(volumeGridForIoss);

        // Boundary of tet-only mesh; pass-through ids for vtkIOSSWriter (GlobalIds + element_side).
        vtkNew<vtkDataSetSurfaceFilter> surf;
        surf->SetInputData(volumeGridForIoss);
        surf->PassThroughCellIdsOn();
        surf->PassThroughPointIdsOn();
        surf->Update();
        surfaceWork->DeepCopy(surf->GetOutput());
        PrepareTetBoundarySurfaceForIoss(volumeGridForIoss, surfaceWork.GetPointer());

        AddCellObjectId(volumeGridForIoss, 1);
        AppendPdcBlock(result, blockIndex, volumeGridForIoss, "tetrahedra", 1);
        nextSurfaceObjectId = 2;
      }
    }
  }
  else if (vtkPolyData* pd = vtkPolyData::SafeDownCast(input))
  {
    surfaceWork->DeepCopy(pd);
  }
  else
  {
    vtkNew<vtkDataSetSurfaceFilter> surf;
    surf->SetInputData(input);
    surf->Update();
    surfaceWork->DeepCopy(surf->GetOutput());
  }

  std::vector<vtkSmartPointer<vtkPolyData>> sidePieces;
  CollectCuspConnectedSurfacePieces(surfaceWork.GetPointer(), this->FeatureAngle, &sidePieces);

  const unsigned int nPairs = static_cast<unsigned int>(sidePieces.size());
  int sideOid = nextSurfaceObjectId;
  int nodeOid = nextSurfaceObjectId + static_cast<int>(nPairs);
  vtkIdType nextStandaloneSurfacePointGid = 1;

  for (unsigned int i = 0; i < nPairs; ++i)
  {
    vtkPolyData* sidePatch = sidePieces[i];

    vtkNew<vtkPolyData> sideCopy;
    sideCopy->DeepCopy(sidePatch);
    if (volumeGridForIoss)
    {
      RestoreSurfacePointGlobalIdsFromVolume(sideCopy, volumeGridForIoss);
    }
    else
    {
      AssignStandalonePatchPointGlobalIds(sideCopy, nextStandaloneSurfacePointGid);
    }
    SetContiguousCellGlobalIdsPolyData(sideCopy);
    AddCellObjectId(sideCopy, sideOid++);
    const std::string sideName = "side" + std::to_string(i);
    AppendPdcBlock(result, blockIndex, sideCopy, sideName.c_str(), static_cast<int>(i) + 1);

    vtkSmartPointer<vtkPolyData> nodePd = BuildNodeSetPolyData(sideCopy);
    AddCellObjectId(nodePd, nodeOid++);
    const std::string nodeName = "node" + std::to_string(i);
    AppendPdcBlock(result, blockIndex, nodePd, nodeName.c_str(), static_cast<int>(i) + 1);
  }

  BuildIossAssembly(result, volumeGridForIoss != nullptr, nPairs);

  output->ShallowCopy(result);

  return 1;
}

VTK_ABI_NAMESPACE_END
