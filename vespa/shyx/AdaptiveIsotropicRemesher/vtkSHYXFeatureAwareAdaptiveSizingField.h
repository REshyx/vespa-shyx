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

#include <optional>
#include <utility>
#include <vector>

#include "custom_interpolated_corrected_curvatures.h"

namespace vespa_shyx_air_remesh_internals
{
namespace pmp_sf = CGAL::Polygon_mesh_processing;

/**
 * Curvature-driven sizing field that holds TWO independent per-vertex target-length maps:
 * one for the global region and one for feature/constrained edges. Behaves like CGAL's
 * Adaptive_sizing_field except that the per-edge / per-vertex sizing is dispatched based on
 * whether the edge is in `featureEdges` (the same edge_is_constrained_map fed to
 * isotropic_remeshing).
 *
 * Why we cannot reuse two CGAL::Polygon_mesh_processing::Adaptive_sizing_field instances:
 * CGAL's class stores its per-vertex sizing in dynamic_vertex_property_t<FT>, which is
 * keyed only by the value type. Two instances on the same mesh therefore share storage
 * and the second-constructed field overwrites the first's targets, causing a global
 * over-refinement (and bad_alloc) when the feature targets are smaller than the global
 * targets.
 *
 * Implementation mirrors CGAL's Adaptive_sizing_field math (interpolated corrected
 * curvatures, vertex_size_sq = 6*tol/|kappa|max - 3*tol^2, clamped to [short, long]) but
 * stores the two per-vertex caches in DISTINCT named Surface_mesh property maps.
 *
 * Optional neighbor ratio limit: when @a neighbor_max_ratio > 1 in the constructor, targets are relaxed
 * so along every mesh edge neither endpoint exceeds R times the other (iterative symmetric reduction on
 * both named maps independently), damping sharp ICC-driven jumps across the surface. R <= 1 disables.
 *
 * Per-edge dispatch: is_too_long / is_too_short read both endpoints from the SAME map
 * (feature map if the edge is feature, global map otherwise). This keeps non-feature edges
 * fully decoupled from the feature targets even when they happen to share an endpoint with
 * a feature edge -- matching the user requirement that feature edges have their own
 * independent adaptive field.
 *
 * Copy semantics: the class must be copy-constructible because CGAL's Named_function_parameters
 * machinery (CGAL/Named_function_parameters.h:68) stores non-copyable parameters wrapped in
 * std::reference_wrapper<const T>, after which sizing.at(...) inside tangential_relaxation
 * fails to compile (reference_wrapper has no .at member). Default copy is fine: the only
 * non-trivial members are property-map handles that all refer to data living on the surface
 * mesh, exactly like CGAL's own Adaptive_sizing_field. The two named property maps are
 * therefore left on the mesh; they are cleaned up when the mesh itself is destroyed (each
 * RequestData call constructs a fresh CGAL_Surface via vtkCGALHelper, so nothing leaks
 * across runs).
 *
 * Between CGAL remesh iterations, `recompute_curvature(mesh)` may be called to run ICC again on the
 * **current full mesh** and refill both maps. Patch remesh still uses the expanded
 * Face_filtered_graph only for the **initial** constructor pass; refresh uses the whole surface so
 * face indices stay valid after topology changes.
 *
 * ICC vertex normals: build `v:vespa_icc_normal` (global area blend) plus, when the feature mask is
 * enabled, `f:vespa_icc_in_mask`, `v:vespa_icc_n_mask`, and `v:vespa_icc_n_nonmask` via
 * `PrepareIccVertexNormalsForAdaptiveSizing` before constructing this object. Interpolated corrected
 * curvature uses **per-face corner normals** consistent with each triangle's mask side so mask
 * interiors are not biased by boundary folds. With those maps loaded, **per-vertex** ICC aggregates
 * sum only **non-mask** incident faces (ball neighborhoods still traverse mask triangles without
 * counting them). `recompute_curvature` refreshes `v:vespa_icc_normal`
 * only with plain `compute_vertex_normals` (dual-region maps are not rebuilt across remesh topology).
 */
template <typename FeatureEdgeMap>
class FeatureAwareAdaptiveSizingField
{
public:
  using FT                  = double;
  using Point_3             = CGAL_Surface::Point;
  using vertex_descriptor   = CGAL_Surface::Vertex_index;
  using halfedge_descriptor = CGAL_Surface::Halfedge_index;
  using face_descriptor     = CGAL_Surface::Face_index;

  template <typename FaceRange>
  FeatureAwareAdaptiveSizingField(FT tol_global, std::pair<FT, FT> bounds_global, FT tol_feature,
    std::pair<FT, FT> bounds_feature, const FaceRange& face_range, CGAL_Surface& mesh,
    FeatureEdgeMap feat_map, FT neighbor_max_ratio = FT(0))
    : feat_(feat_map)
    , tol_g_(tol_global)
    , short_g_(bounds_global.first)
    , long_g_(bounds_global.second)
    , tol_f_(tol_feature)
    , short_f_(bounds_feature.first)
    , long_f_(bounds_feature.second)
    , neighbor_max_ratio_(neighbor_max_ratio)
  {
    map_g_ =
      mesh.template add_property_map<vertex_descriptor, FT>("v:vespa_size_global", FT(0)).first;
    map_f_ =
      mesh.template add_property_map<vertex_descriptor, FT>("v:vespa_size_feature", FT(0)).first;

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
  }

  /**
   * Re-run custom_interpolated_corrected_curvatures on the current geometry and refill map_g_ / map_f_.
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
   * Refill both target-length maps using the **current** `v:vespa_icc_normal` without modifying it.
   * Use after PrepareIccVertexNormalsForAdaptiveSizing (e.g. sizing ICC preview port) so capped
   * `v:vespa_size_*` match fresh normals; remesh-alone paths may leave maps stale relative to vn.
   */
  void recompute_sizes_from_current_icc_normals(CGAL_Surface& mesh) { compute_sizes_(mesh, mesh); }

  FT at(const vertex_descriptor v, const CGAL_Surface& sm) const
  {
    return is_vertex_feature_(v, sm) ? get(map_f_, v) : get(map_g_, v);
  }

  std::optional<FT> is_too_long(const vertex_descriptor va, const vertex_descriptor vb,
    const CGAL_Surface& sm) const
  {
    const bool feat = is_edge_feature_(va, vb, sm);
    const FT s = (CGAL::min)(get_size_(va, feat), get_size_(vb, feat));
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
    const bool feat = boost::get(feat_, sm.edge(h));
    const auto va = sm.source(h);
    const auto vb = sm.target(h);
    const FT s = (CGAL::min)(get_size_(va, feat), get_size_(vb, feat));
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
    FT sf = 0;
    std::size_t n = 0;
    for (halfedge_descriptor ha : CGAL::halfedges_around_target(v, sm))
    {
      sg += get(map_g_, sm.source(ha));
      sf += get(map_f_, sm.source(ha));
      ++n;
    }
    if (n > 0)
    {
      put(map_g_, v, sg / FT(n));
      put(map_f_, v, sf / FT(n));
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
    using Kernel = typename CGAL::Kernel_traits<Point_3>::Kernel;
    using Principal = vespa_shyx::Custom_principal_curvatures_and_directions<Kernel>;
    using CTag = CGAL::dynamic_vertex_property_t<Principal>;
    auto curv_map = get(CTag(), fg);
    const auto vn_opt =
      mesh.property_map<vertex_descriptor, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");
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

    for (auto v : vertices(fg))
    {
      const Principal vc = get(curv_map, v);
      const FT max_abs = max_abs_principal_(vc);
      put(map_g_, v, vertex_size_(tol_g_, short_g_, long_g_, max_abs));
      put(map_f_, v, vertex_size_(tol_f_, short_f_, long_f_, max_abs));
    }
    gradient_limit_vertex_sizes_(mesh);
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

  FT get_size_(vertex_descriptor v, bool feature) const
  {
    return feature ? get(map_f_, v) : get(map_g_, v);
  }

  bool is_vertex_feature_(vertex_descriptor v, const CGAL_Surface& sm) const
  {
    const auto h0 = sm.halfedge(v);
    if (h0 == CGAL_Surface::null_halfedge())
    {
      return false;
    }
    for (halfedge_descriptor h : CGAL::halfedges_around_target(h0, sm))
    {
      if (boost::get(feat_, sm.edge(h)))
      {
        return true;
      }
    }
    return false;
  }

  bool is_edge_feature_(vertex_descriptor va, vertex_descriptor vb, const CGAL_Surface& sm) const
  {
    for (halfedge_descriptor h : halfedges_around_source(va, sm))
    {
      if (sm.target(h) == vb)
      {
        return boost::get(feat_, sm.edge(h));
      }
    }
    return false;
  }

  /**
   * After ICC per-vertex targets, optionally relax sharp spatial jumps: along every edge, neither
   * endpoint map value may exceed R times the other's (symmetric reduction only). Vertices with
   * non-positive map entries are skipped (patch-uncovered defaults). R <= 1 disables.
   *
   * Multiple passes approximate a gradient cap on log-size; tighter R needs more iterations but
   * 32 sweeps are ample for typical meshes.
   */
  void gradient_limit_vertex_sizes_(CGAL_Surface& mesh)
  {
    if (!(neighbor_max_ratio_ > FT(1)))
    {
      return;
    }
    const FT R = neighbor_max_ratio_;
    for (int sweep = 0; sweep < gradient_limit_sweeps_; ++sweep)
    {
      bool changed = false;
      for (CGAL_Surface::Edge_index e : mesh.edges())
      {
        const halfedge_descriptor h = mesh.halfedge(e);
        const vertex_descriptor va = mesh.source(h);
        const vertex_descriptor vb = mesh.target(h);
        changed |= limit_two_vertex_targets_(map_g_, va, vb, R);
        changed |= limit_two_vertex_targets_(map_f_, va, vb, R);
      }
      if (!changed)
      {
        break;
      }
    }
  }

  static bool limit_two_vertex_targets_(VSizeMap& map, vertex_descriptor va, vertex_descriptor vb, FT R)
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

  FeatureEdgeMap feat_;
  VSizeMap map_g_;
  VSizeMap map_f_;
  FT tol_g_;
  FT short_g_;
  FT long_g_;
  FT tol_f_;
  FT short_f_;
  FT long_f_;
  FT neighbor_max_ratio_;
  static constexpr int gradient_limit_sweeps_ = 32;

};

} // namespace vespa_shyx_air_remesh_internals

#endif
