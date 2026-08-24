#include "vtkSHYXSnappyHexMesh.h"

#include "shyx_snappy.h"

#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>
#include <vtkAppendPolyData.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellArrayIterator.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkCompositeDataIterator.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkOpenFOAMReader.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkStringArray.h>
#include <vtkSmartPointer.h>
#include <vtkTriangleFilter.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
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

bool CaseDirectoryEmpty(const char* p)
{
  if (!p || p[0] == '\0')
  {
    return true;
  }
  for (const char* c = p; *c; ++c)
  {
    if (!std::isspace(static_cast<unsigned char>(*c)))
    {
      return false;
    }
  }
  return true;
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

bool WriteExtendedFeatureBlob(vtkPolyData* pd, const std::string& path, std::string* err)
{
  if (!pd || !pd->GetFieldData())
  {
    return false;
  }
  auto* blob = vtkStringArray::SafeDownCast(
    pd->GetFieldData()->GetAbstractArray("FoamExtendedFeatureEdgeMesh"));
  if (!blob || blob->GetNumberOfValues() < 1)
  {
    return false;
  }
  const std::string ascii = blob->GetValue(0);
  if (ascii.empty())
  {
    return false;
  }
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream os(path);
  if (!os)
  {
    if (err)
    {
      *err = "Cannot write " + path;
    }
    return false;
  }
  os << ascii;
  return static_cast<bool>(os);
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
  vtkCellArray* lineCells = pd->GetLines();
  if (lineCells && lineCells->GetNumberOfCells() > 0)
  {
    vtkSmartPointer<vtkCellArrayIterator> it = vtk::TakeSmartPointer(lineCells->NewIterator());
    for (it->GoToFirstCell(); !it->IsDoneWithTraversal(); it->GoToNextCell())
    {
      vtkIdType nPts = 0;
      const vtkIdType* ids = nullptr;
      it->GetCurrentCell(nPts, ids);
      if (!ids || nPts < 2)
      {
        continue;
      }
      for (vtkIdType i = 0; i + 1 < nPts; ++i)
      {
        const int a = addPt(ids[i]);
        const int b = addPt(ids[i + 1]);
        if (a != b)
        {
          edges.emplace_back(a, b);
        }
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

const char* Nz(const char* s)
{
  return (s && s[0] != '\0') ? s : "";
}

bool HasPolyMesh(const fs::path& caseDir)
{
  std::error_code ec;
  const fs::path meshDir = caseDir / "constant" / "polyMesh";
  return fs::exists(meshDir / "faces", ec) && fs::exists(meshDir / "boundary", ec);
}

std::vector<std::string> ParseBlockNames(const char* names)
{
  return SplitLines(names);
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
    result.push_back(row);
  }
  return result;
}

size_t CountBoundaryVariables(const std::vector<std::vector<double>>& values)
{
  if (values.empty())
  {
    return 0;
  }
  size_t nVariables = 1;
  for (const auto& row : values)
  {
    nVariables = std::max(nVariables, row.size());
  }
  return nVariables;
}

std::vector<double> ResolveLineDoubles(
  const std::vector<std::vector<double>>& values, size_t rowIndex, size_t nVariables)
{
  std::vector<double> result(nVariables, std::numeric_limits<double>::quiet_NaN());
  if (rowIndex >= values.size())
  {
    return result;
  }
  for (size_t i = 0; i < nVariables && i < values[rowIndex].size(); ++i)
  {
    result[i] = values[rowIndex][i];
  }
  return result;
}

std::string LowerCopy(std::string s)
{
  for (char& c : s)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool IsInternalMeshName(const std::string& name)
{
  const std::string lower = LowerCopy(name);
  return lower == "internalmesh" || lower.find("internalmesh") != std::string::npos;
}

double FiniteOrZero(double v)
{
  return vtkMath::IsFinite(v) ? v : 0.0;
}

std::string FoamFieldFileName(const std::string& sanitized)
{
  if (sanitized.rfind("shyx_", 0) == 0)
  {
    return sanitized;
  }
  return "shyx_" + sanitized;
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

std::string StripFoamComments(std::string text)
{
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();)
  {
    if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/')
    {
      i += 2;
      while (i < text.size() && text[i] != '\n')
      {
        ++i;
      }
      continue;
    }
    if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*')
    {
      i += 2;
      while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
      {
        ++i;
      }
      i = std::min(text.size(), i + 2);
      continue;
    }
    out.push_back(text[i++]);
  }
  return out;
}

std::vector<std::string> ParsePolyMeshBoundaryNames(const fs::path& boundaryFile)
{
  std::vector<std::string> names;
  std::ifstream is(boundaryFile.string());
  if (!is)
  {
    return names;
  }
  std::ostringstream buf;
  buf << is.rdbuf();
  const std::string text = StripFoamComments(buf.str());

  size_t pos = 0;
  const size_t foam = text.find("FoamFile");
  if (foam != std::string::npos)
  {
    const size_t brace = text.find('{', foam);
    if (brace != std::string::npos)
    {
      int depth = 1;
      pos = brace + 1;
      while (pos < text.size() && depth > 0)
      {
        if (text[pos] == '{')
        {
          ++depth;
        }
        else if (text[pos] == '}')
        {
          --depth;
        }
        ++pos;
      }
    }
  }

  const size_t open = text.find('(', pos);
  if (open == std::string::npos)
  {
    return names;
  }
  pos = open + 1;
  int depth = 1;
  while (pos < text.size() && depth > 0)
  {
    const char c = text[pos];
    if (c == '(')
    {
      ++depth;
      ++pos;
      continue;
    }
    if (c == ')')
    {
      --depth;
      ++pos;
      continue;
    }
    if (depth == 1 && (std::isalpha(static_cast<unsigned char>(c)) || c == '_'))
    {
      const size_t start = pos;
      while (pos < text.size() &&
        (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_'))
      {
        ++pos;
      }
      const std::string ident = text.substr(start, pos - start);
      while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
      {
        ++pos;
      }
      if (pos < text.size() && text[pos] == '{')
      {
        names.push_back(ident);
        int block = 1;
        ++pos;
        while (pos < text.size() && block > 0)
        {
          if (text[pos] == '{')
          {
            ++block;
          }
          else if (text[pos] == '}')
          {
            --block;
          }
          ++pos;
        }
        continue;
      }
      continue;
    }
    ++pos;
  }
  return names;
}

void RemoveStaleShyxVariableFiles(const fs::path& zeroDir)
{
  std::error_code ec;
  if (!fs::exists(zeroDir, ec) || !fs::is_directory(zeroDir, ec))
  {
    return;
  }
  std::vector<fs::path> stale;
  for (fs::directory_iterator it(zeroDir, ec); it != fs::directory_iterator() && !ec; it.increment(ec))
  {
    const fs::path p = it->path();
    const std::string name = p.filename().string();
    if (name.rfind("shyx_BoundaryVariable", 0) == 0)
    {
      stale.push_back(p);
    }
  }
  for (const fs::path& p : stale)
  {
    fs::remove(p, ec);
  }
}

int FindBlockRow(const std::vector<std::string>& names, const std::string& want)
{
  for (size_t i = 0; i < names.size(); ++i)
  {
    if (names[i] == want)
    {
      return static_cast<int>(i);
    }
  }
  const std::string wantLower = LowerCopy(want);
  for (size_t i = 0; i < names.size(); ++i)
  {
    if (LowerCopy(names[i]) == wantLower)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int FindInternalMeshRow(const std::vector<std::string>& names)
{
  for (size_t i = 0; i < names.size(); ++i)
  {
    if (IsInternalMeshName(names[i]))
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool WriteCustomVolFields(const fs::path& caseDir, const char* blockNames, const char* variables,
  std::string* err)
{
  const std::vector<std::string> names = ParseBlockNames(blockNames);
  const std::vector<std::vector<double>> matrix = ParseLineDoubleMatrix(variables);
  const size_t nVars = CountBoundaryVariables(matrix);
  const fs::path zeroDir = caseDir / "0";

  if (names.empty() || nVars == 0)
  {
    RemoveStaleShyxVariableFiles(zeroDir);
    return true;
  }

  std::error_code ec;
  fs::create_directories(zeroDir, ec);
  if (ec)
  {
    if (err)
    {
      *err = "Cannot create 0/: " + ec.message();
    }
    return false;
  }
  RemoveStaleShyxVariableFiles(zeroDir);

  std::vector<std::string> patches = ParsePolyMeshBoundaryNames(caseDir / "constant" / "polyMesh" / "boundary");
  if (patches.empty())
  {
    for (const std::string& n : names)
    {
      if (!IsInternalMeshName(n) && !n.empty())
      {
        patches.push_back(n);
      }
    }
  }

  const int internalRow = FindInternalMeshRow(names);
  for (size_t v = 0; v < nVars; ++v)
  {
    const std::string arrayName = "BoundaryVariable" + std::to_string(v + 1);
    const std::string foamName = FoamFieldFileName(FoamIdent(arrayName));
    const fs::path filePath = zeroDir / foamName;
    std::ofstream os(filePath.string());
    if (!os)
    {
      if (err)
      {
        *err = "Cannot write 0/" + foamName;
      }
      return false;
    }
    WriteFoamHeader(os, "volScalarField", foamName.c_str(), "0");
    os << "dimensions      [0 0 0 0 0 0 0];\n\n";
    double internalVal = 0.0;
    if (internalRow >= 0)
    {
      const std::vector<double> row =
        ResolveLineDoubles(matrix, static_cast<size_t>(internalRow), nVars);
      internalVal = FiniteOrZero(row[v]);
    }
    os << std::setprecision(17);
    os << "internalField   uniform " << internalVal << ";\n\nboundaryField\n{\n";
    for (const std::string& patch : patches)
    {
      double val = 0.0;
      const int rowIndex = FindBlockRow(names, patch);
      if (rowIndex >= 0)
      {
        const std::vector<double> row =
          ResolveLineDoubles(matrix, static_cast<size_t>(rowIndex), nVars);
        val = FiniteOrZero(row[v]);
      }
      os << "    " << patch << "\n"
            "    {\n"
            "        type            calculated;\n"
            "        value           uniform "
         << val << ";\n"
            "    }\n";
    }
    os << "}\n";
  }
  return true;
}

void AttachUniformCellArrays(vtkDataSet* ds, const char* blockNames, const char* variables)
{
  if (!ds)
  {
    return;
  }
  const std::vector<std::string> names = ParseBlockNames(blockNames);
  const std::vector<std::vector<double>> matrix = ParseLineDoubleMatrix(variables);
  const size_t nVars = CountBoundaryVariables(matrix);
  if (nVars == 0)
  {
    return;
  }
  int rowIndex = FindInternalMeshRow(names);
  if (rowIndex < 0 && !names.empty())
  {
    rowIndex = 0;
  }
  const std::vector<double> row = ResolveLineDoubles(
    matrix, rowIndex >= 0 ? static_cast<size_t>(rowIndex) : 0, nVars);
  const vtkIdType nCells = ds->GetNumberOfCells();
  for (size_t v = 0; v < nVars; ++v)
  {
    const std::string arrayName = FoamFieldFileName("BoundaryVariable" + std::to_string(v + 1));
    vtkNew<vtkDoubleArray> arr;
    arr->SetName(arrayName.c_str());
    arr->SetNumberOfComponents(1);
    arr->SetNumberOfTuples(nCells);
    arr->Fill(FiniteOrZero(row[v]));
    ds->GetCellData()->RemoveArray(arrayName.c_str());
    ds->GetCellData()->AddArray(arr);
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
  this->SetCaseDirectory(nullptr);
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
  this->SetBlockNames(nullptr);
  this->SetBoundaryVariables(nullptr);
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
  os << indent << "CaseDirectory: " << (this->CaseDirectory ? this->CaseDirectory : "(none)")
     << "\n";
  os << indent << "CaseFoamPath: " << (this->CaseFoamPath ? this->CaseFoamPath : "(none)") << "\n";
  os << indent << "BlockNames: " << (this->BlockNames ? this->BlockNames : "(none)") << "\n";
  os << indent << "BoundaryVariables: "
     << (this->BoundaryVariables ? this->BoundaryVariables : "(none)") << "\n";
}

std::string vtkSHYXSnappyHexMesh::ComputeMeshFingerprint(
  vtkMTimeType inputMTime, vtkMTimeType featureMTime) const
{
  std::ostringstream os;
  os << inputMTime << '|' << featureMTime << '|' << (this->CastellatedMesh ? 1 : 0) << '|'
     << (this->Snap ? 1 : 0) << '|' << (this->AddLayers ? 1 : 0) << '|'
     << this->BackgroundCellSize << '|' << this->BoundsMargin << '|' << this->MaxGlobalCells << '|'
     << this->NCellsBetweenLevels << '|' << this->RefinementMin << '|' << this->RefinementMax << '|'
     << this->NSmoothPatch << '|' << this->SnapTolerance << '|' << this->NSolveIter << '|'
     << this->NRelaxIter << '|' << this->NSurfaceLayers << '|' << this->ExpansionRatio << '|'
     << this->FinalLayerThickness << '|' << this->MinThickness << '|' << this->FeatureAngle << '|'
     << (this->ImplicitFeatureSnap ? 1 : 0) << '|' << this->FeatureLevel << '|'
     << Nz(this->CaseDirectory) << '|' << Nz(this->SurfaceNames) << '|' << Nz(this->SurfaceLevelMin)
     << '|' << Nz(this->SurfaceLevelMax) << '|' << Nz(this->SurfacePatchTypes) << '|'
     << Nz(this->RegionNames) << '|' << Nz(this->RegionModes) << '|' << Nz(this->RegionLevels)
     << '|' << Nz(this->RegionDistances) << '|' << Nz(this->LayerNames) << '|'
     << Nz(this->LayerNSurfaceLayers) << '|';
  os << this->InsidePoints.size();
  for (double v : this->InsidePoints)
  {
    os << ',' << v;
  }
  return os.str();
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

  const vtkMTimeType featureMTime = featureEdges ? featureEdges->GetMTime() : vtkMTimeType{ 0 };
  const std::string fingerprint = this->ComputeMeshFingerprint(input->GetMTime(), featureMTime);

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
    this->LastMeshFingerprint.clear();
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
    AttachUniformCellArrays(hex, this->BlockNames, this->BoundaryVariables);
    SetSingleBlockMesh(output, hex, "internalMesh");
    return 1;
  }

  const bool canSkipRemesh = !previousCase.empty() && HasPolyMesh(fs::path(previousCase)) &&
    !this->LastMeshFingerprint.empty() && fingerprint == this->LastMeshFingerprint;
  if (canSkipRemesh)
  {
    std::string fieldErr;
    if (!WriteCustomVolFields(
          fs::path(previousCase), this->BlockNames, this->BoundaryVariables, &fieldErr))
    {
      vtkWarningMacro(<< fieldErr << " Reusing previous mesh failed; remeshing.");
    }
    else
    {
      std::string parseErr;
      const fs::path foamPath = fs::path(previousCase) / "case.foam";
      if (ReadCaseWithOpenFOAMReader(foamPath.string(), output, &parseErr))
      {
        return 1;
      }
      vtkWarningMacro(<< parseErr << " Reusing previous mesh failed; remeshing.");
    }
    this->LastMeshFingerprint.clear();
  }

  std::error_code ec;
  fs::path caseDirPath;
  if (CaseDirectoryEmpty(this->CaseDirectory))
  {
    const fs::path tmpRoot = fs::temp_directory_path(ec) /
      ("shyx-snappy-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "-" +
        std::to_string(this->GetMTime()));
    if (ec)
    {
      vtkErrorMacro(<< "Cannot resolve temp directory: " << ec.message());
      return 0;
    }
    caseDirPath = tmpRoot / "case";
  }
  else
  {
    caseDirPath = fs::path(this->CaseDirectory);
  }
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
  int emeshExtended = 0;
  if (featureEdges && featureEdges->GetNumberOfCells() > 0)
  {
    std::string emeshErr;
    const fs::path extPath = triDir / "features.extendedFeatureEdgeMesh";
    if (WriteExtendedFeatureBlob(featureEdges, extPath.string(), &emeshErr))
    {
      emeshPath = extPath.string();
      emeshExtended = 1;
    }
    else
    {
      emeshPath = (triDir / "features.eMesh").string();
      if (!WriteFeatureEdgeMesh(featureEdges, emeshPath, &emeshErr))
      {
        vtkWarningMacro(<< emeshErr
                        << " Feature edges ignored; meshing without explicit snap. "
                           "Pick VTK Feature Edges or SHYX Extended Feature Edge Mesh "
                           "(line cells), not a surface/PDC.");
        emeshPath.clear();
      }
      else
      {
        emeshExtended = 0;
      }
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
  p.emesh_is_extended = emeshExtended;

  char err[2048];
  err[0] = '\0';
  const std::string caseDir = caseDirPath.string();
  const char* stlArg = nullptr;
  const int rc = shyx_snappy_run(stlArg, caseDir.c_str(), &p, err, 2048);
  WriteCaseFoam(caseDirPath);
  const fs::path foamPath = caseDirPath / "case.foam";
  const fs::path caseLog = caseDirPath / "snappyHexMesh.log";
  if (rc != 0 && rc != 5)
  {
    std::error_code existEc;
    const bool haveMeshLog = fs::exists(caseLog, existEc);
    std::ostringstream msg;
    msg << "snappyHexMesh failed (" << rc << "): " << err << "\ncase: " << foamPath.string();
    if (haveMeshLog)
    {
      const std::string tail = TailFile(caseLog, 1200);
      if (!tail.empty())
      {
        msg << "\n--- snappyHexMesh.log tail ---\n" << tail;
      }
    }
    vtkErrorMacro(<< msg.str());
    return 0;
  }
  if (rc == 5)
  {
    vtkWarningMacro(<< err);
  }

  std::string fieldErr;
  if (!WriteCustomVolFields(caseDirPath, this->BlockNames, this->BoundaryVariables, &fieldErr))
  {
    vtkErrorMacro(<< fieldErr);
    return 0;
  }

  std::string parseErr;
  if (!ReadCaseWithOpenFOAMReader(foamPath.string(), output, &parseErr))
  {
    vtkErrorMacro(<< parseErr << " (case.foam: " << foamPath.string() << ")");
    return 0;
  }
  this->LastMeshFingerprint = fingerprint;
  return 1;
}
