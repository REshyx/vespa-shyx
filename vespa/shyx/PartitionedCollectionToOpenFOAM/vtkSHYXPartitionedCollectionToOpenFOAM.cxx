#include "vtkSHYXPartitionedCollectionToOpenFOAM.h"

#include <vtkAlgorithm.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataArray.h>
#include <vtkDataAssembly.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkGenericCell.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkOpenFOAMReader.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXPartitionedCollectionToOpenFOAM);

namespace
{
namespace fs = std::filesystem;

struct FaceKeyHash
{
  std::size_t operator()(const std::vector<vtkIdType>& v) const noexcept
  {
    std::size_t h = v.size();
    for (vtkIdType x : v)
    {
      h ^= static_cast<std::size_t>(x) + 0x9e3779b9u + (h << 6) + (h >> 2);
    }
    return h;
  }
};

using FaceKey = std::vector<vtkIdType>;
using FaceKeyMap = std::unordered_map<FaceKey, int, FaceKeyHash>;

struct UniqueFace
{
  vtkIdType Owner = -1;
  vtkIdType Neighbour = -1;
  std::vector<vtkIdType> PointIds;
  int PatchIndex = -1;
};

struct PdcLayout
{
  bool HasIossAssembly = false;
  bool HasElementBlock = false;
  int ElementBlockCount = 0;
  unsigned int ElementBlockPdcIndex = 0;
  std::vector<unsigned int> SideSetPdcIndices;
};

struct PatchInfo
{
  std::string Name;
  std::string Type;
  std::vector<int> FaceIds;
};

FaceKey SortedKey(const std::vector<vtkIdType>& gids)
{
  FaceKey key = gids;
  std::sort(key.begin(), key.end());
  return key;
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

bool ParseIossLayout(vtkDataAssembly* assembly, PdcLayout* layout)
{
  if (!assembly || !layout)
  {
    return false;
  }
  layout->HasIossAssembly = false;
  layout->HasElementBlock = false;
  layout->ElementBlockCount = 0;
  layout->ElementBlockPdcIndex = 0;
  layout->SideSetPdcIndices.clear();

  const int elemBlocks =
    FirstAssemblyNodeByPath(assembly, "/IOSS/element_blocks", "/element_blocks");
  const int sideSets = FirstAssemblyNodeByPath(assembly, "/IOSS/side_sets", "/side_sets");
  const int nodeSets = FirstAssemblyNodeByPath(assembly, "/IOSS/node_sets", "/node_sets");
  layout->HasIossAssembly = (elemBlocks >= 0 || sideSets >= 0 || nodeSets >= 0);

  if (elemBlocks >= 0)
  {
    const int nChildren = assembly->GetNumberOfChildren(elemBlocks);
    for (int i = 0; i < nChildren; ++i)
    {
      const int child = assembly->GetChild(elemBlocks, i);
      const unsigned int pdcIndex = FirstDataSetIndexForAssemblyChild(assembly, child);
      if (pdcIndex == static_cast<unsigned int>(-1))
      {
        continue;
      }
      ++layout->ElementBlockCount;
      if (!layout->HasElementBlock)
      {
        layout->HasElementBlock = true;
        layout->ElementBlockPdcIndex = pdcIndex;
      }
    }
  }
  CollectAssemblyChildDataSetIndices(assembly, sideSets, &layout->SideSetPdcIndices);
  return layout->HasIossAssembly;
}

bool IsVolumeUnstructuredGrid(vtkDataSet* ds)
{
  auto* ug = vtkUnstructuredGrid::SafeDownCast(ds);
  return ug && ug->GetNumberOfCells() > 0 && ug->GetNumberOfPoints() > 0;
}

bool IsSideSurface(vtkDataSet* ds)
{
  auto* pd = vtkPolyData::SafeDownCast(ds);
  if (!pd)
  {
    return false;
  }
  return pd->GetNumberOfPolys() > 0 || pd->GetNumberOfStrips() > 0;
}

vtkIdType ArrayIdValue(vtkDataArray* arr, vtkIdType i)
{
  if (!arr || i < 0 || i >= arr->GetNumberOfTuples())
  {
    return -1;
  }
  return static_cast<vtkIdType>(arr->GetTuple1(i));
}

void CellCentroid(vtkUnstructuredGrid* ug, vtkIdType cid, double c[3])
{
  vtkNew<vtkIdList> pts;
  ug->GetCellPoints(cid, pts);
  c[0] = c[1] = c[2] = 0.0;
  const vtkIdType n = pts->GetNumberOfIds();
  if (n == 0)
  {
    return;
  }
  for (vtkIdType i = 0; i < n; ++i)
  {
    double p[3];
    ug->GetPoint(pts->GetId(i), p);
    c[0] += p[0];
    c[1] += p[1];
    c[2] += p[2];
  }
  const double inv = 1.0 / static_cast<double>(n);
  c[0] *= inv;
  c[1] *= inv;
  c[2] *= inv;
}

void EnsureOutwardWinding(vtkUnstructuredGrid* ug, vtkIdType owner, std::vector<vtkIdType>& pids)
{
  if (!ug || pids.size() < 3)
  {
    return;
  }
  double cellC[3];
  CellCentroid(ug, owner, cellC);
  double p0[3], p1[3], p2[3], u[3], v[3], n[3], fc[3] = { 0.0, 0.0, 0.0 };
  for (vtkIdType pid : pids)
  {
    double p[3];
    ug->GetPoint(pid, p);
    fc[0] += p[0];
    fc[1] += p[1];
    fc[2] += p[2];
  }
  const double inv = 1.0 / static_cast<double>(pids.size());
  fc[0] *= inv;
  fc[1] *= inv;
  fc[2] *= inv;
  ug->GetPoint(pids[0], p0);
  ug->GetPoint(pids[1], p1);
  ug->GetPoint(pids[2], p2);
  vtkMath::Subtract(p1, p0, u);
  vtkMath::Subtract(p2, p0, v);
  vtkMath::Cross(u, v, n);
  const double outward[3] = { fc[0] - cellC[0], fc[1] - cellC[1], fc[2] - cellC[2] };
  if (vtkMath::Dot(n, outward) < 0.0)
  {
    std::reverse(pids.begin(), pids.end());
  }
}

bool ExplodeVolumeFaces(vtkUnstructuredGrid* ug, std::vector<UniqueFace>* faces, FaceKeyMap* keyToIndex,
  std::string* err)
{
  auto* ptG = ug->GetPointData() ? ug->GetPointData()->GetGlobalIds() : nullptr;
  if (!ptG || ptG->GetNumberOfTuples() != ug->GetNumberOfPoints())
  {
    *err = "Volume block is missing point GlobalIds (1..N). Connect SHYX DataSet To Partitioned "
           "Collection after TetGen.";
    return false;
  }

  vtkNew<vtkGenericCell> cell;
  const vtkIdType nCells = ug->GetNumberOfCells();
  vtkIdType nVolumeCells = 0;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    ug->GetCell(cid, cell);
    if (cell->GetCellDimension() != 3)
    {
      continue;
    }
    ++nVolumeCells;
    const int nFaces = cell->GetNumberOfFaces();
    if (nFaces <= 0)
    {
      *err = "Volume cell " + std::to_string(cid) + " has no faces.";
      return false;
    }
    for (int f = 0; f < nFaces; ++f)
    {
      vtkCell* face = cell->GetFace(f);
      if (!face)
      {
        continue;
      }
      const int npts = face->GetNumberOfPoints();
      if (npts < 3)
      {
        *err = "Volume cell " + std::to_string(cid) + " has a degenerate face.";
        return false;
      }
      std::vector<vtkIdType> vtkIds(static_cast<size_t>(npts));
      std::vector<vtkIdType> gids(static_cast<size_t>(npts));
      for (int i = 0; i < npts; ++i)
      {
        const vtkIdType pid = face->GetPointId(i);
        vtkIds[static_cast<size_t>(i)] = pid;
        gids[static_cast<size_t>(i)] = ArrayIdValue(ptG, pid);
        if (gids[static_cast<size_t>(i)] < 0)
        {
          *err = "Volume point GlobalIds are incomplete.";
          return false;
        }
      }
      const FaceKey key = SortedKey(gids);
      auto it = keyToIndex->find(key);
      if (it == keyToIndex->end())
      {
        EnsureOutwardWinding(ug, cid, vtkIds);
        UniqueFace uf;
        uf.Owner = cid;
        uf.PointIds = std::move(vtkIds);
        const int idx = static_cast<int>(faces->size());
        faces->push_back(std::move(uf));
        (*keyToIndex)[key] = idx;
      }
      else
      {
        UniqueFace& uf = (*faces)[static_cast<size_t>(it->second)];
        if (uf.Neighbour >= 0)
        {
          *err = "Non-manifold volume face (shared by more than two cells).";
          return false;
        }
        uf.Neighbour = cid;
      }
    }
  }
  if (nVolumeCells == 0)
  {
    *err = "Volume block has no 3D cells.";
    return false;
  }
  return true;
}

void BuildCellGlobalIdMap(vtkUnstructuredGrid* ug, std::unordered_map<vtkIdType, vtkIdType>* out)
{
  out->clear();
  vtkDataArray* cg = ug->GetCellData() ? ug->GetCellData()->GetGlobalIds() : nullptr;
  if (!cg)
  {
    return;
  }
  const vtkIdType n = ug->GetNumberOfCells();
  out->reserve(static_cast<size_t>(n));
  for (vtkIdType i = 0; i < n; ++i)
  {
    (*out)[ArrayIdValue(cg, i)] = i;
  }
}

int LookupFaceIndexByGids(const FaceKeyMap& keyToIndex, const std::vector<vtkIdType>& gids)
{
  if (gids.size() < 3)
  {
    return -1;
  }
  auto it = keyToIndex.find(SortedKey(gids));
  if (it == keyToIndex.end())
  {
    return -1;
  }
  return it->second;
}

int LookupFaceIndexByElementSide(vtkUnstructuredGrid* ug, vtkDataArray* ptG,
  const std::unordered_map<vtkIdType, vtkIdType>& cellGidToIndex, const FaceKeyMap& keyToIndex,
  vtkDataArray* elementSide, vtkIdType sideCell)
{
  if (!elementSide || elementSide->GetNumberOfComponents() < 2 ||
    sideCell >= elementSide->GetNumberOfTuples())
  {
    return -1;
  }
  const vtkIdType elemGid = static_cast<vtkIdType>(elementSide->GetComponent(sideCell, 0));
  const int exoFace = static_cast<int>(elementSide->GetComponent(sideCell, 1));
  auto cit = cellGidToIndex.find(elemGid);
  if (cit == cellGidToIndex.end() || exoFace < 1)
  {
    return -1;
  }
  vtkNew<vtkGenericCell> cell;
  ug->GetCell(cit->second, cell);
  const int faceIdx = exoFace - 1;
  if (faceIdx < 0 || faceIdx >= cell->GetNumberOfFaces())
  {
    return -1;
  }
  vtkCell* face = cell->GetFace(faceIdx);
  if (!face || face->GetNumberOfPoints() < 3)
  {
    return -1;
  }
  std::vector<vtkIdType> gids(static_cast<size_t>(face->GetNumberOfPoints()));
  for (int i = 0; i < face->GetNumberOfPoints(); ++i)
  {
    gids[static_cast<size_t>(i)] = ArrayIdValue(ptG, face->GetPointId(i));
  }
  return LookupFaceIndexByGids(keyToIndex, gids);
}

std::string SanitizeFoamName(const std::string& raw)
{
  std::string out;
  out.reserve(raw.size());
  for (unsigned char ch : raw)
  {
    if (std::isalnum(ch) || ch == '_')
    {
      out.push_back(static_cast<char>(ch));
    }
    else
    {
      out.push_back('_');
    }
  }
  while (!out.empty() && out.front() == '_')
  {
    out.erase(out.begin());
  }
  if (out.empty())
  {
    out = "side";
  }
  if (std::isdigit(static_cast<unsigned char>(out.front())))
  {
    out.insert(out.begin(), 'p');
  }
  return out;
}

std::string UniqueFoamName(const std::string& base, const std::vector<PatchInfo>& patches)
{
  std::string name = base;
  int suffix = 2;
  auto taken = [&](const std::string& n) {
    for (const PatchInfo& p : patches)
    {
      if (p.Name == n)
      {
        return true;
      }
    }
    return false;
  };
  while (taken(name))
  {
    name = base + "_" + std::to_string(suffix++);
  }
  return name;
}

std::string InferPatchType(const std::string& name)
{
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.find("wall") != std::string::npos)
  {
    return "wall";
  }
  if (lower.find("empty") != std::string::npos)
  {
    return "empty";
  }
  if (lower.find("symmetry") != std::string::npos)
  {
    return "symmetryPlane";
  }
  return "patch";
}

void WriteFoamHeader(std::ostream& os, const char* cls, const char* object, const char* location)
{
  os << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
        "| =========                 |                                                 |\n"
        "| \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |\n"
        "|  \\\\    /   O peration     | Version:  v2412                                 |\n"
        "|   \\\\  /    A nd           | Website:  www.openfoam.com                      |\n"
        "|    \\\\/     M anipulation  |                                                 |\n"
        "\\*---------------------------------------------------------------------------*/\n"
        "FoamFile\n"
        "{\n"
        "    version     2.0;\n"
        "    format      ascii;\n"
        "    class       "
     << cls << ";\n"
     << "    location    \"" << location << "\";\n"
     << "    object      " << object << ";\n"
        "}\n"
        "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n";
}

bool EnsureDir(const fs::path& path, std::string* err)
{
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec)
  {
    *err = "Cannot create directory " + path.string() + " (" + ec.message() + ")";
    return false;
  }
  return true;
}

bool WritePolyMesh(const fs::path& caseDir, vtkUnstructuredGrid* ug, const std::vector<UniqueFace>& faces,
  const std::vector<int>& writeOrder, vtkIdType nInternal, const std::vector<PatchInfo>& patches,
  std::string* err)
{
  const fs::path meshDir = caseDir / "constant" / "polyMesh";
  const fs::path sysDir = caseDir / "system";
  if (!EnsureDir(meshDir, err) || !EnsureDir(sysDir, err))
  {
    return false;
  }

  const vtkIdType nPoints = ug->GetNumberOfPoints();
  const int nFaces = static_cast<int>(writeOrder.size());

  {
    std::ofstream os((meshDir / "points").string());
    if (!os)
    {
      *err = "Cannot write points";
      return false;
    }
    WriteFoamHeader(os, "vectorField", "points", "constant/polyMesh");
    os << nPoints << "\n(\n";
    os << std::setprecision(17);
    for (vtkIdType i = 0; i < nPoints; ++i)
    {
      double p[3];
      ug->GetPoint(i, p);
      os << "(" << p[0] << " " << p[1] << " " << p[2] << ")\n";
    }
    os << ")\n";
  }

  {
    std::ofstream os((meshDir / "faces").string());
    if (!os)
    {
      *err = "Cannot write faces";
      return false;
    }
    WriteFoamHeader(os, "faceList", "faces", "constant/polyMesh");
    os << nFaces << "\n(\n";
    for (int wi : writeOrder)
    {
      const UniqueFace& uf = faces[static_cast<size_t>(wi)];
      std::vector<vtkIdType> pids = uf.PointIds;
      vtkIdType own = uf.Owner;
      vtkIdType nei = uf.Neighbour;
      if (nei >= 0 && own > nei)
      {
        std::swap(own, nei);
        std::reverse(pids.begin(), pids.end());
      }
      os << pids.size() << "(";
      for (size_t i = 0; i < pids.size(); ++i)
      {
        if (i)
        {
          os << " ";
        }
        os << pids[i];
      }
      os << ")\n";
    }
    os << ")\n";
  }

  {
    std::ofstream os((meshDir / "owner").string());
    if (!os)
    {
      *err = "Cannot write owner";
      return false;
    }
    WriteFoamHeader(os, "labelList", "owner", "constant/polyMesh");
    os << nFaces << "\n(\n";
    for (int wi : writeOrder)
    {
      const UniqueFace& uf = faces[static_cast<size_t>(wi)];
      vtkIdType own = uf.Owner;
      vtkIdType nei = uf.Neighbour;
      if (nei >= 0 && own > nei)
      {
        std::swap(own, nei);
      }
      os << own << "\n";
    }
    os << ")\n";
  }

  {
    std::ofstream os((meshDir / "neighbour").string());
    if (!os)
    {
      *err = "Cannot write neighbour";
      return false;
    }
    WriteFoamHeader(os, "labelList", "neighbour", "constant/polyMesh");
    os << nInternal << "\n(\n";
    for (vtkIdType i = 0; i < nInternal; ++i)
    {
      const UniqueFace& uf = faces[static_cast<size_t>(writeOrder[static_cast<size_t>(i)])];
      vtkIdType own = uf.Owner;
      vtkIdType nei = uf.Neighbour;
      if (nei >= 0 && own > nei)
      {
        std::swap(own, nei);
      }
      os << nei << "\n";
    }
    os << ")\n";
  }

  {
    std::ofstream os((meshDir / "boundary").string());
    if (!os)
    {
      *err = "Cannot write boundary";
      return false;
    }
    WriteFoamHeader(os, "polyBoundaryMesh", "boundary", "constant/polyMesh");
    os << patches.size() << "\n(\n";
    vtkIdType start = nInternal;
    for (const PatchInfo& p : patches)
    {
      os << "    " << p.Name << "\n    {\n"
         << "        type            " << p.Type << ";\n"
         << "        nFaces          " << p.FaceIds.size() << ";\n"
         << "        startFace       " << start << ";\n"
         << "    }\n";
      start += static_cast<vtkIdType>(p.FaceIds.size());
    }
    os << ")\n";
  }

  {
    std::ofstream os((sysDir / "controlDict").string());
    if (os)
    {
      WriteFoamHeader(os, "dictionary", "controlDict", "system");
      os << "application     checkMesh;\n"
            "startFrom       startTime;\n"
            "startTime       0;\n"
            "stopAt          endTime;\n"
            "endTime         1;\n"
            "deltaT          1;\n"
            "writeControl    timeStep;\n"
            "writeInterval   1;\n";
    }
  }

  {
    std::ofstream os((caseDir / "case.foam").string());
    if (os)
    {
      os << "// ParaView OpenFOAM reader marker. File -> Open this file.\n";
    }
  }
  return true;
}

vtkIdType CountCells(vtkDataObject* obj)
{
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    return ds->GetNumberOfCells();
  }
  auto* mb = vtkMultiBlockDataSet::SafeDownCast(obj);
  if (!mb)
  {
    return 0;
  }
  vtkIdType n = 0;
  const unsigned int nb = mb->GetNumberOfBlocks();
  for (unsigned int i = 0; i < nb; ++i)
  {
    n += CountCells(mb->GetBlock(i));
  }
  return n;
}

bool ReadCaseWithOpenFOAMReader(
  const std::string& foamFile, vtkMultiBlockDataSet* output, std::string* err)
{
  vtkNew<vtkOpenFOAMReader> reader;
  reader->SetFileName(foamFile.c_str());
  reader->CreateCellToPointOff();
  reader->ListTimeStepsByControlDictOff();
  reader->ReadZonesOn();
  reader->UpdateInformation();
  reader->EnableAllPatchArrays();
  reader->EnableAllCellArrays();
  reader->EnableAllPointArrays();
  reader->Update();
  vtkDataObject* produced = reader->GetOutput();
  auto* mb = vtkMultiBlockDataSet::SafeDownCast(produced);
  if (!mb || mb->GetNumberOfBlocks() == 0 || CountCells(mb) == 0)
  {
    *err = "vtkOpenFOAMReader produced no MultiBlockDataSet from " + foamFile;
    return false;
  }
  output->DeepCopy(mb);
  return true;
}
} // namespace

vtkSHYXPartitionedCollectionToOpenFOAM::vtkSHYXPartitionedCollectionToOpenFOAM()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetDefaultFacesName("defaultFaces");
}

vtkSHYXPartitionedCollectionToOpenFOAM::~vtkSHYXPartitionedCollectionToOpenFOAM()
{
  this->SetCaseDirectory(nullptr);
  this->SetDefaultFacesName(nullptr);
}

void vtkSHYXPartitionedCollectionToOpenFOAM::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CaseDirectory: " << (this->CaseDirectory ? this->CaseDirectory : "(none)")
     << "\n";
  os << indent << "AllowDefaultFaces: " << this->AllowDefaultFaces << "\n";
  os << indent << "DefaultFacesName: "
     << (this->DefaultFacesName ? this->DefaultFacesName : "(none)") << "\n";
}

int vtkSHYXPartitionedCollectionToOpenFOAM::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  return 0;
}

int vtkSHYXPartitionedCollectionToOpenFOAM::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkMultiBlockDataSet");
    return 1;
  }
  return 0;
}

int vtkSHYXPartitionedCollectionToOpenFOAM::RequestDataObject(
  vtkInformation*, vtkInformationVector**, vtkInformationVector* outputVector)
{
  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  if (!vtkMultiBlockDataSet::GetData(outInfo))
  {
    vtkNew<vtkMultiBlockDataSet> output;
    outInfo->Set(vtkDataObject::DATA_OBJECT(), output);
  }
  return 1;
}

int vtkSHYXPartitionedCollectionToOpenFOAM::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPartitionedDataSetCollection* input =
    vtkPartitionedDataSetCollection::GetData(inputVector[0], 0);
  vtkMultiBlockDataSet* output = vtkMultiBlockDataSet::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Missing input vtkPartitionedDataSetCollection or output.");
    return 0;
  }
  output->Initialize();

  if (!this->CaseDirectory || this->CaseDirectory[0] == '\0')
  {
    vtkErrorMacro(<< "Set Case Directory to the OpenFOAM case root to write.");
    return 0;
  }

  PdcLayout layout;
  vtkDataAssembly* assembly = input->GetDataAssembly();
  ParseIossLayout(assembly, &layout);
  if (layout.HasIossAssembly)
  {
    if (layout.ElementBlockCount > 1)
    {
      vtkErrorMacro(<< "Expected exactly one volume block under element_blocks; found "
                    << layout.ElementBlockCount << ".");
      return 0;
    }
  }
  else
  {
    layout = PdcLayout{};
    const unsigned int n = input->GetNumberOfPartitionedDataSets();
    std::vector<unsigned int> volumes;
    for (unsigned int i = 0; i < n; ++i)
    {
      vtkDataSet* ds = GetDataSetFromPdcBlock(input, i);
      if (IsVolumeUnstructuredGrid(ds))
      {
        volumes.push_back(i);
      }
      else if (IsSideSurface(ds))
      {
        layout.SideSetPdcIndices.push_back(i);
      }
    }
    if (volumes.size() == 1)
    {
      layout.HasElementBlock = true;
      layout.ElementBlockPdcIndex = volumes.front();
    }
    else if (volumes.size() > 1)
    {
      vtkErrorMacro(<< "Expected exactly one volume UnstructuredGrid; found " << volumes.size()
                    << ".");
      return 0;
    }
  }

  if (!layout.HasElementBlock)
  {
    vtkErrorMacro(<< "No volume mesh in the Partitioned Data Collection. Connect SHYX DataSet To "
                     "Partitioned Collection after TetGen (IOSS element_blocks). "
                     "SHYX Selection Append Patches has no volume and cannot be written as "
                     "polyMesh.");
    return 0;
  }

  vtkUnstructuredGrid* volume = vtkUnstructuredGrid::SafeDownCast(
    GetDataSetFromPdcBlock(input, layout.ElementBlockPdcIndex));
  if (!IsVolumeUnstructuredGrid(volume))
  {
    vtkErrorMacro(<< "Volume block is empty or not vtkUnstructuredGrid.");
    return 0;
  }

  std::vector<UniqueFace> faces;
  FaceKeyMap keyToIndex;
  std::string err;
  if (!ExplodeVolumeFaces(volume, &faces, &keyToIndex, &err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }

  std::unordered_map<vtkIdType, vtkIdType> cellGidToIndex;
  BuildCellGlobalIdMap(volume, &cellGidToIndex);
  vtkDataArray* volPtG = volume->GetPointData()->GetGlobalIds();

  std::vector<PatchInfo> patches;
  patches.reserve(layout.SideSetPdcIndices.size());

  for (unsigned int sideIdx : layout.SideSetPdcIndices)
  {
    vtkPolyData* side = vtkPolyData::SafeDownCast(GetDataSetFromPdcBlock(input, sideIdx));
    const std::string rawName = ReadBlockNameFromMeta(input, sideIdx, "side");
    if (!side)
    {
      vtkErrorMacro(<< "skipping side \"" << rawName << "\": not vtkPolyData");
      continue;
    }
    vtkDataArray* sidePtG = side->GetPointData() ? side->GetPointData()->GetGlobalIds() : nullptr;
    vtkDataArray* elementSide = side->GetCellData() ? side->GetCellData()->GetArray("element_side") : nullptr;

    PatchInfo patch;
    patch.Name = UniqueFoamName(SanitizeFoamName(rawName), patches);
    patch.Type = InferPatchType(rawName);
    vtkIdType unmatched = 0;
    vtkIdType matched = 0;
    const vtkIdType nCells = side->GetNumberOfCells();
    vtkNew<vtkIdList> cpts;
    for (vtkIdType ci = 0; ci < nCells; ++ci)
    {
      side->GetCellPoints(ci, cpts);
      if (cpts->GetNumberOfIds() < 3)
      {
        continue;
      }
      int faceIdx = -1;
      if (sidePtG)
      {
        std::vector<vtkIdType> gids(static_cast<size_t>(cpts->GetNumberOfIds()));
        bool ok = true;
        for (vtkIdType k = 0; k < cpts->GetNumberOfIds(); ++k)
        {
          gids[static_cast<size_t>(k)] = ArrayIdValue(sidePtG, cpts->GetId(k));
          if (gids[static_cast<size_t>(k)] < 0)
          {
            ok = false;
            break;
          }
        }
        if (ok)
        {
          faceIdx = LookupFaceIndexByGids(keyToIndex, gids);
        }
      }
      if (faceIdx < 0)
      {
        faceIdx = LookupFaceIndexByElementSide(
          volume, volPtG, cellGidToIndex, keyToIndex, elementSide, ci);
      }
      if (faceIdx < 0)
      {
        ++unmatched;
        continue;
      }
      UniqueFace& uf = faces[static_cast<size_t>(faceIdx)];
      if (uf.Neighbour >= 0)
      {
        vtkErrorMacro(<< "Side \"" << rawName << "\" matched an internal volume face.");
        return 0;
      }
      if (uf.PatchIndex >= 0)
      {
        if (uf.PatchIndex == static_cast<int>(patches.size()))
        {
          continue;
        }
        const std::string other = patches[static_cast<size_t>(uf.PatchIndex)].Name;
        vtkErrorMacro(<< "Overlapping sides \"" << other << "\" and \"" << rawName
                      << "\" claim the same volume boundary face.");
        return 0;
      }
      uf.PatchIndex = static_cast<int>(patches.size());
      patch.FaceIds.push_back(faceIdx);
      ++matched;
    }

    if (unmatched > 0)
    {
      vtkErrorMacro(<< "skipping side \"" << rawName << "\": " << unmatched
                    << " faces not on the volume boundary (" << matched << " faces kept)");
    }
    if (patch.FaceIds.empty())
    {
      if (unmatched == 0)
      {
        vtkErrorMacro(<< "skipping side \"" << rawName << "\": no polygonal faces");
      }
      continue;
    }
    patches.push_back(std::move(patch));
  }

  std::vector<int> leftover;
  leftover.reserve(faces.size());
  vtkIdType nInternal = 0;
  for (int i = 0; i < static_cast<int>(faces.size()); ++i)
  {
    const UniqueFace& uf = faces[static_cast<size_t>(i)];
    if (uf.Neighbour >= 0)
    {
      ++nInternal;
      continue;
    }
    if (uf.PatchIndex < 0)
    {
      leftover.push_back(i);
    }
  }

  if (!leftover.empty())
  {
    if (!this->AllowDefaultFaces)
    {
      vtkErrorMacro(<< leftover.size()
                    << " volume boundary faces are not assigned to any side set. Enable Allow "
                       "Default Faces, or add covering side sets. OpenFOAM requires every "
                       "exterior face on exactly one patch.");
      return 0;
    }
    PatchInfo def;
    def.Name = UniqueFoamName(
      SanitizeFoamName(this->DefaultFacesName ? this->DefaultFacesName : "defaultFaces"), patches);
    def.Type = "wall";
    def.FaceIds = leftover;
    for (int fi : leftover)
    {
      faces[static_cast<size_t>(fi)].PatchIndex = static_cast<int>(patches.size());
    }
    vtkWarningMacro(<< leftover.size() << " unassigned boundary faces written as \"" << def.Name
                    << "\".");
    patches.push_back(std::move(def));
  }

  if (patches.empty())
  {
    vtkErrorMacro(<< "No OpenFOAM patches to write (all sides skipped and no defaultFaces).");
    return 0;
  }

  std::vector<int> writeOrder;
  writeOrder.reserve(faces.size());
  for (int i = 0; i < static_cast<int>(faces.size()); ++i)
  {
    if (faces[static_cast<size_t>(i)].Neighbour >= 0)
    {
      writeOrder.push_back(i);
    }
  }
  for (PatchInfo& p : patches)
  {
    for (int fi : p.FaceIds)
    {
      writeOrder.push_back(fi);
    }
  }

  const fs::path caseDir(this->CaseDirectory);
  if (!WritePolyMesh(caseDir, volume, faces, writeOrder, nInternal, patches, &err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }

  const std::string foamFile = (caseDir / "case.foam").string();
  if (!ReadCaseWithOpenFOAMReader(foamFile, output, &err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }
  return 1;
}

VTK_ABI_NAMESPACE_END
