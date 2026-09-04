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
#include <vtkDataArray.h>
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
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkStringArray.h>
#include <vtkSmartPointer.h>
#include <vtkTriangleFilter.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
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
constexpr const char* kBoundaryRadialValueArrayName = "BoundaryRadialValue";
constexpr const char* kBoundaryRadialNormalArrayName = "BoundaryRadialValueNormal";

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
    if (name.rfind("shyx_BoundaryVariable", 0) == 0 || name == FoamFieldFileName(kBoundaryRadialValueArrayName) ||
      name == FoamFieldFileName(kBoundaryRadialNormalArrayName))
    {
      stale.push_back(p);
    }
  }
  for (const fs::path& p : stale)
  {
    fs::remove(p, ec);
  }
}

void RemoveStaleShyxRadialFiles(const fs::path& zeroDir)
{
  std::error_code ec;
  if (!fs::exists(zeroDir, ec) || !fs::is_directory(zeroDir, ec))
  {
    return;
  }
  const std::string radialName = FoamFieldFileName(kBoundaryRadialValueArrayName);
  const std::string normalName = FoamFieldFileName(kBoundaryRadialNormalArrayName);
  fs::remove(zeroDir / radialName, ec);
  fs::remove(zeroDir / normalName, ec);
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

bool ResolveWriteNormal(const std::vector<int>& flags, size_t rowIndex)
{
  return rowIndex < flags.size() && flags[rowIndex] != 0;
}

bool AnyWriteNormal(const std::vector<int>& flags)
{
  for (int flag : flags)
  {
    if (flag != 0)
    {
      return true;
    }
  }
  return false;
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

bool ComputeAverageCellNormal(vtkDataSet* ds, double normal[3])
{
  normal[0] = normal[1] = normal[2] = 0.0;
  if (!ds || ds->GetNumberOfPoints() == 0)
  {
    return false;
  }

  vtkNew<vtkIdList> cpts;
  double p0[3], p1[3], p2[3], e1[3], e2[3], n[3];
  for (vtkIdType cid = 0; cid < ds->GetNumberOfCells(); ++cid)
  {
    ds->GetCellPoints(cid, cpts);
    if (cpts->GetNumberOfIds() < 3)
    {
      continue;
    }
    ds->GetPoint(cpts->GetId(0), p0);
    for (vtkIdType k = 1; k + 1 < cpts->GetNumberOfIds(); ++k)
    {
      ds->GetPoint(cpts->GetId(k), p1);
      ds->GetPoint(cpts->GetId(k + 1), p2);
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

  auto assignEmpty = [&]() {
    pd->GetPointData()->RemoveArray(arrayName);
    pd->GetPointData()->AddArray(values);
    pd->GetCellData()->RemoveArray(arrayName);
    pd->GetCellData()->AddArray(cellValues);
  };

  if (nPts == 0 || nCells == 0 || !pd->GetPoints())
  {
    assignEmpty();
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
    assignEmpty();
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
    assignEmpty();
    return;
  }
  center[0] /= static_cast<double>(nBoundaryPts);
  center[1] /= static_cast<double>(nBoundaryPts);
  center[2] /= static_cast<double>(nBoundaryPts);

  double normal[3];
  if (!ComputeAverageCellNormal(pd, normal))
  {
    assignEmpty();
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

void CollectNamedBlocks(
  vtkDataObject* obj, const std::string& name, std::vector<std::pair<std::string, vtkDataSet*>>& out)
{
  if (!obj)
  {
    return;
  }
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    if (!name.empty() && name != "Root")
    {
      out.push_back({ name, ds });
    }
    return;
  }
  auto* mb = vtkMultiBlockDataSet::SafeDownCast(obj);
  if (!mb)
  {
    return;
  }
  const unsigned int n = mb->GetNumberOfBlocks();
  for (unsigned int i = 0; i < n; ++i)
  {
    std::string childName = name;
    if (vtkInformation* meta = mb->GetMetaData(i))
    {
      if (const char* raw = meta->Get(vtkCompositeDataSet::NAME()))
      {
        childName = raw;
      }
    }
    CollectNamedBlocks(mb->GetBlock(i), childName, out);
  }
}

vtkDataSet* FindNamedBlock(
  const std::vector<std::pair<std::string, vtkDataSet*>>& blocks, const std::string& want)
{
  for (const auto& b : blocks)
  {
    if (b.first == want)
    {
      return b.second;
    }
  }
  const std::string wantLower = LowerCopy(want);
  for (const auto& b : blocks)
  {
    if (LowerCopy(b.first) == wantLower)
    {
      return b.second;
    }
  }
  return nullptr;
}

void RemoveNamedArrays(vtkDataSet* ds, const char* name)
{
  if (!ds || !name || name[0] == '\0')
  {
    return;
  }
  ds->GetCellData()->RemoveArray(name);
  ds->GetPointData()->RemoveArray(name);
}

void AttachZeroCellArray(vtkDataSet* ds, const char* name, int ncomp)
{
  if (!ds || !name || name[0] == '\0' || ncomp < 1)
  {
    return;
  }
  vtkNew<vtkDoubleArray> arr;
  arr->SetName(name);
  arr->SetNumberOfComponents(ncomp);
  arr->SetNumberOfTuples(ds->GetNumberOfCells());
  arr->Fill(0.0);
  ds->GetCellData()->RemoveArray(name);
  ds->GetCellData()->AddArray(arr);
}

void WriteComponentTuple(std::ostream& os, const double* v, int ncomp)
{
  os << std::setprecision(17);
  if (ncomp == 1)
  {
    os << v[0];
    return;
  }
  os << "(";
  for (int c = 0; c < ncomp; ++c)
  {
    if (c)
    {
      os << " ";
    }
    os << v[c];
  }
  os << ")";
}

struct PatchFieldBlock
{
  bool Uniform = true;
  std::vector<double> Data;
};

bool WriteCalculatedVolField(const fs::path& zeroDir, const std::string& foamName, const char* cls,
  const char* listType, int ncomp, const double* internalUniform,
  const std::vector<std::string>& patches, const std::vector<PatchFieldBlock>& patchFields,
  std::string* err)
{
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
  WriteFoamHeader(os, cls, foamName.c_str(), "0");
  os << "dimensions      [0 0 0 0 0 0 0];\n\n";
  os << std::setprecision(17);
  os << "internalField   uniform ";
  WriteComponentTuple(os, internalUniform, ncomp);
  os << ";\n\nboundaryField\n{\n";
  for (size_t i = 0; i < patches.size(); ++i)
  {
    const PatchFieldBlock& block =
      i < patchFields.size() ? patchFields[i] : PatchFieldBlock{};
    os << "    " << patches[i] << "\n    {\n        type            calculated;\n";
    if (block.Uniform || block.Data.size() < static_cast<size_t>(ncomp))
    {
      double zero[9] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
      const double* v = block.Data.size() >= static_cast<size_t>(ncomp) ? block.Data.data() : zero;
      os << "        value           uniform ";
      WriteComponentTuple(os, v, ncomp);
      os << ";\n";
    }
    else
    {
      const vtkIdType nTuples = static_cast<vtkIdType>(block.Data.size() / static_cast<size_t>(ncomp));
      os << "        value           nonuniform List<" << listType << ">\n" << nTuples << "\n(\n";
      for (vtkIdType t = 0; t < nTuples; ++t)
      {
        WriteComponentTuple(os, &block.Data[static_cast<size_t>(t) * static_cast<size_t>(ncomp)], ncomp);
        os << "\n";
      }
      os << ");\n";
    }
    os << "    }\n";
  }
  os << "}\n";
  return true;
}

PatchFieldBlock MakePatchFieldFromArray(vtkDataArray* arr, int ncomp, bool forceUniformZero)
{
  PatchFieldBlock block;
  if (forceUniformZero || !arr || arr->GetNumberOfComponents() < ncomp || arr->GetNumberOfTuples() == 0)
  {
    block.Uniform = true;
    block.Data.assign(static_cast<size_t>(ncomp), 0.0);
    return block;
  }
  const vtkIdType n = arr->GetNumberOfTuples();
  if (n == 1)
  {
    block.Uniform = true;
    block.Data.resize(static_cast<size_t>(ncomp));
    for (int c = 0; c < ncomp; ++c)
    {
      block.Data[static_cast<size_t>(c)] = FiniteOrZero(arr->GetComponent(0, c));
    }
    return block;
  }
  bool allSame = true;
  double first[9] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
  for (int c = 0; c < ncomp; ++c)
  {
    first[c] = FiniteOrZero(arr->GetComponent(0, c));
  }
  block.Data.resize(static_cast<size_t>(n) * static_cast<size_t>(ncomp));
  for (vtkIdType t = 0; t < n; ++t)
  {
    for (int c = 0; c < ncomp; ++c)
    {
      const double v = FiniteOrZero(arr->GetComponent(t, c));
      block.Data[static_cast<size_t>(t) * static_cast<size_t>(ncomp) + static_cast<size_t>(c)] = v;
      if (v != first[c])
      {
        allSame = false;
      }
    }
  }
  if (allSame)
  {
    block.Uniform = true;
    block.Data.assign(first, first + ncomp);
  }
  else
  {
    block.Uniform = false;
  }
  return block;
}

bool ApplyBoundaryNormalsAndRadial(vtkMultiBlockDataSet* output, const fs::path& caseDir,
  const char* blockNames, const char* writeNormals, int computeRadial, double exponent,
  std::string* err)
{
  const std::string radialFoam = FoamFieldFileName(kBoundaryRadialValueArrayName);
  const std::string normalFoam = FoamFieldFileName(kBoundaryRadialNormalArrayName);
  const fs::path zeroDir = caseDir / "0";
  const std::vector<std::string> names = ParseBlockNames(blockNames);
  const std::vector<int> flags = ParseLineIntFlags(writeNormals);
  const bool writeAnyNormal = AnyWriteNormal(flags);
  const bool doRadial = computeRadial != 0;

  std::vector<std::pair<std::string, vtkDataSet*>> blocks;
  CollectNamedBlocks(output, std::string(), blocks);

  auto stripArrays = [&]() {
    for (const auto& b : blocks)
    {
      RemoveNamedArrays(b.second, radialFoam.c_str());
      RemoveNamedArrays(b.second, normalFoam.c_str());
    }
  };

  if (!writeAnyNormal && !doRadial)
  {
    stripArrays();
    RemoveStaleShyxRadialFiles(zeroDir);
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
  RemoveStaleShyxRadialFiles(zeroDir);

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

  vtkDataSet* internal = nullptr;
  const int internalRow = FindInternalMeshRow(names);
  if (internalRow >= 0)
  {
    internal = FindNamedBlock(blocks, names[static_cast<size_t>(internalRow)]);
  }
  if (!internal)
  {
    for (const auto& b : blocks)
    {
      if (IsInternalMeshName(b.first) && vtkUnstructuredGrid::SafeDownCast(b.second))
      {
        internal = b.second;
        break;
      }
    }
  }

  for (const auto& b : blocks)
  {
    RemoveNamedArrays(b.second, radialFoam.c_str());
    RemoveNamedArrays(b.second, normalFoam.c_str());
  }

  if (internal)
  {
    if (doRadial)
    {
      AttachZeroCellArray(internal, radialFoam.c_str(), 1);
    }
    if (writeAnyNormal)
    {
      AttachZeroCellArray(internal, normalFoam.c_str(), 3);
    }
  }

  for (const std::string& patch : patches)
  {
    vtkDataSet* ds = FindNamedBlock(blocks, patch);
    if (!ds)
    {
      continue;
    }
    const int rowIndex = FindBlockRow(names, patch);
    const bool writeNormal =
      rowIndex >= 0 && ResolveWriteNormal(flags, static_cast<size_t>(rowIndex));

    vtkPolyData* pd = vtkPolyData::SafeDownCast(ds);
    if (doRadial && pd)
    {
      AddBoundaryRadialValueArray(pd, radialFoam.c_str(), exponent);
    }
    else if (doRadial)
    {
      AttachZeroCellArray(ds, radialFoam.c_str(), 1);
    }

    if (!writeAnyNormal)
    {
      continue;
    }

    double avgN[3] = { 0.0, 0.0, 0.0 };
    const bool haveNormal = writeNormal && ComputeAverageCellNormal(ds, avgN);
    vtkDataArray* cellRadial =
      doRadial ? vtkDataArray::SafeDownCast(ds->GetCellData()->GetArray(radialFoam.c_str())) : nullptr;

    vtkNew<vtkDoubleArray> normals;
    normals->SetName(normalFoam.c_str());
    normals->SetNumberOfComponents(3);
    const vtkIdType nCells = ds->GetNumberOfCells();
    normals->SetNumberOfTuples(nCells);
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!haveNormal)
      {
        normals->SetTuple3(cid, 0.0, 0.0, 0.0);
        continue;
      }
      double scale = 1.0;
      if (doRadial)
      {
        scale = 0.0;
        if (cellRadial && cid < cellRadial->GetNumberOfTuples())
        {
          scale = FiniteOrZero(cellRadial->GetComponent(cid, 0));
        }
      }
      normals->SetTuple3(cid, avgN[0] * scale, avgN[1] * scale, avgN[2] * scale);
    }
    ds->GetCellData()->RemoveArray(normalFoam.c_str());
    ds->GetCellData()->AddArray(normals);
  }

  std::vector<PatchFieldBlock> radialFields;
  std::vector<PatchFieldBlock> normalFields;
  radialFields.reserve(patches.size());
  normalFields.reserve(patches.size());
  for (const std::string& patch : patches)
  {
    vtkDataSet* ds = FindNamedBlock(blocks, patch);
    vtkDataArray* radialArr =
      ds ? vtkDataArray::SafeDownCast(ds->GetCellData()->GetArray(radialFoam.c_str())) : nullptr;
    vtkDataArray* normalArr =
      ds ? vtkDataArray::SafeDownCast(ds->GetCellData()->GetArray(normalFoam.c_str())) : nullptr;
    if (doRadial)
    {
      radialFields.push_back(MakePatchFieldFromArray(radialArr, 1, false));
    }
    if (writeAnyNormal)
    {
      normalFields.push_back(MakePatchFieldFromArray(normalArr, 3, false));
    }
  }

  const double zeroS = 0.0;
  const double zeroV[3] = { 0.0, 0.0, 0.0 };
  if (doRadial &&
    !WriteCalculatedVolField(zeroDir, radialFoam, "volScalarField", "scalar", 1, &zeroS, patches,
      radialFields, err))
  {
    return false;
  }
  if (writeAnyNormal &&
    !WriteCalculatedVolField(zeroDir, normalFoam, "volVectorField", "vector", 3, zeroV, patches,
      normalFields, err))
  {
    return false;
  }
  return true;
}

bool ReportVtkProgress(vtkSHYXSnappyHexMesh* self, double frac, const char* text)
{
  if (!self)
  {
    return false;
  }
  self->SetProgressText(text);
  self->UpdateProgress(frac);
  return self->CheckAbort() != 0;
}

} // namespace

extern "C" int vtkSHYXSnappyProgressCb(double snappyFrac, const char* text, void* user)
{
  auto* self = static_cast<vtkSHYXSnappyHexMesh*>(user);
  const double t = snappyFrac < 0.0 ? 0.0 : (snappyFrac > 1.0 ? 1.0 : snappyFrac);
  return ReportVtkProgress(
           self, 0.08 + 0.84 * t, (text && text[0] != '\0') ? text : "snappyHexMesh")
    ? 1
    : 0;
}

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
  this->SetBoundaryWriteNormals(nullptr);
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
  os << indent << "BoundaryWriteNormals: "
     << (this->BoundaryWriteNormals ? this->BoundaryWriteNormals : "(none)") << "\n";
  os << indent << "ComputeBoundaryRadialValue: " << this->ComputeBoundaryRadialValue << "\n";
  os << indent << "BoundaryRadialNormalFalloffFactor: " << this->BoundaryRadialNormalFalloffFactor
     << "\n";
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

  if (ReportVtkProgress(this, 0.01, "Preparing surfaces"))
  {
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
    if (ReportVtkProgress(this, 0.4, "Building background hex"))
    {
      return 0;
    }
    if (!BuildBackgroundHexVtk(
          hex, box[0], box[2], box[4], box[1], box[3], box[5], nx, ny, nz, &hexErr))
    {
      vtkErrorMacro(<< "Failed to build background hex: " << hexErr);
      return 0;
    }
    AttachUniformCellArrays(hex, this->BlockNames, this->BoundaryVariables);
    SetSingleBlockMesh(output, hex, "internalMesh");
    ReportVtkProgress(this, 1.0, "Done");
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
      if (ReportVtkProgress(this, 0.5, "Reusing previous mesh"))
      {
        return 0;
      }
      if (ReadCaseWithOpenFOAMReader(foamPath.string(), output, &parseErr))
      {
        std::string normalErr;
        if (!ApplyBoundaryNormalsAndRadial(output, fs::path(previousCase), this->BlockNames,
              this->BoundaryWriteNormals, this->ComputeBoundaryRadialValue,
              this->BoundaryRadialNormalFalloffFactor, &normalErr))
        {
          vtkWarningMacro(<< normalErr);
        }
        ReportVtkProgress(this, 1.0, "Done");
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
  for (size_t i = 0; i < parts.size(); ++i)
  {
    const MeshPart& part = parts[i];
    const double frac =
      0.02 + 0.04 * (static_cast<double>(i) / static_cast<double>(std::max<size_t>(1, parts.size())));
    if (ReportVtkProgress(this, frac, "Writing surfaces"))
    {
      return 0;
    }
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
  if (ReportVtkProgress(this, 0.08, "Running snappyHexMesh"))
  {
    return 0;
  }
  const int rc =
    shyx_snappy_run(stlArg, caseDir.c_str(), &p, err, 2048, &vtkSHYXSnappyProgressCb, this);
  WriteCaseFoam(caseDirPath);
  const fs::path foamPath = caseDirPath / "case.foam";
  const fs::path caseLog = caseDirPath / "snappyHexMesh.log";
  if (rc == 6)
  {
    return 0;
  }
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
  if (ReportVtkProgress(this, 0.93, "Writing block variables"))
  {
    return 0;
  }
  if (!WriteCustomVolFields(caseDirPath, this->BlockNames, this->BoundaryVariables, &fieldErr))
  {
    vtkErrorMacro(<< fieldErr);
    return 0;
  }

  std::string parseErr;
  if (ReportVtkProgress(this, 0.96, "Reading mesh"))
  {
    return 0;
  }
  if (!ReadCaseWithOpenFOAMReader(foamPath.string(), output, &parseErr))
  {
    vtkErrorMacro(<< parseErr << " (case.foam: " << foamPath.string() << ")");
    return 0;
  }
  {
    std::string normalErr;
    if (!ApplyBoundaryNormalsAndRadial(output, caseDirPath, this->BlockNames,
          this->BoundaryWriteNormals, this->ComputeBoundaryRadialValue,
          this->BoundaryRadialNormalFalloffFactor, &normalErr))
    {
      vtkWarningMacro(<< normalErr);
    }
  }
  this->LastMeshFingerprint = fingerprint;
  ReportVtkProgress(this, 1.0, "Done");
  return 1;
}
