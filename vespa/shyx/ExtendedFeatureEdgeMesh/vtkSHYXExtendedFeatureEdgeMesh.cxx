#include "vtkSHYXExtendedFeatureEdgeMesh.h"

#include "shyx_extended_feature.h"
#include "shyx_snappy.h"

#include <vtkAlgorithmOutput.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellArrayIterator.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkGeometryFilter.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTriangleFilter.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

vtkStandardNewMacro(vtkSHYXExtendedFeatureEdgeMesh);

namespace
{
const void* const kShyxFoamEnvAnchorExt = reinterpret_cast<const void*>(&shyx_touch_foam_env);
}

namespace
{
const char* EdgeStatusName(int s)
{
  switch (s)
  {
    case SHYX_EXT_EDGE_EXTERNAL:
      return "external";
    case SHYX_EXT_EDGE_INTERNAL:
      return "internal";
    case SHYX_EXT_EDGE_FLAT:
      return "flat";
    case SHYX_EXT_EDGE_OPEN:
      return "open";
    case SHYX_EXT_EDGE_MULTIPLE:
      return "multiple";
    default:
      return "none";
  }
}

const char* PointStatusName(int s)
{
  switch (s)
  {
    case SHYX_EXT_POINT_CONVEX:
      return "convex";
    case SHYX_EXT_POINT_CONCAVE:
      return "concave";
    case SHYX_EXT_POINT_MIXED:
      return "mixed";
    default:
      return "nonFeature";
  }
}

void AppendFanTriangles(const vtkIdType* ids, vtkIdType npts, int region, std::vector<int>* tris,
  std::vector<int>* regions)
{
  if (!ids || npts < 3)
  {
    return;
  }
  for (vtkIdType k = 1; k + 1 < npts; ++k)
  {
    const vtkIdType a = ids[0];
    const vtkIdType b = ids[k];
    const vtkIdType c = ids[k + 1];
    if (a == b || b == c || c == a)
    {
      continue;
    }
    tris->push_back(static_cast<int>(a));
    tris->push_back(static_cast<int>(b));
    tris->push_back(static_cast<int>(c));
    regions->push_back(region);
  }
}

int RegionOfCell(vtkDataArray* regionArr, vtkIdType cellId)
{
  if (!regionArr || cellId >= regionArr->GetNumberOfTuples())
  {
    return 0;
  }
  return std::max(0, static_cast<int>(regionArr->GetTuple1(cellId)));
}

bool RegionArrayUnset(const char* name)
{
  if (!name || name[0] == '\0')
  {
    return true;
  }
  return std::strcmp(name, "None") == 0 || std::strcmp(name, "none") == 0 ||
    std::strcmp(name, "(None)") == 0 || std::strcmp(name, "(none)") == 0;
}

void CollectTriangles(vtkPolyData* pd, const char* regionName, std::vector<double>* xyz,
  std::vector<int>* tris, std::vector<int>* regions, std::vector<int>* baffleByRegion)
{
  if (!pd)
  {
    return;
  }
  vtkSmartPointer<vtkPolyData> surface = pd;
  if (pd->GetNumberOfPolys() == 0 && pd->GetNumberOfStrips() == 0)
  {
    vtkNew<vtkGeometryFilter> geom;
    geom->SetInputData(pd);
    geom->Update();
    if (geom->GetOutput() && (geom->GetOutput()->GetNumberOfPolys() > 0 ||
                               geom->GetOutput()->GetNumberOfStrips() > 0))
    {
      surface = geom->GetOutput();
    }
  }
  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(surface);
  tri->PassLinesOff();
  tri->PassVertsOff();
  tri->Update();
  vtkPolyData* mesh = tri->GetOutput();
  if (!mesh || !mesh->GetPoints())
  {
    return;
  }
  vtkPoints* pts = mesh->GetPoints();
  const vtkIdType nPts = pts->GetNumberOfPoints();
  xyz->resize(static_cast<size_t>(nPts) * 3);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    double p[3];
    pts->GetPoint(i, p);
    (*xyz)[static_cast<size_t>(i) * 3] = p[0];
    (*xyz)[static_cast<size_t>(i) * 3 + 1] = p[1];
    (*xyz)[static_cast<size_t>(i) * 3 + 2] = p[2];
  }
  vtkDataArray* regionArr = nullptr;
  vtkDataArray* baffleArr = nullptr;
  if (mesh->GetCellData())
  {
    if (!RegionArrayUnset(regionName))
    {
      regionArr = mesh->GetCellData()->GetArray(regionName);
    }
    baffleArr = mesh->GetCellData()->GetArray("Baffle");
  }

  // Iterate vtkCellArray directly. GetCell()/GetCellType() on vtkPolyData can
  // return VTK_EMPTY_CELL or VTK_POLYGON; skipping those used to collect 0
  // triangles and finish Apply instantly with an empty output.
  int maxRegion = 0;
  vtkIdType cellId = 0;
  auto consume = [&](vtkCellArray* cells) {
    if (!cells || cells->GetNumberOfCells() == 0)
    {
      return;
    }
    vtkSmartPointer<vtkCellArrayIterator> it = vtk::TakeSmartPointer(cells->NewIterator());
    for (it->GoToFirstCell(); !it->IsDoneWithTraversal(); it->GoToNextCell())
    {
      vtkIdType npts = 0;
      const vtkIdType* ids = nullptr;
      it->GetCurrentCell(npts, ids);
      const int r = RegionOfCell(regionArr, cellId);
      maxRegion = std::max(maxRegion, r);
      if (baffleArr && cellId < baffleArr->GetNumberOfTuples() && baffleArr->GetTuple1(cellId) != 0.0)
      {
        if (static_cast<int>(baffleByRegion->size()) <= r)
        {
          baffleByRegion->resize(static_cast<size_t>(r) + 1, 0);
        }
        (*baffleByRegion)[static_cast<size_t>(r)] = 1;
      }
      AppendFanTriangles(ids, npts, r, tris, regions);
      ++cellId;
    }
  };
  consume(mesh->GetPolys());
  consume(mesh->GetStrips());
  if (static_cast<int>(baffleByRegion->size()) <= maxRegion)
  {
    baffleByRegion->resize(static_cast<size_t>(maxRegion) + 1, 0);
  }
}

void CollectLines(vtkPolyData* pd, std::vector<double>* xyz, std::vector<int>* edges)
{
  if (!pd || !pd->GetPoints())
  {
    return;
  }
  vtkPoints* pts = pd->GetPoints();
  const int base = static_cast<int>(xyz->size() / 3);
  const vtkIdType nPts = pts->GetNumberOfPoints();
  xyz->resize(static_cast<size_t>(base + nPts) * 3);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    double p[3];
    pts->GetPoint(i, p);
    (*xyz)[static_cast<size_t>(base + i) * 3] = p[0];
    (*xyz)[static_cast<size_t>(base + i) * 3 + 1] = p[1];
    (*xyz)[static_cast<size_t>(base + i) * 3 + 2] = p[2];
  }
  vtkCellArray* lineCells = pd->GetLines();
  if (!lineCells || lineCells->GetNumberOfCells() == 0)
  {
    return;
  }
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
      const int a = base + static_cast<int>(ids[i]);
      const int b = base + static_cast<int>(ids[i + 1]);
      if (a != b)
      {
        edges->push_back(a);
        edges->push_back(b);
      }
    }
  }
}

vtkSmartPointer<vtkIntArray> MakeIntArray(const char* name, const int* data, int n)
{
  vtkNew<vtkIntArray> arr;
  arr->SetName(name);
  arr->SetNumberOfComponents(1);
  arr->SetNumberOfTuples(n);
  for (int i = 0; i < n; ++i)
  {
    arr->SetValue(i, data ? data[i] : 0);
  }
  return arr;
}

vtkSmartPointer<vtkIntArray> MakeFieldInt(const char* name, int v)
{
  vtkNew<vtkIntArray> arr;
  arr->SetName(name);
  arr->SetNumberOfTuples(1);
  arr->SetValue(0, v);
  return arr;
}
} // namespace

vtkSHYXExtendedFeatureEdgeMesh::vtkSHYXExtendedFeatureEdgeMesh()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
  this->SetRegionArrayName(nullptr);
}

vtkSHYXExtendedFeatureEdgeMesh::~vtkSHYXExtendedFeatureEdgeMesh()
{
  this->SetRegionArrayName(nullptr);
}

void vtkSHYXExtendedFeatureEdgeMesh::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "IncludedAngle: " << this->IncludedAngle << "\n";
  os << indent << "GeometricTestOnly: " << this->GeometricTestOnly << "\n";
  os << indent << "TrimMinLength: " << this->TrimMinLength << "\n";
  os << indent << "TrimMinElements: " << this->TrimMinElements << "\n";
  os << indent << "KeepOpenEdges: " << this->KeepOpenEdges << "\n";
  os << indent << "KeepNonManifoldEdges: " << this->KeepNonManifoldEdges << "\n";
  os << indent << "KeepRegionEdges: " << this->KeepRegionEdges << "\n";
  os << indent << "BaffleAllRegions: " << this->BaffleAllRegions << "\n";
  os << indent << "RegionArrayName: "
     << (this->RegionArrayName && this->RegionArrayName[0] ? this->RegionArrayName : "(none)")
     << "\n";
}

void vtkSHYXExtendedFeatureEdgeMesh::SetExtraFeatureEdgesConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

int vtkSHYXExtendedFeatureEdgeMesh::FillInputPortInformation(int port, vtkInformation* info)
{
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
  if (port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  }
  return 1;
}

int vtkSHYXExtendedFeatureEdgeMesh::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* extra = vtkPolyData::GetData(inputVector[1]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Missing input or output.");
    return 0;
  }

  std::vector<double> xyz;
  std::vector<int> tris;
  std::vector<int> regions;
  std::vector<int> baffleByRegion;
  CollectTriangles(input, this->RegionArrayName, &xyz, &tris, &regions, &baffleByRegion);

  std::vector<double> extraXyz;
  std::vector<int> extraEdges;
  CollectLines(extra, &extraXyz, &extraEdges);
  if (tris.empty())
  {
    CollectLines(input, &extraXyz, &extraEdges);
  }

  if (tris.empty() && extraEdges.empty())
  {
    vtkErrorMacro(<< "No triangles (or extra feature lines) collected from vtkPolyData. "
                  << "Input: " << input->GetNumberOfPoints() << " points, "
                  << input->GetNumberOfPolys() << " polys, " << input->GetNumberOfStrips()
                  << " strips, " << input->GetNumberOfLines() << " lines, "
                  << input->GetNumberOfVerts() << " verts. Need a surface mesh (faces), "
                  << "not a point cloud. Check Output Messages.");
    return 0;
  }

  ShyxExtendedFeatureParams p;
  shyx_extended_feature_params_default(&p);
  p.included_angle = this->IncludedAngle;
  p.geometric_test_only = this->GeometricTestOnly ? 1 : 0;
  p.trim_min_length = this->TrimMinLength;
  p.trim_min_elements = this->TrimMinElements;
  p.keep_open_edges = this->KeepOpenEdges ? 1 : 0;
  p.keep_non_manifold_edges = this->KeepNonManifoldEdges ? 1 : 0;
  p.keep_region_edges = this->KeepRegionEdges ? 1 : 0;
  p.baffle_all_regions = this->BaffleAllRegions ? 1 : 0;

  const int nTri = static_cast<int>(tris.size() / 3);
  const int nPts = static_cast<int>(xyz.size() / 3);
  const int nExtraPts = static_cast<int>(extraXyz.size() / 3);
  const int nExtraEd = static_cast<int>(extraEdges.size() / 2);
  const int nRegions = static_cast<int>(baffleByRegion.size());

  ShyxExtendedFeatureMesh mesh;
  std::memset(&mesh, 0, sizeof(mesh));
  char err[2048];
  err[0] = '\0';
  const int rc = shyx_extended_feature_extract(xyz.empty() ? nullptr : xyz.data(), nPts,
    tris.empty() ? nullptr : tris.data(), nTri, regions.empty() ? nullptr : regions.data(),
    baffleByRegion.empty() ? nullptr : baffleByRegion.data(), nRegions,
    extraXyz.empty() ? nullptr : extraXyz.data(), nExtraPts,
    extraEdges.empty() ? nullptr : extraEdges.data(), nExtraEd, &p, &mesh, err, 2048);
  if (rc != 0)
  {
    vtkErrorMacro(<< err);
    shyx_extended_feature_mesh_free(&mesh);
    return 0;
  }

  vtkNew<vtkPoints> pts;
  pts->SetNumberOfPoints(mesh.n_points);
  for (int i = 0; i < mesh.n_points; ++i)
  {
    pts->SetPoint(i, mesh.points[3 * i], mesh.points[3 * i + 1], mesh.points[3 * i + 2]);
  }
  output->Initialize();
  output->SetPoints(pts);

  vtkNew<vtkCellArray> lines;
  vtkNew<vtkIntArray> edgeStatus;
  edgeStatus->SetName("EdgeStatus");
  edgeStatus->SetNumberOfTuples(mesh.n_edges);
  vtkNew<vtkStringArray> edgeStatusName;
  edgeStatusName->SetName("EdgeStatusName");
  edgeStatusName->SetNumberOfTuples(mesh.n_edges);
  vtkNew<vtkIntArray> regionEdge;
  regionEdge->SetName("RegionEdge");
  regionEdge->SetNumberOfTuples(mesh.n_edges);
  for (int i = 0; i < mesh.n_edges; ++i)
  {
    vtkIdType ids[2] = { mesh.edges[2 * i], mesh.edges[2 * i + 1] };
    lines->InsertNextCell(2, ids);
    const int st = mesh.edge_status ? mesh.edge_status[i] : SHYX_EXT_EDGE_NONE;
    edgeStatus->SetValue(i, st);
    edgeStatusName->SetValue(i, EdgeStatusName(st));
    regionEdge->SetValue(i, mesh.region_edge ? mesh.region_edge[i] : 0);
  }
  output->SetLines(lines);

  output->GetCellData()->AddArray(edgeStatus);
  output->GetCellData()->AddArray(edgeStatusName);
  output->GetCellData()->AddArray(regionEdge);
  output->GetCellData()->SetActiveScalars("EdgeStatus");

  vtkNew<vtkIntArray> pointStatus;
  pointStatus->SetName("PointStatus");
  pointStatus->SetNumberOfTuples(mesh.n_points);
  vtkNew<vtkStringArray> pointStatusName;
  pointStatusName->SetName("PointStatusName");
  pointStatusName->SetNumberOfTuples(mesh.n_points);
  vtkNew<vtkIntArray> featurePoint;
  featurePoint->SetName("FeaturePoint");
  featurePoint->SetNumberOfTuples(mesh.n_points);
  for (int i = 0; i < mesh.n_points; ++i)
  {
    const int st = mesh.point_status ? mesh.point_status[i] : SHYX_EXT_POINT_NONFEATURE;
    pointStatus->SetValue(i, st);
    pointStatusName->SetValue(i, PointStatusName(st));
    featurePoint->SetValue(i, i < mesh.non_feature_start ? 1 : 0);
  }
  output->GetPointData()->AddArray(pointStatus);
  output->GetPointData()->AddArray(pointStatusName);
  output->GetPointData()->AddArray(featurePoint);

  vtkFieldData* fd = output->GetFieldData();
  fd->AddArray(MakeFieldInt("SHYXExtendedFeatureEdgeMesh", 1));
  fd->AddArray(MakeFieldInt("concaveStart", mesh.concave_start));
  fd->AddArray(MakeFieldInt("mixedStart", mesh.mixed_start));
  fd->AddArray(MakeFieldInt("nonFeatureStart", mesh.non_feature_start));
  fd->AddArray(MakeFieldInt("internalStart", mesh.internal_start));
  fd->AddArray(MakeFieldInt("flatStart", mesh.flat_start));
  fd->AddArray(MakeFieldInt("openStart", mesh.open_start));
  fd->AddArray(MakeFieldInt("multipleStart", mesh.multiple_start));
  if (mesh.foam_ascii && mesh.foam_ascii[0] != '\0')
  {
    vtkNew<vtkStringArray> blob;
    blob->SetName("FoamExtendedFeatureEdgeMesh");
    blob->SetNumberOfTuples(1);
    blob->SetValue(0, mesh.foam_ascii);
    fd->AddArray(blob);
  }

  vtkDebugMacro(<< "extendedFeatureEdgeMesh: " << mesh.n_edges << " edges, " << mesh.n_points
                << " points from " << nTri << " triangles.");
  shyx_extended_feature_mesh_free(&mesh);
  return 1;
}
