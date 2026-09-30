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
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include "custom_interpolated_corrected_curvatures.h"

namespace vespa_shyx_air_remesh_internals
{
namespace pmp_sf = CGAL::Polygon_mesh_processing;

/**
 * Curvature-driven sizing field with a single per-vertex target-length map
 * (`v:vespa_size_global`). Mirrors CGAL's Adaptive_sizing_field math
 * (interpolated corrected curvatures, vertex_size_sq = 6*tol/|kappa|max - 3*tol^2, clamped to
 * [short, long]) but stores the cache in a **distinct named** Surface_mesh property map to avoid
 * the dynamic_vertex_property_t key collision in CGAL's own class.
 *
 * Optional ICC neighborhood (mutually exclusive):
 * - @a icc_neighborhood_rings **>= 1**: sum face measures over a **k-ring** of faces (hop 1 =
 *   incident faces). Euclidean `ball_radius` is ignored.
 * - @a icc_neighborhood_rings **== 0**: use ICC `ball_radius` (@a icc_ball_radius). Negative
 *   (default **-1**) is incident faces only. **0** becomes a tiny epsilon times average edge
 *   length. **> 0** is a Euclidean ball (model units) with faces weighted by inclusion ratio.
 *
 * This is the curvature-estimation scale; it is not Expansion ratio (`neighbor_max_ratio`), which
 * only limits adjacent *sizing* jumps after targets are computed.
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
 * Optional size-field scale: when @a size_field_scale is not 1, every positive target is multiplied
 * by that factor **after** clamp, neighbor-ratio limiting, and scale-to-range. The result is **not**
 * re-clamped to [short, long], so the field can leave the Min/Max interval. **1** (default) is a
 * no-op. Applied again on every `recompute_curvature` / `recompute_sizes_from_current_icc_normals`.
 *
 * Optional boundary anchor: when @a anchor_boundary_size is true, open-boundary vertices are not
 * used as Expansion-ratio seeds (their ICC value is discarded). After scale, each border vertex is
 * set to the mean length of its border edges, and every other vertex is clamped into
 * \f$[L/R^{d}, L R^{d}]\f$ of those anchors (\f$R\f$ = neighbor_max_ratio, \f$d\f$ = hop distance).
 * \f$R \le 1\f$ only rewrites the border vertices. The anchor is the geometric edge length, not
 * multiplied by size_field_scale. When @a boundary_size_scale is not 1, each opening loop's anchor
 * is that loop's mean border-edge length times the factor (\f$> 1\f$ coarsens, \f$< 1\f$ refines).
 * The absolute target is taken from the input mesh and reused on later `recompute_curvature`
 * passes, so the factor is not compounded across remesh iterations.
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
 * Callers that remesh more than once on the same Surface_mesh must \c collect_garbage() between
 * remesh/recompute passes; otherwise the next isotropic_remeshing can hang (see
 * AdaptiveIsotropicRemesher/README.md §3).
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
    std::vector<FT>* uncapped_sizes_out = nullptr, FT icc_ball_radius = FT(-1),
    int icc_neighborhood_rings = 0, FT size_field_scale = FT(1), bool anchor_boundary_size = false,
    FT boundary_size_scale = FT(1))
    : tol_g_(tol)
    , short_g_(bounds.first)
    , long_g_(bounds.second)
    , neighbor_max_ratio_(neighbor_max_ratio)
    , scale_to_range_(scale_to_range)
    , uncapped_sizes_out_(uncapped_sizes_out)
    , icc_ball_radius_(icc_ball_radius)
    , icc_neighborhood_rings_(icc_neighborhood_rings > 0 ? icc_neighborhood_rings : 0)
    , size_field_scale_(size_field_scale)
    , anchor_boundary_size_(anchor_boundary_size)
    , boundary_size_scale_(boundary_size_scale > FT(0) ? boundary_size_scale : FT(1))
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
    const auto vn_opt =
      mesh.property_map<vertex_descriptor, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");
    if (vn_opt.has_value())
    {
      pmp_sf::compute_vertex_normals(mesh, *vn_opt);
    }
    compute_sizes_(mesh, mesh);
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
    auto curv_map   = get(CTag(), fg);
    const auto vn_opt =
      mesh.property_map<vertex_descriptor, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");
    const FT radiusForIcc =
      (icc_neighborhood_rings_ >= 1) ? FT(-1) : icc_ball_radius_;
    if (vn_opt.has_value())
    {
      vespa_shyx::custom_interpolated_corrected_curvatures(fg,
        pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map)
          .vertex_normal_map(*vn_opt)
          .ball_radius(radiusForIcc),
        &mesh, icc_neighborhood_rings_);
    }
    else
    {
      vespa_shyx::custom_interpolated_corrected_curvatures(fg,
        pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map)
          .ball_radius(radiusForIcc),
        &mesh, icc_neighborhood_rings_);
    }

    if (uncapped_sizes_out_)
    {
      const FT nan = std::numeric_limits<FT>::quiet_NaN();
      uncapped_sizes_out_->assign(static_cast<std::size_t>(mesh.number_of_vertices()), nan);
    }

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
    // Lip curvature must not seed Expansion ratio; those vertices are overwritten with
    // geometric border length after scaling.
    if (anchor_boundary_size_)
    {
      clear_border_vertex_sizes_(mesh);
    }
    gradient_limit_vertex_sizes_(mesh);
    scale_to_range_map_(mesh);
    apply_size_field_scale_(mesh);
    if (anchor_boundary_size_)
    {
      anchor_boundary_sizes_(mesh);
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

    for (vertex_descriptor v : vertices(mesh))
    {
      const FT s = get(map_g_, v);
      if (s > FT(0))
      {
        pq.push(Node(s, v));
      }
    }

    while (!pq.empty())
    {
      const Node top = pq.top();
      pq.pop();
      const FT su = top.first;
      const vertex_descriptor u = top.second;
      // Stale heap entry after a better (smaller) value was written.
      if (su > get(map_g_, u))
      {
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
        }
      }
    }
  }

  /**
   * Legacy iterative method: Gauss–Seidel sweeps over all edges (up to
   * gradient_limit_sweeps_), projecting each edge with limit_two_vertex_targets_. Kept for
   * reference / comparison with multiplicative Dijkstra and future coloring / VBD variants.
   */
  void gradient_limit_vertex_sizes_gauss_seidel_(CGAL_Surface& mesh, FT R)
  {
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
      if (!changed)
      {
        break;
      }
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

  /** Set every open-boundary vertex target to 0 so it is skipped by later sizing passes. */
  void clear_border_vertex_sizes_(CGAL_Surface& mesh)
  {
    for (CGAL_Surface::Edge_index e : mesh.edges())
    {
      const halfedge_descriptor h = mesh.halfedge(e);
      if (!mesh.is_border(h) && !mesh.is_border(mesh.opposite(h)))
      {
        continue;
      }
      put(map_g_, mesh.source(h), FT(0));
      put(map_g_, mesh.target(h), FT(0));
    }
  }

  struct BorderLoopTarget
  {
    FT cx = 0;
    FT cy = 0;
    FT cz = 0;
    FT target = 0;
  };

  /**
   * Set each opening loop to (mean border-edge length * boundary_size_scale_). The absolute
   * target is captured on the first call (input mesh) and reused later so remesh iterations
   * do not compound the factor. Loops are matched by centroid.
   */
  bool assign_scaled_border_loop_targets_(CGAL_Surface& mesh)
  {
    struct Loop
    {
      std::vector<vertex_descriptor> verts;
      FT lenSum = 0;
      int nEdges = 0;
      FT cx = 0;
      FT cy = 0;
      FT cz = 0;
    };

    std::vector<Loop> loops;
    std::set<halfedge_descriptor> visited;
    for (halfedge_descriptor h : mesh.halfedges())
    {
      if (!mesh.is_border(h) || visited.find(h) != visited.end())
      {
        continue;
      }
      Loop loop;
      halfedge_descriptor cur = h;
      do
      {
        visited.insert(cur);
        const vertex_descriptor va = mesh.source(cur);
        const vertex_descriptor vb = mesh.target(cur);
        loop.verts.push_back(vb);
        const Point_3& pa = mesh.point(va);
        const Point_3& pb = mesh.point(vb);
        const FT len = CGAL::approximate_sqrt(CGAL::squared_distance(pa, pb));
        if (len > FT(0))
        {
          loop.lenSum += len;
          ++loop.nEdges;
        }
        cur = mesh.next(cur);
      } while (cur != h);
      if (loop.nEdges <= 0 || loop.verts.empty())
      {
        continue;
      }
      for (vertex_descriptor v : loop.verts)
      {
        const Point_3& p = mesh.point(v);
        loop.cx += p.x();
        loop.cy += p.y();
        loop.cz += p.z();
      }
      const FT n = static_cast<FT>(loop.verts.size());
      loop.cx /= n;
      loop.cy /= n;
      loop.cz /= n;
      loops.push_back(std::move(loop));
    }
    if (loops.empty())
    {
      return false;
    }

    const bool capture = border_loop_targets_.empty();
    if (capture)
    {
      border_loop_targets_.reserve(loops.size());
    }
    for (const Loop& loop : loops)
    {
      FT target = 0;
      if (capture)
      {
        target = (loop.lenSum / static_cast<FT>(loop.nEdges)) * boundary_size_scale_;
        border_loop_targets_.push_back(BorderLoopTarget{ loop.cx, loop.cy, loop.cz, target });
      }
      else
      {
        FT bestD = std::numeric_limits<FT>::infinity();
        std::size_t best = 0;
        for (std::size_t i = 0; i < border_loop_targets_.size(); ++i)
        {
          const FT dx = loop.cx - border_loop_targets_[i].cx;
          const FT dy = loop.cy - border_loop_targets_[i].cy;
          const FT dz = loop.cz - border_loop_targets_[i].cz;
          const FT d = dx * dx + dy * dy + dz * dz;
          if (d < bestD)
          {
            bestD = d;
            best = i;
          }
        }
        target = border_loop_targets_[best].target;
      }
      for (vertex_descriptor v : loop.verts)
      {
        put(map_g_, v, target);
      }
    }
    return true;
  }

  /**
   * Write each border vertex to the mean length of its border edges, then clamp every reached
   * vertex into the Expansion-ratio band of those anchors. R <= 1 only rewrites the border.
   */
  void anchor_boundary_sizes_(CGAL_Surface& mesh)
  {
    std::size_t maxIdx = 0;
    for (vertex_descriptor v : vertices(mesh))
    {
      maxIdx = (std::max)(maxIdx, static_cast<std::size_t>(v.idx()));
    }
    std::vector<FT> lenSum(maxIdx + 1, FT(0));
    std::vector<int> lenCnt(maxIdx + 1, 0);

    for (CGAL_Surface::Edge_index e : mesh.edges())
    {
      const halfedge_descriptor h = mesh.halfedge(e);
      if (!mesh.is_border(h) && !mesh.is_border(mesh.opposite(h)))
      {
        continue;
      }
      const vertex_descriptor va = mesh.source(h);
      const vertex_descriptor vb = mesh.target(h);
      const FT len = CGAL::approximate_sqrt(CGAL::squared_distance(mesh.point(va), mesh.point(vb)));
      if (!(len > FT(0)))
      {
        continue;
      }
      const std::size_t ia = static_cast<std::size_t>(va.idx());
      const std::size_t ib = static_cast<std::size_t>(vb.idx());
      lenSum[ia] += len;
      lenSum[ib] += len;
      ++lenCnt[ia];
      ++lenCnt[ib];
    }

    bool anyBorder = false;
    const bool useScale = boundary_size_scale_ > FT(0) && boundary_size_scale_ != FT(1);
    if (useScale)
    {
      anyBorder = assign_scaled_border_loop_targets_(mesh);
    }
    else
    {
      for (vertex_descriptor v : vertices(mesh))
      {
        const std::size_t i = static_cast<std::size_t>(v.idx());
        if (lenCnt[i] > 0)
        {
          put(map_g_, v, lenSum[i] / FT(lenCnt[i]));
          anyBorder = true;
        }
      }
    }
    if (!anyBorder || !(neighbor_max_ratio_ > FT(1)))
    {
      return;
    }

    const FT R = neighbor_max_ratio_;
    const FT inf = std::numeric_limits<FT>::infinity();
    std::vector<FT> upper(maxIdx + 1, inf);
    std::vector<FT> lower(maxIdx + 1, FT(0));
    using Node = std::pair<FT, std::size_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> upperHeap;
    std::priority_queue<Node> lowerHeap;

    for (vertex_descriptor v : vertices(mesh))
    {
      const std::size_t i = static_cast<std::size_t>(v.idx());
      if (lenCnt[i] <= 0)
      {
        continue;
      }
      const FT s = get(map_g_, v);
      upper[i] = s;
      lower[i] = s;
      upperHeap.push(Node(s, i));
      lowerHeap.push(Node(s, i));
    }

    std::vector<vertex_descriptor> byIdx(maxIdx + 1);
    for (vertex_descriptor v : vertices(mesh))
    {
      byIdx[static_cast<std::size_t>(v.idx())] = v;
    }

    auto relaxTo = [&](std::size_t iu, FT cand, bool takeMin) {
      const vertex_descriptor u = byIdx[iu];
      for (halfedge_descriptor h : CGAL::halfedges_around_target(u, mesh))
      {
        const vertex_descriptor v = mesh.source(h);
        const std::size_t iv = static_cast<std::size_t>(v.idx());
        if (takeMin)
        {
          if (cand < upper[iv])
          {
            upper[iv] = cand;
            upperHeap.push(Node(cand, iv));
          }
        }
        else if (cand > lower[iv])
        {
          lower[iv] = cand;
          lowerHeap.push(Node(cand, iv));
        }
      }
    };

    while (!upperHeap.empty())
    {
      const Node top = upperHeap.top();
      upperHeap.pop();
      if (top.first > upper[top.second])
      {
        continue;
      }
      relaxTo(top.second, top.first * R, true);
    }
    while (!lowerHeap.empty())
    {
      const Node top = lowerHeap.top();
      lowerHeap.pop();
      if (top.first < lower[top.second])
      {
        continue;
      }
      relaxTo(top.second, top.first / R, false);
    }

    for (vertex_descriptor v : vertices(mesh))
    {
      const std::size_t i = static_cast<std::size_t>(v.idx());
      const FT hi = upper[i];
      const FT lo = lower[i];
      if (!(hi > FT(0)) || !(hi < inf))
      {
        continue;
      }
      FT s = get(map_g_, v);
      if (lo > hi)
      {
        s = CGAL::approximate_sqrt(lo * hi);
      }
      else
      {
        if (s < lo)
        {
          s = lo;
        }
        if (s > hi)
        {
          s = hi;
        }
      }
      put(map_g_, v, s);
    }
  }

  /**
   * Multiply every positive map_g_ value by size_field_scale_. No-op when the factor is 1 or
   * non-positive. Does not re-clamp to [short_g_, long_g_].
   */
  void apply_size_field_scale_(CGAL_Surface& mesh)
  {
    if (!(size_field_scale_ > FT(0)) || size_field_scale_ == FT(1))
    {
      return;
    }
    for (vertex_descriptor v : vertices(mesh))
    {
      const FT s = get(map_g_, v);
      if (s > FT(0))
      {
        put(map_g_, v, s * size_field_scale_);
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
  FT       icc_ball_radius_ = FT(-1);
  int      icc_neighborhood_rings_ = 0;
  FT       size_field_scale_ = FT(1);
  bool     anchor_boundary_size_ = false;
  FT       boundary_size_scale_ = FT(1);
  std::vector<BorderLoopTarget> border_loop_targets_;
  static constexpr int gradient_limit_sweeps_ = 32;
};

} // namespace vespa_shyx_air_remesh_internals

#endif
