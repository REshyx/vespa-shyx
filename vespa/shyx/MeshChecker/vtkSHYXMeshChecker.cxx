#include "vtkSHYXMeshChecker.h"

#include "vtkCGALHelper.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkLine.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyLine.h>
#include <vtkPolyData.h>
#include <vtkTriangle.h>

#include <CGAL/version.h>
#include <CGAL/Polygon_mesh_processing/intersection.h>
#if CGAL_VERSION_NR >= 1060000000
#  include <CGAL/Polygon_mesh_processing/autorefinement.h>
#endif
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/boost/graph/helpers.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef VTKSHYXMESHCHECKER_HAVE_TBB
#  include <tbb/blocked_range.h>
#  include <tbb/enumerable_thread_specific.h>
#  include <tbb/parallel_for.h>
#  include <tbb/parallel_sort.h>
#endif

vtkStandardNewMacro(vtkSHYXMeshChecker);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
vtkSHYXMeshChecker::vtkSHYXMeshChecker()
{
  this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
int vtkSHYXMeshChecker::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

namespace
{
using SMHalfedge = boost::graph_traits<CGAL_Surface>::halfedge_descriptor;

struct SoupEdgeKey
{
  std::size_t a;
  std::size_t b;
};

inline bool operator<(SoupEdgeKey x, SoupEdgeKey y)
{
  return x.a < y.a || (x.a == y.a && x.b < y.b);
}

inline bool operator==(SoupEdgeKey x, SoupEdgeKey y)
{
  return x.a == y.a && x.b == y.b;
}

inline SoupEdgeKey make_key(std::size_t u, std::size_t v)
{
  if (u > v)
  {
    std::swap(u, v);
  }
  return { u, v };
}

void collect_soup_edges_parallel(const vtkCGALHelper::Vespa_soup& soup, std::vector<SoupEdgeKey>& out_keys)
{
  const std::size_t nf = soup.faces.size();
  if (nf == 0)
  {
    return;
  }

#ifdef VTKSHYXMESHCHECKER_HAVE_TBB
  tbb::enumerable_thread_specific<std::vector<SoupEdgeKey>> tls;
  tbb::parallel_for(tbb::blocked_range<std::size_t>(0, nf), [&](const tbb::blocked_range<std::size_t>& r) {
    auto& local = tls.local();
    for (std::size_t fi = r.begin(); fi != r.end(); ++fi)
    {
      const auto& f = soup.faces[fi];
      if (f.size() < 2)
      {
        continue;
      }
      for (std::size_t i = 0; i < f.size(); ++i)
      {
        const std::size_t u = f[i];
        const std::size_t v = f[(i + 1) % f.size()];
        local.push_back(make_key(u, v));
      }
    }
  });
  std::size_t total = 0;
  for (const auto& v : tls)
  {
    total += v.size();
  }
  out_keys.clear();
  out_keys.reserve(total);
  for (const auto& v : tls)
  {
    out_keys.insert(out_keys.end(), v.begin(), v.end());
  }
  tbb::parallel_sort(out_keys.begin(), out_keys.end());
#else
  out_keys.clear();
  out_keys.reserve(nf * 3);
  for (std::size_t fi = 0; fi < nf; ++fi)
  {
    const auto& f = soup.faces[fi];
    if (f.size() < 2)
    {
      continue;
    }
    for (std::size_t i = 0; i < f.size(); ++i)
    {
      const std::size_t u = f[i];
      const std::size_t v = f[(i + 1) % f.size()];
      out_keys.push_back(make_key(u, v));
    }
  }
  std::sort(out_keys.begin(), out_keys.end());
#endif
}

void append_non_manifold_soup_edges(const vtkCGALHelper::Vespa_soup& soup, vtkPoints* pts, vtkCellArray* lines,
  vtkIntArray* reason, int reason_value, std::size_t* out_sorted_edge_keys = nullptr,
  std::size_t* out_groups_incidence_gt2 = nullptr, std::size_t* out_skipped_oob = nullptr)
{
  std::vector<SoupEdgeKey> keys;
  collect_soup_edges_parallel(soup, keys);
  if (out_sorted_edge_keys)
  {
    *out_sorted_edge_keys = keys.size();
  }
  if (out_groups_incidence_gt2)
  {
    *out_groups_incidence_gt2 = 0;
  }
  if (out_skipped_oob)
  {
    *out_skipped_oob = 0;
  }
  if (keys.empty())
  {
    return;
  }
  for (std::size_t i = 0; i < keys.size();)
  {
    std::size_t j = i + 1;
    while (j < keys.size() && keys[j] == keys[i])
    {
      ++j;
    }
    const std::size_t count = j - i;
    if (count > 2)
    {
      if (out_groups_incidence_gt2)
      {
        ++(*out_groups_incidence_gt2);
      }
      const SoupEdgeKey& e = keys[i];
      if (e.a < soup.points.size() && e.b < soup.points.size())
      {
        const auto& pa = soup.points[e.a];
        const auto& pb = soup.points[e.b];
        const vtkIdType id0 =
          pts->InsertNextPoint(CGAL::to_double(pa.x()), CGAL::to_double(pa.y()), CGAL::to_double(pa.z()));
        const vtkIdType id1 =
          pts->InsertNextPoint(CGAL::to_double(pb.x()), CGAL::to_double(pb.y()), CGAL::to_double(pb.z()));
        vtkNew<vtkLine> line;
        line->GetPointIds()->SetId(0, id0);
        line->GetPointIds()->SetId(1, id1);
        lines->InsertNextCell(line);
        reason->InsertNextTuple1(reason_value);
      }
      else if (out_skipped_oob)
      {
        ++(*out_skipped_oob);
      }
    }
    i = j;
  }
}

struct VertexHash
{
  std::size_t operator()(Graph_Verts v) const noexcept { return static_cast<std::size_t>(v); }
};

void append_boundary_polylines(const CGAL_Surface& mesh, const Graph_Coord& coords, vtkPoints* pts,
  vtkCellArray* lines, vtkIntArray* reason, int reason_value, std::size_t* out_border_succ_size = nullptr,
  vtkIdType* out_polylines_inserted = nullptr)
{
  std::unordered_map<Graph_Verts, Graph_Verts, VertexHash> succ;
  succ.reserve(mesh.number_of_halfedges() / 2 + 1);
  for (SMHalfedge h : mesh.halfedges())
  {
    if (mesh.is_border(h))
    {
      succ[mesh.source(h)] = mesh.target(h);
    }
  }
  if (out_border_succ_size)
  {
    *out_border_succ_size = succ.size();
  }
  vtkIdType polylines_inserted = 0;
  if (succ.empty())
  {
    if (out_polylines_inserted)
    {
      *out_polylines_inserted = 0;
    }
    return;
  }

  std::unordered_set<Graph_Verts, VertexHash> visited;
  visited.reserve(succ.size());

  for (const auto& seed : succ)
  {
    const Graph_Verts start = seed.first;
    if (visited.count(start) != 0u)
    {
      continue;
    }

    std::vector<Graph_Verts> cycle;
    Graph_Verts cur = start;
    do
    {
      if (visited.count(cur) != 0u)
      {
        break;
      }
      visited.insert(cur);
      cycle.push_back(cur);
      const auto it = succ.find(cur);
      if (it == succ.end())
      {
        break;
      }
      cur = it->second;
    } while (cur != start);

    if (cycle.size() < 2)
    {
      continue;
    }

    vtkNew<vtkPolyLine> poly;
    poly->GetPointIds()->SetNumberOfIds(static_cast<vtkIdType>(cycle.size()));
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(cycle.size()); ++i)
    {
      const auto& p = get(coords, cycle[static_cast<std::size_t>(i)]);
      const vtkIdType pid = pts->InsertNextPoint(
        CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
      poly->GetPointIds()->SetId(i, pid);
    }
    lines->InsertNextCell(poly);
    reason->InsertNextTuple1(reason_value);
    ++polylines_inserted;
  }
  if (out_polylines_inserted)
  {
    *out_polylines_inserted = polylines_inserted;
  }
}

void append_face_triangle(const CGAL_Surface& mesh, const Graph_Coord& coords, Graph_Faces f, vtkPoints* pts,
  vtkCellArray* polys, vtkIntArray* reason, int reason_value)
{
  SMHalfedge h = mesh.halfedge(f);
  vtkNew<vtkTriangle> tri;
  int k = 0;
  SMHalfedge cur = h;
  int guard = 0;
  do
  {
    if (k >= 3 || guard++ > 16)
    {
      return;
    }
    const auto& p = get(coords, mesh.target(cur));
    const vtkIdType pid =
      pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    tri->GetPointIds()->SetId(k++, pid);
    cur = mesh.next(cur);
  } while (cur != h);
  if (k != 3)
  {
    return;
  }
  polys->InsertNextCell(tri);
  reason->InsertNextTuple1(reason_value);
}

void append_self_intersection_triangles(const CGAL_Surface& mesh, const Graph_Coord& coords, vtkPoints* pts,
  vtkCellArray* polys, vtkIntArray* reason, int reason_value, std::size_t* out_intersecting_pairs = nullptr,
  vtkIdType* out_triangles_inserted = nullptr)
{
  if (!CGAL::is_triangle_mesh(mesh))
  {
    if (out_intersecting_pairs)
    {
      *out_intersecting_pairs = 0;
    }
    if (out_triangles_inserted)
    {
      *out_triangles_inserted = 0;
    }
    return;
  }
  std::vector<std::pair<Graph_Faces, Graph_Faces>> pairs;
  (void)pmp::self_intersections(mesh, std::back_inserter(pairs));
  if (out_intersecting_pairs)
  {
    *out_intersecting_pairs = pairs.size();
  }
  const vtkIdType polys_before = polys->GetNumberOfCells();
  for (const auto& pr : pairs)
  {
    append_face_triangle(mesh, coords, pr.first, pts, polys, reason, reason_value);
    append_face_triangle(mesh, coords, pr.second, pts, polys, reason, reason_value);
  }
  if (out_triangles_inserted)
  {
    *out_triangles_inserted = polys->GetNumberOfCells() - polys_before;
  }
}

/** Same gate as vtkSHYXAutorefineSelfIntersectionFilter, plus self_intersections when does_self_intersect
 *  is false but intersecting face pairs exist. */
bool triangle_mesh_should_autorefine(const CGAL_Surface& mesh)
{
  if (!CGAL::is_triangle_mesh(mesh))
  {
    return false;
  }
  try
  {
    if (pmp::does_self_intersect(mesh))
    {
      return true;
    }
    std::vector<std::pair<Graph_Faces, Graph_Faces>> pairs;
    (void)pmp::self_intersections(mesh, std::back_inserter(pairs));
    return !pairs.empty();
  }
  catch (const std::exception&)
  {
    return false;
  }
}

} // namespace

//------------------------------------------------------------------------------
void vtkSHYXMeshChecker::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CheckSoupEdges: " << (this->CheckSoupEdges ? "on\n" : "off\n");
  os << indent << "CheckBoundary: " << (this->CheckBoundary ? "on\n" : "off\n");
  os << indent << "CheckSelfIntersection: " << (this->CheckSelfIntersection ? "on\n" : "off\n");
  os << indent << "CheckOrient: " << (this->CheckOrient ? "on\n" : "off\n");
  os << indent << "AttemptOrientRepair: " << (this->AttemptOrientRepair ? "on\n" : "off\n");
  os << indent << "AttemptRepairSelfIntersections: " << (this->AttemptRepairSelfIntersections ? "on\n" : "off\n");
  os << indent << "OnlyIfSelfIntersecting: " << (this->OnlyIfSelfIntersecting ? "on\n" : "off\n");
  os << indent << "PreserveGenus: " << (this->PreserveGenus ? "on\n" : "off\n");
  os << indent << "LogSteps: " << (this->LogSteps ? "on\n" : "off\n");
}

//------------------------------------------------------------------------------
int vtkSHYXMeshChecker::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* outMesh = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outBad = vtkPolyData::GetData(outputVector, 1);
  if (!input || !outMesh || !outBad)
  {
    vtkErrorMacro("Missing input or output (expect two vtkPolyData output ports).");
    return 0;
  }

  vtkNew<vtkPoints> pts;
  vtkNew<vtkCellArray> line_cells;
  vtkNew<vtkCellArray> poly_cells;
  vtkNew<vtkIntArray> reason;
  reason->SetName("SHYX_CheckReason");
  reason->SetNumberOfComponents(1);

  vtkCGALHelper::Vespa_soup soup;
  vtkCGALHelper::toCGAL(input, &soup);

  if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] Step 0: input vtkPolyData points=" << input->GetNumberOfPoints()
                     << " cells=" << input->GetNumberOfCells()
                     << ". Out: port0=repaired mesh, port1=diagnostics. Checks: soup_edges="
                     << this->CheckSoupEdges << " boundary=" << this->CheckBoundary
                     << " self_intersection=" << this->CheckSelfIntersection
                     << " check_orient=" << this->CheckOrient << " attempt_soup_repair=" << this->AttemptOrientRepair
                     << " attempt_intersection_repair=" << this->AttemptRepairSelfIntersections
                     << " only_if_self_intersecting=" << this->OnlyIfSelfIntersecting
                     << " preserve_genus=" << this->PreserveGenus);
    vtkWarningMacro(<< "[SHYXMeshChecker] Step 1: polygon soup from VTK; soup points=" << soup.points.size()
                     << " soup faces=" << soup.faces.size());
  }

  vtkIdType n_line_cells_after_soup = 0;
  std::size_t soup_keys = 0, soup_gt2_groups = 0, soup_oob = 0;
  if (this->CheckSoupEdges)
  {
    const vtkIdType lines_before = line_cells->GetNumberOfCells();
    append_non_manifold_soup_edges(soup, pts.Get(), line_cells.Get(), reason.Get(), 1, &soup_keys, &soup_gt2_groups,
      &soup_oob);
    n_line_cells_after_soup = line_cells->GetNumberOfCells() - lines_before;
    if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXMeshChecker] Step 2 (reason=1 soup): sorted canonical edge keys=" << soup_keys
                       << " groups with incidence>2=" << soup_gt2_groups
                       << " skipped (vertex index OOB)=" << soup_oob
                       << " VTK_LINE cells added=" << n_line_cells_after_soup);
    }
  }
  else if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] Step 2 (reason=1 soup): skipped (CheckSoupEdges off).");
  }

  bool soup_is_mesh = false;
  try
  {
    bool is_oriented = true;
    if (this->CheckOrient)
    {
      is_oriented = pmp::orient_polygon_soup(soup.points, soup.faces);
      if (!is_oriented)
      {
        vtkWarningMacro(<< "[SHYXMeshChecker] Could not uniformly orient the polygon soup (e.g., it has a "
                           "Mobius strip-like topology).");
      }
    }
    if (this->AttemptOrientRepair)
    {
      pmp::repair_polygon_soup(soup.points, soup.faces);
    }
    soup_is_mesh = pmp::is_polygon_soup_a_polygon_mesh(soup.faces);
    if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXMeshChecker] Step 2.5: CheckOrient=" << (this->CheckOrient ? "on" : "off")
                       << " orient_ok=" << (this->CheckOrient ? (is_oriented ? "true" : "false") : "n/a")
                       << "; AttemptOrientRepair(repair_polygon_soup)="
                       << (this->AttemptOrientRepair ? "on" : "off")
                       << "; soup points=" << soup.points.size() << " soup faces=" << soup.faces.size());
    }
  }
  catch (const std::exception& e)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] Step 2.5/3: soup orient/repair or is_polygon_soup_a_polygon_mesh threw: "
                     << e.what());
    soup_is_mesh = false;
  }

  if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] Step 3: is_polygon_soup_a_polygon_mesh -> "
                     << (soup_is_mesh ? "true (CGAL can build Surface_mesh)"
                                     : "false (soup fails CGAL combinatorial rules; steps 4-6 skipped)"));
  }

  bool have_surface = false;
  std::unique_ptr<vtkCGALHelper::Vespa_surface> surf;
  if (soup_is_mesh)
  {
    try
    {
      surf = std::make_unique<vtkCGALHelper::Vespa_surface>();
      pmp::polygon_soup_to_polygon_mesh(soup.points, soup.faces, surf->surface);
      have_surface = true;
      if (this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXMeshChecker] Step 4: polygon_soup_to_polygon_mesh OK; CGAL vertices="
                         << surf->surface.number_of_vertices() << " faces=" << surf->surface.number_of_faces()
                         << " is_triangle_mesh=" << (CGAL::is_triangle_mesh(surf->surface) ? "true" : "false"));
      }
    }
    catch (const std::exception& e)
    {
      vtkWarningMacro(<< "[SHYXMeshChecker] Step 4: polygon_soup_to_polygon_mesh failed: " << e.what());
    }
  }
  else if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] Step 4: skipped polygon_soup_to_polygon_mesh (soup not a polygon mesh).");
  }

  if (have_surface && surf)
  {
    if (this->CheckBoundary)
    {
      const vtkIdType lines_before_bd = line_cells->GetNumberOfCells();
      std::size_t succ_sz = 0;
      vtkIdType n_polylines = 0;
      append_boundary_polylines(surf->surface, surf->coords, pts.Get(), line_cells.Get(), reason.Get(), 2,
        &succ_sz, &n_polylines);
      if (this->LogSteps)
      {
        vtkWarningMacro(<< "[SHYXMeshChecker] Step 5 (reason=2 boundary): border succ map size=" << succ_sz
                         << " VTK_POLY_LINE inserted=" << n_polylines
                         << " line-port cells total now=" << line_cells->GetNumberOfCells()
                         << " (delta " << (line_cells->GetNumberOfCells() - lines_before_bd) << " on this step)");
      }
    }
    else if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXMeshChecker] Step 5 (reason=2 boundary): skipped (CheckBoundary off).");
    }

    if (this->CheckSelfIntersection)
    {
      if (!CGAL::is_triangle_mesh(surf->surface))
      {
        vtkWarningMacro(<< "[SHYXMeshChecker] Step 6 (reason=3 self-intersection): skipped - not a pure triangle "
                           "mesh (PMP::self_intersections needs all-triangle faces).");
      }
      else
      {
        std::size_t n_pairs = 0;
        vtkIdType n_tris = 0;
        try
        {
          append_self_intersection_triangles(surf->surface, surf->coords, pts.Get(), poly_cells.Get(),
            reason.Get(), 3, &n_pairs, &n_tris);
          if (this->LogSteps)
          {
            vtkWarningMacro(<< "[SHYXMeshChecker] Step 6 (reason=3 self-intersection): intersecting face pairs="
                             << n_pairs << " VTK_TRIANGLE cells added=" << n_tris
                             << " (expect triangles ~= 2*pairs if all faces are triangles).");
          }
        }
        catch (const std::exception& e)
        {
          vtkWarningMacro(<< "[SHYXMeshChecker] Step 6: CGAL self_intersections failed: " << e.what());
        }
      }
    }
    else if (this->LogSteps)
    {
      vtkWarningMacro(<< "[SHYXMeshChecker] Step 6 (reason=3 self-intersection): skipped (CheckSelfIntersection off).");
    }
  }
  else if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] Steps 5-6: skipped (no CGAL surface mesh).");
  }

  const vtkIdType out_lines = line_cells->GetNumberOfCells();
  const vtkIdType out_polys = poly_cells->GetNumberOfCells();
  const vtkIdType out_pts = pts->GetNumberOfPoints();
  const vtkIdType out_cells = out_lines + out_polys;
  if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] Step 7: port1 diagnostic summary; points=" << out_pts
                     << " line+polyline cells=" << out_lines << " triangle cells=" << out_polys
                     << " total cells=" << out_cells << " SHYX_CheckReason tuples=" << reason->GetNumberOfTuples());
    if (out_cells == 0)
    {
      vtkWarningMacro(<< "[SHYXMeshChecker] No diagnostic geometry on port 1. Typical causes: (1) manifold soup "
                         "edges; (2) closed surface; (3) no self-intersections; (4) soup not a polygon mesh; "
                         "(5) self-intersection check skipped for non-triangle mesh.");
    }
    vtkWarningMacro(<< "[SHYXMeshChecker] --- port 1 (diagnostics) end ---");
  }

  // Output port 1: illegal / diagnostic primitives
  outBad->Initialize();
  outBad->SetPoints(pts);
  outBad->SetLines(line_cells);
  outBad->SetPolys(poly_cells);
  outBad->GetCellData()->AddArray(reason);
  outBad->GetCellData()->SetActiveScalars(reason->GetName());

  // Output port 0: best-effort repaired surface (soup or CGAL mesh, optional autorefine)
  outMesh->Initialize();
  if (have_surface && surf)
  {
    try
    {
      vtkCGALHelper::Vespa_surface outSurf;
      outSurf.surface = surf->surface;
      outSurf.coords  = get(CGAL::vertex_point, outSurf.surface);
      if (this->AttemptRepairSelfIntersections && CGAL::is_triangle_mesh(outSurf.surface))
      {
        const bool has_ix = triangle_mesh_should_autorefine(outSurf.surface);
        const bool run = !this->OnlyIfSelfIntersecting || has_ix;
        if (run)
        {
          if (this->LogSteps)
          {
#if CGAL_VERSION_NR >= 1060000000
            vtkWarningMacro(<< "[SHYXMeshChecker] port 0: PMP::autorefine (same entry point as "
                               "vtkSHYXAutorefineSelfIntersectionFilter, CGAL 6+) ...");
#else
            vtkWarningMacro(<< "[SHYXMeshChecker] port 0: PMP::experimental::autorefine_and_remove_self_intersections "
                               "(same as vtkSHYXAutorefineSelfIntersectionFilter, CGAL 5) ...");
#endif
          }
          try
          {
#if CGAL_VERSION_NR >= 1060000000
            (void)this->PreserveGenus;
            pmp::autorefine(outSurf.surface);
#else
            pmp::experimental::autorefine_and_remove_self_intersections(
              outSurf.surface, pmp::parameters::preserve_genus(this->PreserveGenus));
#endif
          }
          catch (const std::exception& e)
          {
            vtkWarningMacro(<< "[SHYXMeshChecker] port 0: autorefine threw: " << e.what());
          }
          if (this->LogSteps)
          {
            vtkWarningMacro(<< "[SHYXMeshChecker] port 0: after autorefine, does_self_intersect="
                             << (pmp::does_self_intersect(outSurf.surface) ? "true" : "false"));
          }
        }
        else if (this->LogSteps)
        {
          vtkWarningMacro(<< "[SHYXMeshChecker] port 0: autorefine skipped (OnlyIfSelfIntersecting on and no "
                             "self-intersections reported).");
        }
      }
      if (!vtkCGALHelper::toVTK(&outSurf, outMesh))
      {
        vtkWarningMacro(<< "[SHYXMeshChecker] port 0: toVTK(surface) failed.");
      }
    }
    catch (const std::exception& e)
    {
      vtkWarningMacro(<< "[SHYXMeshChecker] port 0: repair/toVTK: " << e.what()
                       << " - writing unrepaired surface mesh from soup conversion.");
      try
      {
        (void)vtkCGALHelper::toVTK(surf.get(), outMesh);
      }
      catch (const std::exception& e2)
      {
        vtkWarningMacro(<< "[SHYXMeshChecker] port 0: fallback toVTK: " << e2.what());
      }
    }
    this->interpolateAttributes(input, outMesh);
  }
  else
  {
    (void)vtkCGALHelper::toVTK(&soup, outMesh);
    this->interpolateAttributes(input, outMesh);
  }

  if (this->LogSteps)
  {
    vtkWarningMacro(<< "[SHYXMeshChecker] port 0 (repaired mesh) points=" << outMesh->GetNumberOfPoints()
                     << " cells=" << outMesh->GetNumberOfCells() << ". --- all outputs end ---");
  }

  return 1;
}
