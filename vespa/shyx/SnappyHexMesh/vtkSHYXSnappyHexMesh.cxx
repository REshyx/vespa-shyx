#include "vtkSHYXSnappyHexMesh.h"

#include "shyx_snappy.h"

#include <vtkCellArray.h>
#include <vtkCellType.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkTriangleFilter.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

vtkStandardNewMacro(vtkSHYXSnappyHexMesh);

namespace
{
bool MkDir(const std::string& p)
{
#ifdef _WIN32
  return _mkdir(p.c_str()) == 0 || errno == EEXIST;
#else
  return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

std::string Join(const std::string& a, const std::string& b)
{
  if (a.empty())
  {
    return b;
  }
  const char sep =
#ifdef _WIN32
    '\\';
#else
    '/';
#endif
  if (a.back() == '/' || a.back() == '\\')
  {
    return a + b;
  }
  return a + sep + b;
}

/** Skip Foam comments and whitespace. */
bool NextToken(std::istream& is, std::string& tok)
{
  tok.clear();
  for (;;)
  {
    const int c = is.peek();
    if (c == EOF)
    {
      return false;
    }
    if (std::isspace(c))
    {
      is.get();
      continue;
    }
    if (c == '/')
    {
      is.get();
      const int n = is.peek();
      if (n == '/')
      {
        std::string dummy;
        std::getline(is, dummy);
        continue;
      }
      if (n == '*')
      {
        is.get();
        int prev = 0;
        int cur = 0;
        while ((cur = is.get()) != EOF)
        {
          if (prev == '*' && cur == '/')
          {
            break;
          }
          prev = cur;
        }
        continue;
      }
      tok.push_back('/');
    }
    break;
  }
  for (;;)
  {
    const int c = is.peek();
    if (c == EOF || std::isspace(c) || c == '(' || c == ')' || c == '{' || c == '}')
    {
      if (tok.empty() && (c == '(' || c == ')' || c == '{' || c == '}'))
      {
        tok.push_back(static_cast<char>(is.get()));
        return true;
      }
      break;
    }
    tok.push_back(static_cast<char>(is.get()));
  }
  return !tok.empty();
}

bool ReadFoamLabelListFile(const std::string& path, std::vector<int>& out, std::string* err)
{
  std::ifstream is(path);
  if (!is)
  {
    if (err)
    {
      *err = "cannot open " + path;
    }
    return false;
  }
  std::string tok;
  int n = -1;
  while (NextToken(is, tok))
  {
    if (tok == "(" && n >= 0)
    {
      out.resize(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i)
      {
        if (!NextToken(is, tok))
        {
          return false;
        }
        out[static_cast<size_t>(i)] = std::stoi(tok);
      }
      return true;
    }
    // first integer after FoamFile block is the count
    if (!tok.empty() && (std::isdigit(tok[0]) || tok[0] == '-'))
    {
      try
      {
        n = std::stoi(tok);
      }
      catch (...)
      {
        n = -1;
      }
    }
  }
  if (err)
  {
    *err = "failed to parse " + path;
  }
  return false;
}

bool ReadFoamPoints(const std::string& path, vtkPoints* pts, std::string* err)
{
  std::ifstream is(path);
  if (!is)
  {
    if (err)
    {
      *err = "cannot open " + path;
    }
    return false;
  }
  std::string tok;
  int n = -1;
  while (NextToken(is, tok))
  {
    if (tok == "(" && n >= 0)
    {
      pts->SetNumberOfPoints(n);
      for (int i = 0; i < n; ++i)
      {
        if (!NextToken(is, tok) || tok != "(")
        {
          return false;
        }
        double x = 0, y = 0, z = 0;
        NextToken(is, tok);
        x = std::stod(tok);
        NextToken(is, tok);
        y = std::stod(tok);
        NextToken(is, tok);
        z = std::stod(tok);
        NextToken(is, tok); // )
        pts->SetPoint(i, x, y, z);
      }
      return true;
    }
    if (!tok.empty() && std::isdigit(tok[0]))
    {
      try
      {
        n = std::stoi(tok);
      }
      catch (...)
      {
        n = -1;
      }
    }
  }
  if (err)
  {
    *err = "failed to parse points";
  }
  return false;
}

bool ReadFoamFaces(const std::string& path, std::vector<std::vector<int>>& faces, std::string* err)
{
  std::ifstream is(path);
  if (!is)
  {
    if (err)
    {
      *err = "cannot open " + path;
    }
    return false;
  }
  std::string tok;
  int n = -1;
  while (NextToken(is, tok))
  {
    if (tok == "(" && n >= 0)
    {
      faces.resize(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i)
      {
        if (!NextToken(is, tok))
        {
          return false;
        }
        // "4(0" compact or "4" then "("
        int nv = 0;
        std::string rest;
        const auto par = tok.find('(');
        if (par != std::string::npos)
        {
          nv = std::stoi(tok.substr(0, par));
          rest = tok.substr(par + 1);
        }
        else
        {
          nv = std::stoi(tok);
          NextToken(is, tok);
          if (tok != "(")
          {
            return false;
          }
        }
        auto& f = faces[static_cast<size_t>(i)];
        f.resize(static_cast<size_t>(nv));
        int filled = 0;
        if (!rest.empty() && rest != ")")
        {
          f[0] = std::stoi(rest);
          filled = 1;
        }
        for (int k = filled; k < nv; ++k)
        {
          NextToken(is, tok);
          if (!tok.empty() && tok.back() == ')')
          {
            tok.pop_back();
            f[static_cast<size_t>(k)] = std::stoi(tok);
            break;
          }
          f[static_cast<size_t>(k)] = std::stoi(tok);
        }
        // consume closing ) if still there
      }
      return true;
    }
    if (!tok.empty() && std::isdigit(tok[0]))
    {
      try
      {
        n = std::stoi(tok);
      }
      catch (...)
      {
        n = -1;
      }
    }
  }
  if (err)
  {
    *err = "failed to parse faces";
  }
  return false;
}

bool PolyMeshToVtk(const std::string& meshDir, vtkUnstructuredGrid* output, std::string* err)
{
  vtkNew<vtkPoints> pts;
  if (!ReadFoamPoints(Join(meshDir, "points"), pts, err))
  {
    return false;
  }
  std::vector<std::vector<int>> faces;
  if (!ReadFoamFaces(Join(meshDir, "faces"), faces, err))
  {
    return false;
  }
  std::vector<int> owner, neighbour;
  if (!ReadFoamLabelListFile(Join(meshDir, "owner"), owner, err))
  {
    return false;
  }
  ReadFoamLabelListFile(Join(meshDir, "neighbour"), neighbour, nullptr);
  int nCells = 0;
  for (int o : owner)
  {
    nCells = std::max(nCells, o + 1);
  }
  std::vector<std::vector<int>> cellFaces(static_cast<size_t>(nCells));
  for (size_t fi = 0; fi < owner.size(); ++fi)
  {
    cellFaces[static_cast<size_t>(owner[fi])].push_back(static_cast<int>(fi));
    if (fi < neighbour.size() && neighbour[fi] >= 0)
    {
      cellFaces[static_cast<size_t>(neighbour[fi])].push_back(static_cast<int>(fi));
    }
  }
  output->SetPoints(pts);
  output->Allocate(nCells);
  for (int ci = 0; ci < nCells; ++ci)
  {
    const auto& cfs = cellFaces[static_cast<size_t>(ci)];
    vtkNew<vtkIdList> stream;
    stream->InsertNextId(static_cast<vtkIdType>(cfs.size()));
    for (int fi : cfs)
    {
      const auto& f = faces[static_cast<size_t>(fi)];
      stream->InsertNextId(static_cast<vtkIdType>(f.size()));
      for (int v : f)
      {
        stream->InsertNextId(v);
      }
    }
    output->InsertNextCell(VTK_POLYHEDRON, stream);
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

  const std::string tmpRoot = Join(".", "shyx-snappy-tmp");
  MkDir(tmpRoot);
  const std::string stlPath = Join(tmpRoot, "geometry.stl");
  const std::string caseDir = Join(tmpRoot, "case");

  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(input);
  tri->Update();
  vtkNew<vtkSTLWriter> stl;
  stl->SetFileName(stlPath.c_str());
  stl->SetInputData(tri->GetOutput());
  stl->SetFileTypeToASCII();
  stl->Write();

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
  if (rc != 0 && rc != 5)
  {
    vtkErrorMacro(<< "snappyHexMesh failed (" << rc << "): " << err);
    return 0;
  }
  if (rc == 5)
  {
    vtkWarningMacro(<< err);
  }

  std::string meshDir = Join(Join(caseDir, "constant"), "polyMesh");
  std::string parseErr;
  if (!PolyMeshToVtk(meshDir, output, &parseErr))
  {
    vtkErrorMacro(<< "Failed to read polyMesh: " << parseErr);
    return 0;
  }
  return 1;
}
