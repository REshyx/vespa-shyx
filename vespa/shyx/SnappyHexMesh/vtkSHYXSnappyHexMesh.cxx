#include "vtkSHYXSnappyHexMesh.h"

#include "shyx_snappy.h"

#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>
#include <vtkAppendPolyData.h>
#include <vtkCell.h>
#include <vtkCellType.h>
#include <vtkCompositeDataIterator.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkOpenFOAMReader.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkSmartPointer.h>
#include <vtkTriangleFilter.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

vtkStandardNewMacro(vtkSHYXSnappyHexMesh);

namespace
{
const void* const kShyxFoamEnvAnchor = reinterpret_cast<const void*>(&shyx_touch_foam_env);

namespace fs = std::filesystem;

void WriteCaseFoam(const fs::path& caseDir)
{
  std::error_code ec;
  if (!fs::exists(caseDir, ec) || !fs::is_directory(caseDir, ec))
  {
    return;
  }
  std::ofstream os(caseDir / "case.foam");
  if (os)
  {
    os << "// ParaView OpenFOAM reader marker. File -> Open this file.\n";
  }
}

bool SameDirectory(const fs::path& a, const fs::path& b)
{
  std::error_code ec;
  if (a.empty() || b.empty())
  {
    return false;
  }
  if (fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec) && !ec)
  {
    return true;
  }
  return fs::absolute(a, ec).lexically_normal() == fs::absolute(b, ec).lexically_normal();
}

bool PathIsOccupied(const fs::path& p)
{
#ifdef _WIN32
  const DWORD attrs = GetFileAttributesW(p.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES)
  {
    return false;
  }
  DWORD flags = FILE_ATTRIBUTE_NORMAL;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY)
  {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }
  HANDLE h = CreateFileW(p.c_str(), DELETE, 0, nullptr, OPEN_EXISTING, flags, nullptr);
  if (h == INVALID_HANDLE_VALUE)
  {
    const DWORD err = GetLastError();
    return err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION;
  }
  CloseHandle(h);
  return false;
#else
  (void)p;
  return false;
#endif
}

bool TreeHasOccupied(const fs::path& root)
{
  std::error_code ec;
  if (!fs::exists(root, ec) || ec)
  {
    return false;
  }
  if (!fs::is_directory(root, ec))
  {
    return PathIsOccupied(root);
  }
  std::error_code itEc;
  const auto opts = fs::directory_options::skip_permission_denied;
  fs::recursive_directory_iterator it(root, opts, itEc);
  if (itEc)
  {
    return true;
  }
  for (; it != fs::recursive_directory_iterator(); it.increment(itEc))
  {
    if (itEc || PathIsOccupied(it->path()))
    {
      return true;
    }
  }
  return PathIsOccupied(root);
}

/** Delete only if nothing in the tree is locked. Otherwise leave the folder intact. */
void RemovePathIfUnoccupied(const fs::path& p)
{
  std::error_code ec;
  if (!fs::exists(p, ec) || ec)
  {
    return;
  }
  if (TreeHasOccupied(p))
  {
    return;
  }
  fs::remove_all(p, ec);
}

void RemoveLegacyLastDir()
{
  std::error_code ec;
  const fs::path last = fs::temp_directory_path(ec) / "shyx-snappy-last";
  if (!ec)
  {
    RemovePathIfUnoccupied(last);
  }
}

/** Plugin load: drop leftover %TEMP%/shyx-snappy-* trees. Occupied trees are left as-is. */
void CleanupAllSnappyTempResidues()
{
  try
  {
    std::error_code ec;
    const fs::path tempRoot = fs::temp_directory_path(ec);
    if (ec || tempRoot.empty())
    {
      return;
    }
    std::vector<fs::path> targets;
    std::error_code itEc;
    const auto opts = fs::directory_options::skip_permission_denied;
    for (fs::directory_iterator it(tempRoot, opts, itEc); it != fs::directory_iterator() && !itEc;
         it.increment(itEc))
    {
      const fs::path p = it->path();
      const std::string name = p.filename().string();
      if (name.rfind("shyx-snappy-", 0) == 0)
      {
        targets.push_back(p);
      }
    }
    for (const fs::path& p : targets)
    {
      RemovePathIfUnoccupied(p);
    }
  }
  catch (...)
  {
  }
}

struct ShyxSnappyTempCleanup
{
  ShyxSnappyTempCleanup() { CleanupAllSnappyTempResidues(); }
};
const ShyxSnappyTempCleanup kShyxSnappyTempCleanup;

/** Drop a previous unique run tree or leftover shyx-snappy-last. Only under %TEMP%. */
void RemoveOwnedCaseTree(const char* caseFoamPath)
{
  if (!caseFoamPath || caseFoamPath[0] == '\0')
  {
    return;
  }
  std::error_code ec;
  const fs::path tempRoot = fs::temp_directory_path(ec);
  if (ec || tempRoot.empty())
  {
    return;
  }
  const fs::path caseDir = fs::absolute(fs::path(caseFoamPath), ec);
  if (ec)
  {
    return;
  }
  if (caseDir.filename() == "shyx-snappy-last" && SameDirectory(caseDir.parent_path(), tempRoot))
  {
    RemovePathIfUnoccupied(caseDir);
    return;
  }
  if (caseDir.filename() != "case")
  {
    return;
  }
  const fs::path runRoot = caseDir.parent_path();
  const std::string runName = runRoot.filename().string();
  if (runName.rfind("shyx-snappy-", 0) == 0 && SameDirectory(runRoot.parent_path(), tempRoot))
  {
    RemovePathIfUnoccupied(runRoot);
  }
}

void WriteRunDiag(const fs::path& caseDir, const std::string& text)
{
  std::error_code ec;
  fs::create_directories(caseDir, ec);
  std::ofstream os(caseDir / "run-diag.txt", std::ios::trunc);
  if (os)
  {
    os << text;
    os.flush();
  }
}

std::string FileSizeOrMissing(const fs::path& p)
{
  std::error_code ec;
  if (!fs::exists(p, ec))
  {
    return "missing";
  }
  const auto n = fs::file_size(p, ec);
  if (ec)
  {
    return "exists";
  }
  return std::to_string(static_cast<unsigned long long>(n)) + " bytes";
}

std::string TailFile(const fs::path& path, std::size_t maxBytes)
{
  std::ifstream is(path, std::ios::binary);
  if (!is)
  {
    return {};
  }
  is.seekg(0, std::ios::end);
  const auto sz = is.tellg();
  if (sz <= 0)
  {
    return {};
  }
  const auto n = std::min<std::streamoff>(static_cast<std::streamoff>(maxBytes), sz);
  is.seekg(-n, std::ios::end);
  std::string s(static_cast<std::size_t>(n), '\0');
  is.read(&s[0], n);
  return s;
}

vtkIdType CountCells(vtkDataObject* root)
{
  if (!root)
  {
    return 0;
  }
  if (auto* ds = vtkDataSet::SafeDownCast(root))
  {
    return ds->GetNumberOfCells();
  }
  auto* cds = vtkCompositeDataSet::SafeDownCast(root);
  if (!cds)
  {
    return 0;
  }
  vtkSmartPointer<vtkCompositeDataIterator> it;
  it.TakeReference(cds->NewIterator());
  it->SkipEmptyNodesOn();
  vtkIdType n = 0;
  for (it->InitTraversal(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    if (auto* ds = vtkDataSet::SafeDownCast(it->GetCurrentDataObject()))
    {
      n += ds->GetNumberOfCells();
    }
  }
  return n;
}

void SetSingleBlockMesh(vtkMultiBlockDataSet* output, vtkUnstructuredGrid* mesh, const char* name)
{
  output->Initialize();
  output->SetNumberOfBlocks(1);
  output->SetBlock(0, mesh);
  if (name)
  {
    output->GetMetaData(static_cast<unsigned int>(0))->Set(vtkCompositeDataSet::NAME(), name);
  }
}

bool ReadCaseWithOpenFOAMReader(
  const std::string& foamFile, vtkMultiBlockDataSet* output, std::string* err)
{
  vtkNew<vtkOpenFOAMReader> reader;
  reader->SetFileName(foamFile.c_str());
  reader->CreateCellToPointOff();
  reader->ListTimeStepsByControlDictOff();
  reader->ReadZonesOn();
  // Patch names are not known until information is updated; enable them all so
  // the output matches File -> Open of case.foam (internalMesh + boundary patches).
  reader->UpdateInformation();
  reader->EnableAllPatchArrays();
  reader->EnableAllCellArrays();
  reader->EnableAllPointArrays();
  reader->Update();
  vtkDataObject* produced = reader->GetOutput();
  auto* mb = vtkMultiBlockDataSet::SafeDownCast(produced);
  if (!mb || mb->GetNumberOfBlocks() == 0 || CountCells(mb) == 0)
  {
    if (err)
    {
      *err = "vtkOpenFOAMReader produced no MultiBlockDataSet from " + foamFile;
    }
    return false;
  }
  output->DeepCopy(mb);
  return true;
}

bool BuildBackgroundHexVtk(vtkUnstructuredGrid* output, double xmin, double ymin, double zmin,
  double xmax, double ymax, double zmax, int nx, int ny, int nz, std::string* err)
{
  if (nx < 1 || ny < 1 || nz < 1)
  {
    if (err)
    {
      *err = "background hex divisions must be >= 1";
    }
    return false;
  }
  const int npx = nx + 1;
  const int npy = ny + 1;
  const int npz = nz + 1;
  const vtkIdType nPoints = static_cast<vtkIdType>(npx) * npy * npz;
  const vtkIdType nCells = static_cast<vtkIdType>(nx) * ny * nz;
  const double dx = (xmax - xmin) / nx;
  const double dy = (ymax - ymin) / ny;
  const double dz = (zmax - zmin) / nz;
  auto pid = [npx, npy](int i, int j, int k) -> vtkIdType {
    return static_cast<vtkIdType>(i) + static_cast<vtkIdType>(npx) * (j + npy * k);
  };

  vtkNew<vtkPoints> pts;
  pts->SetDataTypeToDouble();
  pts->SetNumberOfPoints(nPoints);
  for (int k = 0; k < npz; ++k)
  {
    for (int j = 0; j < npy; ++j)
    {
      for (int i = 0; i < npx; ++i)
      {
        pts->SetPoint(pid(i, j, k), xmin + i * dx, ymin + j * dy, zmin + k * dz);
      }
    }
  }

  output->Initialize();
  output->SetPoints(pts);
  output->Allocate(nCells);
  vtkIdType hex[8];
  for (int k = 0; k < nz; ++k)
  {
    for (int j = 0; j < ny; ++j)
    {
      for (int i = 0; i < nx; ++i)
      {
        hex[0] = pid(i, j, k);
        hex[1] = pid(i + 1, j, k);
        hex[2] = pid(i + 1, j + 1, k);
        hex[3] = pid(i, j + 1, k);
        hex[4] = pid(i, j, k + 1);
        hex[5] = pid(i + 1, j, k + 1);
        hex[6] = pid(i + 1, j + 1, k + 1);
        hex[7] = pid(i, j + 1, k + 1);
        output->InsertNextCell(VTK_HEXAHEDRON, 8, hex);
      }
    }
  }
  return output->GetNumberOfCells() > 0;
}

std::vector<std::string> SplitLines(const char* text)
{
  std::vector<std::string> lines;
  if (!text || text[0] == '\0')
  {
    return lines;
  }
  std::stringstream stream(text);
  std::string line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

int ParseIntOr(const std::vector<std::string>& lines, size_t i, int fallback)
{
  if (i >= lines.size() || lines[i].empty())
  {
    return fallback;
  }
  try
  {
    return std::stoi(lines[i]);
  }
  catch (...)
  {
    return fallback;
  }
}

double ParseDoubleOr(const std::vector<std::string>& lines, size_t i, double fallback)
{
  if (i >= lines.size() || lines[i].empty())
  {
    return fallback;
  }
  try
  {
    return std::stod(lines[i]);
  }
  catch (...)
  {
    return fallback;
  }
}

std::string FoamIdent(const std::string& raw)
{
  std::string s;
  for (unsigned char c : raw)
  {
    if (std::isalnum(c) || c == '_')
    {
      s += static_cast<char>(c);
    }
    else if (!s.empty() && s.back() != '_')
    {
      s += '_';
    }
  }
  while (!s.empty() && s.back() == '_')
  {
    s.pop_back();
  }
  return s;
}

std::string UniqueFoamName(const std::string& raw, int index, std::set<std::string>& used)
{
  std::string s = FoamIdent(raw);
  if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0])))
  {
    s = "part_" + std::to_string(index) + (s.empty() ? std::string() : "_" + s);
  }
  std::string cand = s;
  int n = 2;
  while (used.count(cand))
  {
    cand = s + "_" + std::to_string(n++);
  }
  used.insert(cand);
  return cand;
}

vtkSmartPointer<vtkPolyData> ToTriangulatedSurface(vtkDataObject* obj)
{
  auto* ds = vtkDataSet::SafeDownCast(obj);
  if (!ds || ds->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkPolyData> surface;
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    surface = pd;
  }
  else
  {
    vtkNew<vtkGeometryFilter> geom;
    geom->SetInputData(ds);
    geom->Update();
    surface = geom->GetOutput();
  }
  if (!surface || surface->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(surface);
  tri->Update();
  auto* out = tri->GetOutput();
  if (!out || out->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkNew<vtkPolyData> copy;
  copy->DeepCopy(out);
  return copy;
}

struct MeshPart
{
  std::string raw;
  std::string foam;
  vtkSmartPointer<vtkPolyData> surface;
};

std::string DescribeVtkSnappyCall(vtkDataObject* input, const std::vector<MeshPart>& parts,
  const std::vector<ShyxSnappyGeometry>& geos, const std::vector<std::string>& geoPaths,
  const ShyxSnappyParams& p, const fs::path& caseDir, const char* stlArg)
{
  std::ostringstream os;
  os << "=== vtkSHYXSnappyHexMesh RequestData ===\n";
  os << "ABI=" << SHYX_SNAPPY_PARAMS_ABI
     << " sizeof(ShyxSnappyParams)=" << sizeof(ShyxSnappyParams)
     << " lib_abi=" << shyx_snappy_params_abi() << "\n";
  os << "offset n_locations=" << offsetof(ShyxSnappyParams, n_locations)
     << " locations=" << offsetof(ShyxSnappyParams, locations)
     << " n_geometries=" << offsetof(ShyxSnappyParams, n_geometries)
     << " geometries=" << offsetof(ShyxSnappyParams, geometries) << "\n";
  os << "input=" << (input ? input->GetClassName() : "(null)") << "\n";
  os << "case_dir=" << caseDir.string() << "\n";
  os << "stl_path arg=" << (stlArg && stlArg[0] ? stlArg : "(null)") << "\n";
  os << "n_parts=" << parts.size() << " n_geos=" << geos.size() << "\n";
  for (size_t i = 0; i < parts.size(); ++i)
  {
    vtkPolyData* s = parts[i].surface;
    os << "  part[" << i << "] raw=" << parts[i].raw << " foam=" << parts[i].foam
       << " cells=" << (s ? s->GetNumberOfCells() : 0)
       << " pts=" << (s ? s->GetNumberOfPoints() : 0) << "\n";
  }
  const size_t nShow = geos.size() > 16 ? 16 : geos.size();
  for (size_t i = 0; i < nShow; ++i)
  {
    const fs::path stl = i < geoPaths.size() ? fs::path(geoPaths[i]) : fs::path();
    os << "  geo[" << i << "] name=" << (geos[i].name ? geos[i].name : "(null)")
       << " stl=" << (geos[i].stl_path ? geos[i].stl_path : "(null)")
       << " file=" << FileSizeOrMissing(stl) << "\n";
  }
  os << "p.n_geometries=" << p.n_geometries
     << " p.geometries=" << static_cast<const void*>(p.geometries) << "\n";
  os << "p.n_ref_surfaces=" << p.n_ref_surfaces << " p.n_ref_regions=" << p.n_ref_regions
     << " p.n_layer_patches=" << p.n_layer_patches << " p.n_locations=" << p.n_locations
     << "\n";
  os << "castellated=" << p.castellated << " snap=" << p.snap << " add_layers=" << p.add_layers
     << "\n";
  return os.str();
}

vtkSmartPointer<vtkPolyData> MergePds(vtkPartitionedDataSet* pds)
{
  if (!pds)
  {
    return nullptr;
  }
  vtkNew<vtkAppendPolyData> append;
  int n = 0;
  for (unsigned int p = 0; p < pds->GetNumberOfPartitions(); ++p)
  {
    vtkSmartPointer<vtkPolyData> tri = ToTriangulatedSurface(pds->GetPartition(p));
    if (!tri)
    {
      continue;
    }
    append->AddInputData(tri);
    ++n;
  }
  if (n == 0)
  {
    return nullptr;
  }
  append->Update();
  vtkNew<vtkPolyData> copy;
  copy->DeepCopy(append->GetOutput());
  return copy;
}

bool CollectParts(vtkDataObject* input, std::vector<MeshPart>& parts, std::string* err)
{
  parts.clear();
  std::set<std::string> used;
  if (auto* pdc = vtkPartitionedDataSetCollection::SafeDownCast(input))
  {
    const unsigned int n = pdc->GetNumberOfPartitionedDataSets();
    for (unsigned int i = 0; i < n; ++i)
    {
      std::string raw = "part_" + std::to_string(static_cast<int>(i));
      if (vtkInformation* meta = pdc->GetMetaData(i))
      {
        if (const char* name = meta->Get(vtkCompositeDataSet::NAME()))
        {
          if (name[0] != '\0')
          {
            raw = name;
          }
        }
      }
      vtkSmartPointer<vtkPolyData> surf = MergePds(pdc->GetPartitionedDataSet(i));
      if (!surf)
      {
        continue;
      }
      MeshPart part;
      part.raw = raw;
      part.foam = UniqueFoamName(raw, static_cast<int>(i), used);
      part.surface = surf;
      parts.push_back(part);
    }
  }
  else if (auto* ds = vtkDataSet::SafeDownCast(input))
  {
    vtkSmartPointer<vtkPolyData> surf = ToTriangulatedSurface(ds);
    if (surf)
    {
      MeshPart part;
      part.raw = "geometry";
      part.foam = UniqueFoamName("geometry", 0, used);
      part.surface = surf;
      parts.push_back(part);
    }
  }
  if (parts.empty())
  {
    if (err)
    {
      *err = "Input has no triangulated surface partitions.";
    }
    return false;
  }
  return true;
}

int FindPartIndex(const std::vector<MeshPart>& parts, const std::string& name)
{
  const std::string ident = FoamIdent(name);
  for (int i = 0; i < static_cast<int>(parts.size()); ++i)
  {
    if (parts[static_cast<size_t>(i)].raw == name || parts[static_cast<size_t>(i)].foam == name ||
      FoamIdent(parts[static_cast<size_t>(i)].raw) == ident ||
      parts[static_cast<size_t>(i)].foam == ident)
    {
      return i;
    }
  }
  return -1;
}

bool WriteBinaryStl(vtkPolyData* surface, const std::string& path, std::string* err)
{
  vtkNew<vtkSTLWriter> stl;
  stl->SetFileName(path.c_str());
  stl->SetInputData(surface);
  stl->SetFileTypeToBinary();
  if (stl->Write() == 0)
  {
    if (err)
    {
      *err = "Failed to write STL: " + path;
    }
    return false;
  }
  return true;
}

bool WriteFeatureEdgeMesh(vtkPolyData* pd, const std::string& path, std::string* err)
{
  if (!pd || !pd->GetPoints())
  {
    if (err)
    {
      *err = "Feature edges polydata is empty.";
    }
    return false;
  }
  std::map<vtkIdType, int> remap;
  std::vector<vtkIdType> used;
  auto addPt = [&](vtkIdType id) {
    auto found = remap.find(id);
    if (found != remap.end())
    {
      return found->second;
    }
    const int n = static_cast<int>(used.size());
    remap[id] = n;
    used.push_back(id);
    return n;
  };
  std::vector<std::pair<int, int>> edges;
  for (vtkIdType c = 0; c < pd->GetNumberOfCells(); ++c)
  {
    vtkCell* cell = pd->GetCell(c);
    if (!cell)
    {
      continue;
    }
    const int t = cell->GetCellType();
    if (t != VTK_LINE && t != VTK_POLY_LINE)
    {
      continue;
    }
    vtkIdList* ids = cell->GetPointIds();
    for (int i = 0; i + 1 < ids->GetNumberOfIds(); ++i)
    {
      const int a = addPt(ids->GetId(i));
      const int b = addPt(ids->GetId(i + 1));
      if (a != b)
      {
        edges.emplace_back(a, b);
      }
    }
  }
  if (edges.empty())
  {
    if (err)
    {
      *err = "Feature edges contain no VTK_LINE / VTK_POLY_LINE cells.";
    }
    return false;
  }
  std::ofstream os(path);
  if (!os)
  {
    if (err)
    {
      *err = "Cannot write " + path;
    }
    return false;
  }
  os << "FoamFile\n{\n    version     2.0;\n    format      ascii;\n"
        "    class       featureEdgeMesh;\n    location    \"constant/triSurface\";\n"
        "    object      features;\n}\n";
  os << used.size() << "\n(\n";
  vtkPoints* pts = pd->GetPoints();
  for (vtkIdType id : used)
  {
    double x[3] = { 0, 0, 0 };
    pts->GetPoint(id, x);
    os << "(" << x[0] << " " << x[1] << " " << x[2] << ")\n";
  }
  os << ")\n" << edges.size() << "\n(\n";
  for (const auto& e : edges)
  {
    os << "(" << e.first << " " << e.second << ")\n";
  }
  os << ")\n";
  return true;
}

void UnionBounds(const std::vector<MeshPart>& parts, double bb[6])
{
  bool first = true;
  for (const MeshPart& part : parts)
  {
    double one[6];
    part.surface->GetBounds(one);
    if (first)
    {
      for (int i = 0; i < 6; ++i)
      {
        bb[i] = one[i];
      }
      first = false;
      continue;
    }
    bb[0] = std::min(bb[0], one[0]);
    bb[1] = std::max(bb[1], one[1]);
    bb[2] = std::min(bb[2], one[2]);
    bb[3] = std::max(bb[3], one[3]);
    bb[4] = std::min(bb[4], one[4]);
    bb[5] = std::max(bb[5], one[5]);
  }
}

} // namespace

vtkSHYXSnappyHexMesh::vtkSHYXSnappyHexMesh()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
}

vtkSHYXSnappyHexMesh::~vtkSHYXSnappyHexMesh()
{
  RemoveOwnedCaseTree(this->CaseFoamPath);
  this->SetCaseFoamPathNoModified(nullptr);
  this->SetSurfaceNames(nullptr);
  this->SetSurfaceLevelMin(nullptr);
  this->SetSurfaceLevelMax(nullptr);
  this->SetSurfacePatchTypes(nullptr);
  this->SetRegionNames(nullptr);
  this->SetRegionModes(nullptr);
  this->SetRegionLevels(nullptr);
  this->SetRegionDistances(nullptr);
  this->SetLayerNames(nullptr);
  this->SetLayerNSurfaceLayers(nullptr);
}

void vtkSHYXSnappyHexMesh::SetFeatureEdgesConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

void vtkSHYXSnappyHexMesh::SetCaseFoamPathNoModified(const char* msg)
{
  if ((this->CaseFoamPath == nullptr && (msg == nullptr || msg[0] == '\0')) ||
    (this->CaseFoamPath && msg && std::strcmp(this->CaseFoamPath, msg) == 0))
  {
    return;
  }
  delete[] this->CaseFoamPath;
  this->CaseFoamPath = nullptr;
  if (msg && msg[0] != '\0')
  {
    const size_t n = std::strlen(msg) + 1;
    this->CaseFoamPath = new char[n];
    std::memcpy(this->CaseFoamPath, msg, n);
  }
}

void vtkSHYXSnappyHexMesh::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CastellatedMesh: " << (this->CastellatedMesh ? "ON" : "OFF") << "\n";
  os << indent << "Snap: " << (this->Snap ? "ON" : "OFF") << "\n";
  os << indent << "AddLayers: " << (this->AddLayers ? "ON" : "OFF") << "\n";
  os << indent << "BackgroundCellSize: " << this->BackgroundCellSize << "\n";
  os << indent << "NumberOfInsidePoints: " << this->GetNumberOfInsidePoints() << "\n";
  os << indent << "CaseFoamPath: " << (this->CaseFoamPath ? this->CaseFoamPath : "(none)") << "\n";
}

int vtkSHYXSnappyHexMesh::GetNumberOfInsidePoints() const
{
  return static_cast<int>(this->InsidePoints.size() / 3);
}

void vtkSHYXSnappyHexMesh::SetNumberOfInsidePoints(int n)
{
  if (n < 0)
  {
    n = 0;
  }
  const size_t want = static_cast<size_t>(n) * 3;
  if (this->InsidePoints.size() == want)
  {
    return;
  }
  this->InsidePoints.resize(want, 0.0);
  this->Modified();
}

void vtkSHYXSnappyHexMesh::SetInsidePoint(int i, double x, double y, double z)
{
  if (i < 0)
  {
    return;
  }
  if (i >= this->GetNumberOfInsidePoints())
  {
    this->SetNumberOfInsidePoints(i + 1);
  }
  double* p = &this->InsidePoints[static_cast<size_t>(i) * 3];
  if (p[0] == x && p[1] == y && p[2] == z)
  {
    return;
  }
  p[0] = x;
  p[1] = y;
  p[2] = z;
  this->Modified();
}

void vtkSHYXSnappyHexMesh::SetInsidePoint(int i, const double xyz[3])
{
  this->SetInsidePoint(i, xyz[0], xyz[1], xyz[2]);
}

double* vtkSHYXSnappyHexMesh::GetInsidePoint(int i)
{
  if (i < 0 || i >= this->GetNumberOfInsidePoints())
  {
    return nullptr;
  }
  return &this->InsidePoints[static_cast<size_t>(i) * 3];
}

void vtkSHYXSnappyHexMesh::GetInsidePoint(int i, double xyz[3])
{
  const double* p = this->GetInsidePoint(i);
  if (!p)
  {
    xyz[0] = xyz[1] = xyz[2] = 0.0;
    return;
  }
  xyz[0] = p[0];
  xyz[1] = p[1];
  xyz[2] = p[2];
}

int vtkSHYXSnappyHexMesh::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPartitionedDataSetCollection");
    info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
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

int vtkSHYXSnappyHexMesh::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkMultiBlockDataSet");
    return 1;
  }
  return 0;
}

int vtkSHYXSnappyHexMesh::RequestDataObject(
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

int vtkSHYXSnappyHexMesh::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  const std::string previousCase =
    (this->CaseFoamPath && this->CaseFoamPath[0] != '\0') ? std::string(this->CaseFoamPath)
                                                         : std::string();
  vtkDataObject* input = vtkDataObject::GetData(inputVector[0]);
  vtkPolyData* featureEdges = vtkPolyData::GetData(inputVector[1]);
  vtkMultiBlockDataSet* output = vtkMultiBlockDataSet::GetData(outputVector);
  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }

  std::string partErr;
  std::vector<MeshPart> parts;
  if (!CollectParts(input, parts, &partErr))
  {
    vtkErrorMacro(<< partErr);
    return 0;
  }

  auto backgroundDivisions = [&](const double bb[6], int& nx, int& ny, int& nz, double& cell,
                               double bOut[6]) {
    const double dx0 = bb[1] - bb[0];
    const double dy0 = bb[3] - bb[2];
    const double dz0 = bb[5] - bb[4];
    const double m = this->BoundsMargin;
    bOut[0] = bb[0] - m * dx0;
    bOut[1] = bb[1] + m * dx0;
    bOut[2] = bb[2] - m * dy0;
    bOut[3] = bb[3] + m * dy0;
    bOut[4] = bb[4] - m * dz0;
    bOut[5] = bb[5] + m * dz0;
    const double lx = bOut[1] - bOut[0];
    const double ly = bOut[3] - bOut[2];
    const double lz = bOut[5] - bOut[4];
    cell = (this->BackgroundCellSize > 0.0) ? this->BackgroundCellSize
                                            : std::max({ lx, ly, lz }) / 16.0;
    auto nDiv = [&](double len) {
      const double n = len / cell + 0.5;
      if (n > 100000.0)
      {
        return 100000;
      }
      return std::max(1, static_cast<int>(n));
    };
    nx = nDiv(lx);
    ny = nDiv(ly);
    nz = nDiv(lz);
  };

  double unionBb[6] = { 0, 0, 0, 0, 0, 0 };
  UnionBounds(parts, unionBb);

  if (!this->CastellatedMesh)
  {
    RemoveOwnedCaseTree(previousCase.c_str());
    RemoveLegacyLastDir();
    this->SetCaseFoamPathNoModified(nullptr);
    if (this->Snap || this->AddLayers)
    {
      vtkWarningMacro("Snap and Add layers need Castellated mesh; generating "
                      "background hex only.");
    }
    int nx = 1, ny = 1, nz = 1;
    double cell = 0.0;
    double box[6];
    backgroundDivisions(unionBb, nx, ny, nz, cell, box);
    const long long nBg = static_cast<long long>(nx) * ny * nz;
    const long long cap = std::max<long long>(1000, this->MaxGlobalCells);
    vtkWarningMacro(<< "Background hex " << nx << " x " << ny << " x " << nz << " (" << nBg
                    << " cells), cell size=" << cell);
    if (nBg > cap)
    {
      vtkErrorMacro(<< "background hex would have " << nBg << " cells (" << nx << "x" << ny << "x"
                    << nz << "); increase Background cell size (current " << cell
                    << ") or Max global cells");
      return 0;
    }
    std::string hexErr;
    vtkNew<vtkUnstructuredGrid> hex;
    if (!BuildBackgroundHexVtk(
          hex, box[0], box[2], box[4], box[1], box[3], box[5], nx, ny, nz, &hexErr))
    {
      vtkErrorMacro(<< "Failed to build background hex: " << hexErr);
      return 0;
    }
    SetSingleBlockMesh(output, hex, "internalMesh");
    return 1;
  }

  std::error_code ec;
  const fs::path tmpRoot = fs::temp_directory_path(ec) /
    ("shyx-snappy-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "-" +
      std::to_string(this->GetMTime()));
  if (ec)
  {
    vtkErrorMacro(<< "Cannot resolve temp directory: " << ec.message());
    return 0;
  }
  const fs::path caseDirPath = tmpRoot / "case";
  const fs::path triDir = caseDirPath / "constant" / "triSurface";
  fs::create_directories(triDir, ec);
  if (ec)
  {
    vtkErrorMacro(<< "Cannot create case directories: " << ec.message());
    return 0;
  }
  RemoveOwnedCaseTree(previousCase.c_str());
  RemoveLegacyLastDir();
  this->SetCaseFoamPathNoModified(caseDirPath.string().c_str());

  std::vector<ShyxSnappyGeometry> geos;
  std::vector<std::string> geoNames;
  std::vector<std::string> geoPaths;
  geoNames.reserve(parts.size());
  geoPaths.reserve(parts.size());
  for (const MeshPart& part : parts)
  {
    const std::string stlPath = (triDir / (part.foam + ".stl")).string();
    std::string stlErr;
    if (!WriteBinaryStl(part.surface, stlPath, &stlErr))
    {
      vtkErrorMacro(<< stlErr);
      return 0;
    }
    geoNames.push_back(part.foam);
    geoPaths.push_back(stlPath);
  }
  geos.resize(geoNames.size());
  for (size_t i = 0; i < geoNames.size(); ++i)
  {
    geos[i].name = geoNames[i].c_str();
    geos[i].stl_path = geoPaths[i].c_str();
  }

  auto surfNames = SplitLines(this->SurfaceNames);
  auto surfMin = SplitLines(this->SurfaceLevelMin);
  auto surfMax = SplitLines(this->SurfaceLevelMax);
  auto surfType = SplitLines(this->SurfacePatchTypes);
  std::vector<ShyxSnappyRefinementSurface> refSurfs;
  std::vector<std::string> refNames;
  std::vector<std::string> refTypes;
  size_t nSurfRows = surfNames.size();
  bool anySurf = false;
  for (const auto& n : surfNames)
  {
    if (!n.empty())
    {
      anySurf = true;
      break;
    }
  }
  if (!anySurf)
  {
    for (const MeshPart& part : parts)
    {
      refNames.push_back(part.foam);
      refTypes.emplace_back("wall");
    }
    refSurfs.resize(refNames.size());
    for (size_t i = 0; i < refNames.size(); ++i)
    {
      refSurfs[i].name = refNames[i].c_str();
      refSurfs[i].level_min = this->RefinementMin;
      refSurfs[i].level_max = this->RefinementMax;
      refSurfs[i].patch_type = refTypes[i].c_str();
    }
  }
  else
  {
    for (size_t i = 0; i < nSurfRows; ++i)
    {
      if (surfNames[i].empty())
      {
        continue;
      }
      const int idx = FindPartIndex(parts, surfNames[i]);
      if (idx < 0)
      {
        vtkWarningMacro(<< "refinementSurfaces patch '" << surfNames[i]
                        << "' is not an input partition; skipped.");
        continue;
      }
      refNames.push_back(parts[static_cast<size_t>(idx)].foam);
      std::string ptype = (i < surfType.size() && !surfType[i].empty()) ? surfType[i] : "wall";
      if (ptype != "patch")
      {
        ptype = "wall";
      }
      refTypes.push_back(ptype);
      ShyxSnappyRefinementSurface row;
      row.name = nullptr;
      row.level_min = ParseIntOr(surfMin, i, this->RefinementMin);
      row.level_max = ParseIntOr(surfMax, i, this->RefinementMax);
      row.patch_type = nullptr;
      refSurfs.push_back(row);
    }
    for (size_t i = 0; i < refSurfs.size(); ++i)
    {
      refSurfs[i].name = refNames[i].c_str();
      refSurfs[i].patch_type = refTypes[i].c_str();
    }
  }

  auto regNamesIn = SplitLines(this->RegionNames);
  auto regModesIn = SplitLines(this->RegionModes);
  auto regLevelsIn = SplitLines(this->RegionLevels);
  auto regDistIn = SplitLines(this->RegionDistances);
  std::vector<ShyxSnappyRefinementRegion> refRegs;
  std::vector<std::string> regNames;
  std::vector<std::string> regModes;
  for (size_t i = 0; i < regNamesIn.size(); ++i)
  {
    if (regNamesIn[i].empty())
    {
      continue;
    }
    const int idx = FindPartIndex(parts, regNamesIn[i]);
    if (idx < 0)
    {
      vtkWarningMacro(<< "refinementRegions patch '" << regNamesIn[i]
                      << "' is not an input partition; skipped.");
      continue;
    }
    std::string mode = (i < regModesIn.size() && !regModesIn[i].empty()) ? regModesIn[i] : "inside";
    if (mode != "outside" && mode != "distance")
    {
      mode = "inside";
    }
    regNames.push_back(parts[static_cast<size_t>(idx)].foam);
    regModes.push_back(mode);
    ShyxSnappyRefinementRegion row;
    row.name = nullptr;
    row.mode = nullptr;
    row.level = ParseIntOr(regLevelsIn, i, this->RefinementMax);
    row.distance = ParseDoubleOr(regDistIn, i, 0.0);
    refRegs.push_back(row);
  }
  for (size_t i = 0; i < refRegs.size(); ++i)
  {
    refRegs[i].name = regNames[i].c_str();
    refRegs[i].mode = regModes[i].c_str();
  }

  auto layerNamesIn = SplitLines(this->LayerNames);
  auto layerNsIn = SplitLines(this->LayerNSurfaceLayers);
  std::vector<ShyxSnappyLayerPatch> layerRows;
  std::vector<std::string> layerNames;
  bool anyLayer = false;
  for (const auto& n : layerNamesIn)
  {
    if (!n.empty())
    {
      anyLayer = true;
      break;
    }
  }
  if (!anyLayer)
  {
    for (const auto& s : refNames)
    {
      layerNames.push_back(s);
    }
    layerRows.resize(layerNames.size());
    for (size_t i = 0; i < layerNames.size(); ++i)
    {
      layerRows[i].name = layerNames[i].c_str();
      layerRows[i].n_surface_layers = this->NSurfaceLayers;
    }
  }
  else
  {
    for (size_t i = 0; i < layerNamesIn.size(); ++i)
    {
      if (layerNamesIn[i].empty())
      {
        continue;
      }
      const int idx = FindPartIndex(parts, layerNamesIn[i]);
      if (idx < 0)
      {
        vtkWarningMacro(<< "layers patch '" << layerNamesIn[i]
                        << "' is not an input partition; skipped.");
        continue;
      }
      layerNames.push_back(parts[static_cast<size_t>(idx)].foam);
      ShyxSnappyLayerPatch row;
      row.name = nullptr;
      row.n_surface_layers = ParseIntOr(layerNsIn, i, this->NSurfaceLayers);
      layerRows.push_back(row);
    }
    for (size_t i = 0; i < layerRows.size(); ++i)
    {
      layerRows[i].name = layerNames[i].c_str();
    }
  }

  std::string emeshPath;
  if (featureEdges && featureEdges->GetNumberOfCells() > 0)
  {
    emeshPath = (triDir / "features.eMesh").string();
    std::string emeshErr;
    if (!WriteFeatureEdgeMesh(featureEdges, emeshPath, &emeshErr))
    {
      vtkErrorMacro(<< emeshErr);
      return 0;
    }
  }

  ShyxSnappyParams p;
  shyx_snappy_params_default(&p);
  p.castellated = this->CastellatedMesh ? 1 : 0;
  p.snap = this->Snap ? 1 : 0;
  p.add_layers = this->AddLayers ? 1 : 0;
  p.background_cell_size = this->BackgroundCellSize;
  p.bounds_margin = this->BoundsMargin;
  p.max_global_cells = this->MaxGlobalCells;
  p.n_cells_between_levels = this->NCellsBetweenLevels;
  p.refinement_min = this->RefinementMin;
  p.refinement_max = this->RefinementMax;
  p.n_smooth_patch = this->NSmoothPatch;
  p.snap_tolerance = this->SnapTolerance;
  p.n_solve_iter = this->NSolveIter;
  p.n_relax_iter = this->NRelaxIter;
  p.n_surface_layers = this->NSurfaceLayers;
  p.expansion_ratio = this->ExpansionRatio;
  p.final_layer_thickness = this->FinalLayerThickness;
  p.min_thickness = this->MinThickness;
  p.feature_angle = this->FeatureAngle;
  p.implicit_feature_snap = this->ImplicitFeatureSnap ? 1 : 0;
  p.n_locations = this->GetNumberOfInsidePoints();
  p.locations = p.n_locations > 0 ? this->InsidePoints.data() : nullptr;
  if (p.n_locations == 1)
  {
    p.location_specified = 1;
    p.location_in_mesh[0] = this->InsidePoints[0];
    p.location_in_mesh[1] = this->InsidePoints[1];
    p.location_in_mesh[2] = this->InsidePoints[2];
  }
  p.n_geometries = static_cast<int>(geos.size());
  p.geometries = geos.data();
  p.n_ref_surfaces = static_cast<int>(refSurfs.size());
  p.ref_surfaces = refSurfs.empty() ? nullptr : refSurfs.data();
  p.n_ref_regions = static_cast<int>(refRegs.size());
  p.ref_regions = refRegs.empty() ? nullptr : refRegs.data();
  p.n_layer_patches = static_cast<int>(layerRows.size());
  p.layer_patches = layerRows.empty() ? nullptr : layerRows.data();
  p.emesh_path = emeshPath.empty() ? nullptr : emeshPath.c_str();
  p.feature_level = this->FeatureLevel;

  char err[2048];
  err[0] = '\0';
  const std::string caseDir = caseDirPath.string();
  const char* stlArg = nullptr;
  const std::string vtkDiag =
    DescribeVtkSnappyCall(input, parts, geos, geoPaths, p, caseDirPath, stlArg);
  WriteRunDiag(caseDirPath, vtkDiag);
  const int rc = shyx_snappy_run(stlArg, caseDir.c_str(), &p, err, 2048);
  WriteCaseFoam(caseDirPath);
  const fs::path diagPath = caseDirPath / "run-diag.txt";
  const fs::path foamPath = caseDirPath / "case.foam";
  const fs::path caseLog = caseDirPath / "snappyHexMesh.log";
  if (rc != 0 && rc != 5)
  {
    std::error_code existEc;
    const bool haveMeshLog = fs::exists(caseLog, existEc);
    std::ostringstream msg;
    msg << "snappyHexMesh failed (" << rc << "): " << err << "\ndiag: " << diagPath.string()
        << "\ncase: " << foamPath.string();
    const std::string errStr = err;
    const std::string diagText = TailFile(diagPath, 3500);
    if (errStr == "null argument" || diagText.find("shyx_snappy_run (lib)") == std::string::npos)
    {
      msg << "\nSTALE SHYXSnappyHex.lib: plugin was rebuilt but shyx_snappyhex_ep was not. "
             "Rebuild target shyx_snappyhex_ep then VESPAPlugin.";
    }
    if (haveMeshLog)
    {
      const std::string tail = TailFile(caseLog, 1200);
      if (!tail.empty())
      {
        msg << "\n--- snappyHexMesh.log tail ---\n" << tail;
      }
    }
    else
    {
      msg << "\n(no snappyHexMesh.log this run; OpenFOAM did not start)\n--- run-diag ---\n"
          << diagText;
    }
    vtkErrorMacro(<< msg.str());
    return 0;
  }
  if (rc == 5)
  {
    vtkWarningMacro(<< err);
  }

  std::string parseErr;
  if (!ReadCaseWithOpenFOAMReader(foamPath.string(), output, &parseErr))
  {
    vtkErrorMacro(<< parseErr << " (case.foam: " << foamPath.string()
                  << "; diag: " << diagPath.string() << ")");
    return 0;
  }
  return 1;
}
