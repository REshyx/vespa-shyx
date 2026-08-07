#ifndef vtkSHYXFeatureAwareAdaptiveSizingField_h
#define vtkSHYXFeatureAwareAdaptiveSizingField_h

#include "vtkCGALHelper.h"

#include <CGAL/Kernel_traits.h>
#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <CGAL/boost/graph/Face_filtered_graph.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/boost/graph/selection.h>
#include <CGAL/number_utils.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "custom_interpolated_corrected_curvatures.h"

namespace vespa_shyx_air_remesh_internals
{
namespace pmp_sf = CGAL::Polygon_mesh_processing;

/** %TEMP%/vespa_shyx_sizing_profile.log. Set VESPA_SIZING_PROFILE_VERBOSE=1 for Dijkstra detail. */
inline bool sizingProfileVerbose()
{
  const char* e = std::getenv("VESPA_SIZING_PROFILE_VERBOSE");
  return e && e[0] == '1';
}

inline unsigned& sizingProfileTlsRunId()
{
  thread_local unsigned id = 0;
  return id;
}

inline const char*& sizingProfileTlsKind()
{
  thread_local const char* kind = "";
  return kind;
}

struct SizingProfileRunGuard
{
  explicit SizingProfileRunGuard(bool wallRemesh)
  {
    static std::atomic<unsigned> counter{0};
    id_ = ++counter;
    kind_ = wallRemesh ? "WALL" : "PREVIEW";
    sizingProfileTlsRunId() = id_;
    sizingProfileTlsKind() = kind_;
  }
  ~SizingProfileRunGuard()
  {
    sizingProfileTlsRunId() = 0;
    sizingProfileTlsKind() = "";
  }
  unsigned id() const { return id_; }
  const char* kind() const { return kind_; }

private:
  unsigned id_ = 0;
  const char* kind_ = "";
};

inline void sizingProfileLog(const char* msg)
{
  const char* tmp = std::getenv("TEMP");
  if (!tmp || !tmp[0])
  {
    tmp = std::getenv("TMP");
  }
  if (!tmp || !tmp[0])
  {
    tmp = std::getenv("TMPDIR");
  }
  if (!tmp || !tmp[0])
  {
    tmp = ".";
  }
  const std::string path = std::string(tmp) + "/vespa_shyx_sizing_profile.log";
  if (FILE* f = std::fopen(path.c_str(), "a"))
  {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    const unsigned run = sizingProfileTlsRunId();
    const char* kind = sizingProfileTlsKind();
    if (run != 0 && kind && kind[0])
    {
      std::fprintf(f, "%s #%u %-7s | %s\n", ts, run, kind, msg ? msg : "(null)");
    }
    else
    {
      std::fprintf(f, "%s         | %s\n", ts, msg ? msg : "(null)");
    }
    std::fflush(f);
    std::fclose(f);
  }
}

inline double msSince(std::chrono::steady_clock::time_point t0)
{
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

/**
 * Curvature-driven sizing field with a single per-vertex target-length map
 * (`v:vespa_size_global`). Mirrors CGAL's Adaptive_sizing_field math
 * (interpolated corrected curvatures, vertex_size_sq = 6*tol/|kappa|max - 3*tol^2, clamped to
 * [short, long]) but stores the cache in a **distinct named** Surface_mesh property map to avoid
 * the dynamic_vertex_property_t key collision in CGAL's own class.
 *
 * Optional neighbor ratio limit: when @a neighbor_max_ratio > 1 in the constructor, targets are
 * relaxed so along every mesh edge neither endpoint exceeds R times the other (only reductions).
 * Implemented as multi-source multiplicative Dijkstra achieving
 * \(s^*(v)=\min_u s_0(u)\,R^{d(u,v)}\); the legacy Gauss–Seidel edge-sweep remains available for
 * comparison. R <= 1 disables.
 *
 * Optional scale-to-range: when @a scale_to_range is true, after all per-vertex targets (and
 * neighbor ratio limiting) are computed, the values are linearly remapped so the actual min maps
 * to short (MinEdgeLength) and the actual max maps to long (MaxEdgeLength). Vertices with a
 * zero-initialised target (uncovered by the face range) are excluded from the min/max scan and
 * left at zero. This stretches the curvature-derived distribution to fill the full sizing interval.
 *
 * Optional uncapped preview: when @a uncapped_sizes_out is non-null, the constructor's first ICC
 * pass also writes per-vertex **unclamped** target lengths (NaN where the ICC formula is undefined)
 * indexed by `vertex_descriptor::idx()`. The pointer is cleared after that pass so later
 * `recompute_curvature` / remesh iterations do not overwrite the snapshot.
 *
 * Copy semantics: copy-constructible; the only non-trivial member is a property-map handle that
 * refers to data living on the surface mesh, exactly like CGAL's Adaptive_sizing_field. The named
 * property map is cleaned up when the mesh itself is destroyed (each RequestData call constructs a
 * fresh CGAL_Surface via vtkCGALHelper, so nothing leaks across runs).
 *
 * Between CGAL remesh iterations, `recompute_curvature(mesh)` may be called to run ICC again on
 * the **current full mesh** and refill the map. Patch remesh still uses the expanded
 * Face_filtered_graph only for the **initial** constructor pass; refresh uses the whole surface so
 * face indices stay valid after topology changes.
 *
 * ICC vertex normals: build `v:vespa_icc_normal` (global area blend) plus, when the feature mask
 * is enabled, `f:vespa_icc_in_mask`, `v:vespa_icc_n_mask`, and `v:vespa_icc_n_nonmask` via
 * `PrepareIccVertexNormalsForAdaptiveSizing` before constructing this object. `recompute_curvature`
 * refreshes `v:vespa_icc_normal` only with plain `compute_vertex_normals` (dual-region maps are
 * not rebuilt across remesh topology).
 */
class FeatureAwareAdaptiveSizingField
{
public:
  using FT                  = double;
  using Point_3             = CGAL_Surface::Point;
  using vertex_descriptor   = CGAL_Surface::Vertex_index;
  using halfedge_descriptor = CGAL_Surface::Halfedge_index;
  using face_descriptor     = CGAL_Surface::Face_index;

  template <typename FaceRange>
  FeatureAwareAdaptiveSizingField(FT tol, std::pair<FT, FT> bounds, const FaceRange& face_range,
    CGAL_Surface& mesh, FT neighbor_max_ratio = FT(0), bool scale_to_range = false,
    std::vector<FT>* uncapped_sizes_out = nullptr)
    : tol_g_(tol)
    , short_g_(bounds.first)
    , long_g_(bounds.second)
    , neighbor_max_ratio_(neighbor_max_ratio)
    , scale_to_range_(scale_to_range)
    , uncapped_sizes_out_(uncapped_sizes_out)
  {
    map_g_ =
      mesh.template add_property_map<vertex_descriptor, FT>("v:vespa_size_global", FT(0)).first;

    if (face_range.size() == faces(mesh).size())
    {
      compute_sizes_(mesh, mesh);
    }
    else
    {
      std::vector<face_descriptor> sel(face_range.begin(), face_range.end());
      auto is_sel = get(CGAL::dynamic_face_property_t<bool>(), mesh);
      for (face_descriptor f : faces(mesh))
      {
        put(is_sel, f, false);
      }
      for (face_descriptor f : face_range)
      {
        put(is_sel, f, true);
      }
      CGAL::expand_face_selection(sel, mesh, 1, is_sel, std::back_inserter(sel));
      CGAL::Face_filtered_graph<CGAL_Surface> ffg(mesh, sel);
      compute_sizes_(ffg, mesh);
    }
    // Snapshot only the pre-remesh ICC pass; iteration refreshes must not mutate the preview.
    uncapped_sizes_out_ = nullptr;
  }

  /**
   * Re-run custom_interpolated_corrected_curvatures on the current geometry and refill map_g_.
   * Uses the entire mesh as the curvature domain (see class comment for patch vs refresh).
   * Refreshes `v:vespa_icc_normal` with CGAL `compute_vertex_normals` when present.
   */
  void recompute_curvature(CGAL_Surface& mesh)
  {
    if (sizingProfileTlsRunId() != 0 || sizingProfileVerbose())
    {
      sizingProfileLog("RECOMPUTE curvature begin (plain compute_vertex_normals + ICC; "
                       "NOT full PrepareIccVertexNormals)");
    }
    const auto tAll = std::chrono::steady_clock::now();
    const auto vn_opt =
      mesh.property_map<vertex_descriptor, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");
    if (vn_opt.has_value())
    {
      pmp_sf::compute_vertex_normals(mesh, *vn_opt);
    }
    compute_sizes_(mesh, mesh);
    if (sizingProfileTlsRunId() != 0 || sizingProfileVerbose())
    {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "RECOMPUTE curvature end: %.1f ms", msSince(tAll));
      sizingProfileLog(buf);
    }
  }

  /**
   * Refill the target-length map using the **current** `v:vespa_icc_normal` without modifying it.
   * Use after PrepareIccVertexNormalsForAdaptiveSizing (e.g. sizing ICC preview port) so capped
   * `v:vespa_size_global` matches fresh normals; remesh-alone paths may leave the map stale.
   */
  void recompute_sizes_from_current_icc_normals(CGAL_Surface& mesh) { compute_sizes_(mesh, mesh); }

  FT at(const vertex_descriptor v, const CGAL_Surface& /*sm*/) const { return get(map_g_, v); }

  std::optional<FT> is_too_long(
    const vertex_descriptor va, const vertex_descriptor vb, const CGAL_Surface& sm) const
  {
    const FT s = (CGAL::min)(get(map_g_, va), get(map_g_, vb));
    const FT sqlen = CGAL::squared_distance(sm.point(va), sm.point(vb));
    const FT sqt = CGAL::square((FT(4) / FT(3)) * s);
    if (sqt > FT(0) && sqlen > sqt)
    {
      return sqlen / sqt;
    }
    return std::nullopt;
  }

  std::optional<FT> is_too_short(const halfedge_descriptor h, const CGAL_Surface& sm) const
  {
    const auto va = sm.source(h);
    const auto vb = sm.target(h);
    const FT s = (CGAL::min)(get(map_g_, va), get(map_g_, vb));
    const FT sqlen = CGAL::squared_distance(sm.point(va), sm.point(vb));
    const FT sqt = CGAL::square((FT(4) / FT(5)) * s);
    if (sqt > FT(0) && sqlen < sqt)
    {
      return sqlen / sqt;
    }
    return std::nullopt;
  }

  Point_3 split_placement(const halfedge_descriptor h, const CGAL_Surface& sm) const
  {
    return CGAL::midpoint(sm.point(sm.source(h)), sm.point(sm.target(h)));
  }

  void register_split_vertex(const vertex_descriptor v, const CGAL_Surface& sm)
  {
    FT sg = 0;
    std::size_t n = 0;
    for (halfedge_descriptor ha : CGAL::halfedges_around_target(v, sm))
    {
      sg += get(map_g_, sm.source(ha));
      ++n;
    }
    if (n > 0)
    {
      put(map_g_, v, sg / FT(n));
    }
  }

private:
  using VSizeMap = typename CGAL_Surface::template Property_map<vertex_descriptor, FT>;

  template <typename Principal>
  static FT max_abs_principal_(const Principal& vc)
  {
    return (CGAL::max)(CGAL::abs(vc.max_curvature), CGAL::abs(vc.min_curvature));
  }

  template <typename FaceGraph>
  void compute_sizes_(FaceGraph& fg, CGAL_Surface& mesh)
  {
    using Kernel    = typename CGAL::Kernel_traits<Point_3>::Kernel;
    using Principal = vespa_shyx::Custom_principal_curvatures_and_directions<Kernel>;
    using CTag      = CGAL::dynamic_vertex_property_t<Principal>;

    const auto tAll = std::chrono::steady_clock::now();
    const std::size_t nv = static_cast<std::size_t>(mesh.number_of_vertices());
    const std::size_t ne = static_cast<std::size_t>(mesh.number_of_edges());
    const std::size_t nf = static_cast<std::size_t>(mesh.number_of_faces());

    auto curv_map = get(CTag(), fg);
    const auto vn_opt =
      mesh.property_map<vertex_descriptor, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");

    const auto tIcc = std::chrono::steady_clock::now();
    if (vn_opt.has_value())
    {
      vespa_shyx::custom_interpolated_corrected_curvatures(fg,
        pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map)
          .vertex_normal_map(*vn_opt),
        &mesh);
    }
    else
    {
      vespa_shyx::custom_interpolated_corrected_curvatures(
        fg, pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map), &mesh);
    }
    const double iccMs = msSince(tIcc);

    if (uncapped_sizes_out_)
    {
      const FT nan = std::numeric_limits<FT>::quiet_NaN();
      uncapped_sizes_out_->assign(nv, nan);
    }

    const auto tFill = std::chrono::steady_clock::now();
    for (auto v : vertices(fg))
    {
      const Principal vc = get(curv_map, v);
      const FT max_abs = max_abs_principal_(vc);
      if (uncapped_sizes_out_)
      {
        const std::size_t idi = static_cast<std::size_t>(v.idx());
        if (idi < uncapped_sizes_out_->size())
        {
          (*uncapped_sizes_out_)[idi] = uncapped_vertex_size_(tol_g_, max_abs);
        }
      }
      put(map_g_, v, vertex_size_(tol_g_, short_g_, long_g_, max_abs));
    }
    const double fillMs = msSince(tFill);

    const auto tGrad = std::chrono::steady_clock::now();
    gradient_limit_vertex_sizes_(mesh);
    const double gradMs = msSince(tGrad);

    const auto tScale = std::chrono::steady_clock::now();
    scale_to_range_map_(mesh);
    const double scaleMs = msSince(tScale);

    // Compact sizing summary (always); edge too-long/short predicts remesh split/collapse load.
    double sMin = std::numeric_limits<double>::infinity();
    double sMax = 0.0;
    std::size_t nAtMin = 0;
    std::size_t nAtMax = 0;
    std::size_t nPos = 0;
    for (vertex_descriptor v : vertices(mesh))
    {
      const double s = static_cast<double>(get(map_g_, v));
      if (!(s > 0.0))
      {
        continue;
      }
      ++nPos;
      sMin = (std::min)(sMin, s);
      sMax = (std::max)(sMax, s);
      if (s <= static_cast<double>(short_g_) * 1.0000001)
      {
        ++nAtMin;
      }
      if (s >= static_cast<double>(long_g_) * 0.9999999)
      {
        ++nAtMax;
      }
    }
    std::size_t nTooLong = 0;
    std::size_t nTooShort = 0;
    for (CGAL_Surface::Edge_index e : mesh.edges())
    {
      const halfedge_descriptor h = mesh.halfedge(e);
      if (is_too_long(mesh.source(h), mesh.target(h), mesh))
      {
        ++nTooLong;
      }
      if (is_too_short(h, mesh))
      {
        ++nTooShort;
      }
    }

    {
      char buf[384];
      std::snprintf(buf, sizeof(buf),
        "SIZING done %.1fms (icc=%.1f fill=%.1f expand=%.1f scale=%.1f) nv=%zu ne=%zu nf=%zu "
        "R=%g min=%g max=%g | size[%.6g..%.6g] atMin=%zu(%.1f%%) atMax=%zu | "
        "edges tooLong=%zu(%.1f%%) tooShort=%zu(%.1f%%)",
        msSince(tAll), iccMs, fillMs, gradMs, scaleMs, nv, ne, nf,
        static_cast<double>(neighbor_max_ratio_), static_cast<double>(short_g_),
        static_cast<double>(long_g_),
        nPos ? sMin : 0.0, nPos ? sMax : 0.0, nAtMin,
        nPos ? (100.0 * static_cast<double>(nAtMin) / static_cast<double>(nPos)) : 0.0, nAtMax,
        nTooLong, ne ? (100.0 * static_cast<double>(nTooLong) / static_cast<double>(ne)) : 0.0,
        nTooShort, ne ? (100.0 * static_cast<double>(nTooShort) / static_cast<double>(ne)) : 0.0);
      if (sizingProfileTlsRunId() != 0 || sizingProfileVerbose())
      {
        sizingProfileLog(buf);
      }
    }
  }

  /** Unclamped ICC edge length; NaN when curvature is non-positive or the radicand is non-positive. */
  static FT uncapped_vertex_size_(FT tol, FT max_abs_curv)
  {
    if (!(max_abs_curv > FT(0)) || !(tol > FT(0)))
    {
      return std::numeric_limits<FT>::quiet_NaN();
    }
    const FT vsq = FT(6) * tol / max_abs_curv - FT(3) * tol * tol;
    if (!(vsq > FT(0)))
    {
      return std::numeric_limits<FT>::quiet_NaN();
    }
    return CGAL::approximate_sqrt(vsq);
  }

  static FT vertex_size_(FT tol, FT lo, FT hi, FT max_abs_curv)
  {
    if (max_abs_curv <= FT(0))
    {
      return hi;
    }
    const FT vsq = FT(6) * tol / max_abs_curv - FT(3) * tol * tol;
    if (vsq > hi * hi)
    {
      return hi;
    }
    if (vsq < lo * lo)
    {
      return lo;
    }
    return CGAL::approximate_sqrt(vsq);
  }

  /**
   * After ICC per-vertex targets, optionally relax sharp spatial jumps so along every mesh edge
   * neither endpoint exceeds R times the other (reduction only; non-positive map entries skipped).
   * R <= 1 disables.
   *
   * Default: multi-source multiplicative Dijkstra (exact fixed point in one propagation).
   *
   * TODO: implement graph-coloring Jacobi and/or Pointwise / vertex-block descent variants and
   * benchmark them against multi-source multiplicative Dijkstra (and the retained Gauss–Seidel
   * edge sweep below) for wall-clock time and sizing quality on large meshes.
   */
  void gradient_limit_vertex_sizes_(CGAL_Surface& mesh)
  {
    if (!(neighbor_max_ratio_ > FT(1)))
    {
      return;
    }
    gradient_limit_vertex_sizes_multiplicative_dijkstra_(mesh, neighbor_max_ratio_);
  }

  /**
   * Exact neighbor-ratio limit: \(s^*(v)=\min_u s_0(u)\,R^{d(u,v)}\) via multi-source Dijkstra
   * with multiplicative relaxations \(s(v)\leftarrow\min(s(v), R\cdot s(u))\). Smaller \(s\)
   * expand first (min-heap). Equivalent to additive Dijkstra on \(\log s\) with edge weight
   * \(\log R\).
   */
  void gradient_limit_vertex_sizes_multiplicative_dijkstra_(CGAL_Surface& mesh, FT R)
  {
    using Node = std::pair<FT, vertex_descriptor>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t nSeed = 0;
    for (vertex_descriptor v : vertices(mesh))
    {
      const FT s = get(map_g_, v);
      if (s > FT(0))
      {
        pq.push(Node(s, v));
        ++nSeed;
      }
    }

    std::size_t nPop = 0;
    std::size_t nStale = 0;
    std::size_t nRelax = 0;
    std::size_t maxHeap = pq.size();

    while (!pq.empty())
    {
      maxHeap = (std::max)(maxHeap, pq.size());
      const Node top = pq.top();
      pq.pop();
      ++nPop;
      const FT su = top.first;
      const vertex_descriptor u = top.second;
      if (su > get(map_g_, u))
      {
        ++nStale;
        continue;
      }
      for (halfedge_descriptor h : CGAL::halfedges_around_target(u, mesh))
      {
        const vertex_descriptor v = mesh.source(h);
        const FT sv = get(map_g_, v);
        if (!(sv > FT(0)))
        {
          continue;
        }
        const FT cand = su * R;
        if (cand < sv)
        {
          put(map_g_, v, cand);
          pq.push(Node(cand, v));
          ++nRelax;
        }
      }
    }

    if (sizingProfileVerbose())
    {
      char buf[256];
      std::snprintf(buf, sizeof(buf),
        "  expand(Dijkstra) %.1fms seeds=%zu pops=%zu stale=%zu relax=%zu maxHeap=%zu",
        msSince(t0), nSeed, nPop, nStale, nRelax, maxHeap);
      sizingProfileLog(buf);
    }
  }

  /**
   * Legacy iterative method: Gauss–Seidel sweeps over all edges (up to
   * gradient_limit_sweeps_), projecting each edge with limit_two_vertex_targets_. Kept for
   * reference / comparison with multiplicative Dijkstra and future coloring / VBD variants.
   */
  void gradient_limit_vertex_sizes_gauss_seidel_(CGAL_Surface& mesh, FT R)
  {
    const auto t0 = std::chrono::steady_clock::now();
    int sweepsRun = 0;
    for (int sweep = 0; sweep < gradient_limit_sweeps_; ++sweep)
    {
      bool changed = false;
      for (CGAL_Surface::Edge_index e : mesh.edges())
      {
        const halfedge_descriptor h = mesh.halfedge(e);
        const vertex_descriptor va = mesh.source(h);
        const vertex_descriptor vb = mesh.target(h);
        changed |= limit_two_vertex_targets_(map_g_, va, vb, R);
      }
      ++sweepsRun;
      if (!changed)
      {
        break;
      }
    }
    if (sizingProfileVerbose())
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "  expand(GaussSeidel) %.1fms sweeps=%d", msSince(t0),
        sweepsRun);
      sizingProfileLog(buf);
    }
  }

  static bool limit_two_vertex_targets_(
    VSizeMap& map, vertex_descriptor va, vertex_descriptor vb, FT R)
  {
    FT sa = get(map, va);
    FT sb = get(map, vb);
    if (!(sa > FT(0)) || !(sb > FT(0)))
    {
      return false;
    }
    bool changed = false;
    if (sa > R * sb)
    {
      put(map, va, R * sb);
      sa = R * sb;
      changed = true;
    }
    if (sb > R * sa)
    {
      put(map, vb, R * sa);
      changed = true;
    }
    return changed;
  }

  /**
   * Linearly remap all positive map_g_ values from their current [actual_min, actual_max] into
   * [short_g_, long_g_]. Vertices with value <= 0 (uncovered by the face range) are skipped.
   * No-op when scale_to_range_ is false or when actual_min == actual_max.
   */
  void scale_to_range_map_(CGAL_Surface& mesh)
  {
    if (!scale_to_range_)
    {
      return;
    }
    FT actual_min = std::numeric_limits<FT>::max();
    FT actual_max = FT(0);
    for (vertex_descriptor v : vertices(mesh))
    {
      const FT s = get(map_g_, v);
      if (s > FT(0))
      {
        actual_min = (std::min)(actual_min, s);
        actual_max = (std::max)(actual_max, s);
      }
    }
    if (!(actual_max > actual_min))
    {
      return;
    }
    const FT span_in  = actual_max - actual_min;
    const FT span_out = long_g_ - short_g_;
    for (vertex_descriptor v : vertices(mesh))
    {
      const FT s = get(map_g_, v);
      if (s > FT(0))
      {
        put(map_g_, v, short_g_ + (s - actual_min) * span_out / span_in);
      }
    }
  }

  VSizeMap map_g_;
  FT       tol_g_;
  FT       short_g_;
  FT       long_g_;
  FT       neighbor_max_ratio_;
  bool     scale_to_range_   = false;
  std::vector<FT>* uncapped_sizes_out_ = nullptr;
  static constexpr int gradient_limit_sweeps_ = 32;
};

} // namespace vespa_shyx_air_remesh_internals

#endif
