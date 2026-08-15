#include "vtkSHYXSnappyHexMesh.h"

#include "shyx_snappy.h"

#include <vtkCellType.h>
#include <vtkCompositeDataIterator.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkOpenFOAMReader.h>
#include <vtkSmartPointer.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkTriangleFilter.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

vtkStandardNewMacro(vtkSHYXSnappyHexMesh);

namespace
{
namespace fs = std::filesystem;

struct RemoveTree
{
  fs::path p;
  bool keep = false;
  ~RemoveTree()
  {
    if (keep)
    {
      return;
    }
    std::error_code ec;
    fs::remove_all(p, ec);
  }
};

fs::path LastLogDir()
{
  std::error_code ec;
  return fs::temp_directory_path(ec) / "shyx-snappy-last";
}

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

void CopyTreeOverwrite(const fs::path& from, const fs::path& to)
{
  std::error_code ec;
  if (!fs::exists(from, ec))
  {
    return;
  }
  if (fs::is_directory(from, ec))
  {
    fs::remove_all(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return;
  }
  fs::create_directories(to.parent_path(), ec);
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

void PersistSnappyCase(const fs::path& caseDir)
{
  WriteCaseFoam(caseDir);
  const fs::path dstDir = LastLogDir();
  std::error_code ec;
  fs::create_directories(dstDir, ec);
  for (const char* name : { "system", "constant", "case.foam", "snappyHexMesh.log" })
  {
    CopyTreeOverwrite(caseDir / name, dstDir / name);
  }
  WriteCaseFoam(dstDir);
  std::ofstream ptr(dstDir / "last-case-path.txt");
  if (ptr)
  {
    ptr << caseDir.string() << "\n";
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


vtkUnstructuredGrid* FindInternalMesh(vtkDataObject* root)
{
  if (!root)
  {
    return nullptr;
  }
  vtkUnstructuredGrid* named = nullptr;
  vtkUnstructuredGrid* largest = nullptr;
  auto consider = [&](vtkUnstructuredGrid* ug, const char* name) {
    if (!ug)
    {
      return;
    }
    if (name && std::string(name) == "internalMesh")
    {
      named = ug;
    }
    if (!largest || ug->GetNumberOfCells() > largest->GetNumberOfCells())
    {
      largest = ug;
    }
  };
  if (auto* ug = vtkUnstructuredGrid::SafeDownCast(root))
  {
    return ug;
  }
  auto* cds = vtkCompositeDataSet::SafeDownCast(root);
  if (!cds)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkCompositeDataIterator> it;
  it.TakeReference(cds->NewIterator());
  it->SkipEmptyNodesOn();
  for (it->InitTraversal(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    const char* name = nullptr;
    if (it->HasCurrentMetaData())
    {
      name = it->GetCurrentMetaData()->Get(vtkCompositeDataSet::NAME());
    }
    consider(vtkUnstructuredGrid::SafeDownCast(it->GetCurrentDataObject()), name);
  }
  return named ? named : largest;
}

bool ReadCaseWithOpenFOAMReader(const std::string& foamFile, vtkUnstructuredGrid* output, std::string* err)
{
  vtkNew<vtkOpenFOAMReader> reader;
  reader->SetFileName(foamFile.c_str());
  reader->CreateCellToPointOff();
  reader->ListTimeStepsByControlDictOff();
  reader->ReadZonesOn();
  reader->EnableAllPatchArrays();
  reader->EnableAllCellArrays();
  reader->EnableAllPointArrays();
  reader->Update();
  vtkUnstructuredGrid* mesh = FindInternalMesh(reader->GetOutput());
  if (!mesh || mesh->GetNumberOfCells() == 0)
  {
    if (err)
    {
      *err = "vtkOpenFOAMReader produced no internalMesh from " + foamFile;
    }
    return false;
  }
  output->DeepCopy(mesh);
  return true;
}

/** Cartesian hex grid from an AABB. VTK_HEXAHEDRON, no OpenFOAM round-trip. */
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

} // namespace

vtkSHYXSnappyHexMesh::vtkSHYXSnappyHexMesh()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

void vtkSHYXSnappyHexMesh::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CastellatedMesh: " << (this->CastellatedMesh ? "ON" : "OFF") << "\n";
  os << indent << "Snap: " << (this->Snap ? "ON" : "OFF") << "\n";
  os << indent << "AddLayers: " << (this->AddLayers ? "ON" : "OFF") << "\n";
  os << indent << "BackgroundCellSize: " << this->BackgroundCellSize << "\n";
}

int vtkSHYXSnappyHexMesh::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXSnappyHexMesh::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkUnstructuredGrid");
    return 1;
  }
  return 0;
}

int vtkSHYXSnappyHexMesh::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
  vtkUnstructuredGrid* output = vtkUnstructuredGrid::GetData(outputVector);
  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }
  if (input->GetNumberOfPoints() == 0 || input->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Input mesh is empty.");
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

  // Snap/layers without castellated: OpenFOAM still builds intersections then
  // abort()s (no cellLevel). Treat as background-only; Snap stays checked in
  // the UI when Castellated is turned off.
  if (!this->CastellatedMesh)
  {
    if (this->Snap || this->AddLayers)
    {
      vtkWarningMacro("Snap and Add layers need Castellated mesh; generating "
                      "background hex only.");
    }
    double bb[6];
    input->GetBounds(bb);
    int nx = 1, ny = 1, nz = 1;
    double cell = 0.0;
    double box[6];
    backgroundDivisions(bb, nx, ny, nz, cell, box);
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
    if (!BuildBackgroundHexVtk(
          output, box[0], box[2], box[4], box[1], box[3], box[5], nx, ny, nz, &hexErr))
    {
      vtkErrorMacro(<< "Failed to build background hex: " << hexErr);
      return 0;
    }
    return 1;
  }

  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(input);
  tri->Update();
  vtkPolyData* surface = tri->GetOutput();
  if (!surface || surface->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Triangulated surface is empty.");
    return 0;
  }

  // OpenFOAM still needs a triSurface file; write it under %TEMP% so ParaView's
  // cwd (often Program Files) does not have to be writable.
  std::error_code ec;
  const fs::path tmpRoot = fs::temp_directory_path(ec) /
    ("shyx-snappy-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "-" +
      std::to_string(this->GetMTime()));
  if (ec)
  {
    vtkErrorMacro(<< "Cannot resolve temp directory: " << ec.message());
    return 0;
  }
  fs::create_directories(tmpRoot, ec);
  if (ec)
  {
    vtkErrorMacro(<< "Cannot create temp directory " << tmpRoot.string() << ": " << ec.message());
    return 0;
  }
  RemoveTree cleanup{ tmpRoot, /*keep=*/true };

  const std::string stlPath = (tmpRoot / "geometry.stl").string();
  const std::string caseDir = (tmpRoot / "case").string();

  vtkNew<vtkSTLWriter> stl;
  stl->SetFileName(stlPath.c_str());
  stl->SetInputData(surface);
  stl->SetFileTypeToBinary();
  if (stl->Write() == 0)
  {
    vtkErrorMacro(<< "Failed to write temporary STL: " << stlPath);
    return 0;
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

  char err[2048];
  err[0] = '\0';
  const int rc = shyx_snappy_run(stlPath.c_str(), caseDir.c_str(), &p, err, 2048);
  PersistSnappyCase(caseDir);
  const fs::path logPath = LastLogDir() / "snappyHexMesh.log";
  const fs::path foamPath = fs::path(caseDir) / "case.foam";
  const fs::path lastFoam = LastLogDir() / "case.foam";
  const fs::path caseLog = fs::path(caseDir) / "snappyHexMesh.log";
  if (rc != 0 && rc != 5)
  {
    const std::string tail = TailFile(caseLog, 1200);
    vtkErrorMacro(<< "snappyHexMesh failed (" << rc << "): " << err
                  << "\nlog: " << logPath.string()
                  << "\ncase.foam: " << foamPath.string()
                  << "\nlast: " << lastFoam.string()
                  << (tail.empty() ? "" : ("\n--- log tail ---\n" + tail)));
    return 0;
  }
  if (rc == 5)
  {
    vtkWarningMacro(<< err);
  }

  std::string parseErr;
  if (!ReadCaseWithOpenFOAMReader(foamPath.string(), output, &parseErr))
  {
    vtkErrorMacro(<< parseErr << " (case.foam: " << foamPath.string() << "; last: " << lastFoam.string()
                  << "; log: " << logPath.string() << ")");
    return 0;
  }
  vtkWarningMacro(<< "OpenFOAM case kept. File -> Open: " << lastFoam.string()
                  << " (this run: " << foamPath.string() << ")");
  return 1;
}
