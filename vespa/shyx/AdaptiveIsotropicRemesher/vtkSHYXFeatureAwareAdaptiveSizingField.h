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
#include <limits>
#include <optional>
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
 * Optional neighbor ratio limit: when @a neighbor_max_ratio > 1 in the constructor, targets are
 * relaxed so along every mesh edge neither endpoint exceeds R times the other (iterative symmetric
 * reduction), damping sharp ICC-driven jumps. R <= 1 disables.
 *
 * Optional scale-to-range: when @a scale_to_range is true, after all per-vertex targets (and
 * neighbor ratio limiting) are computed, the values are linearly remapped so the actual min maps
 * to short (MinEdgeLength) and the actual max maps to long (MaxEdgeLength). Vertices with a
 * zero-initialised target (uncovered by the face range) are excluded from the min/max scan and
 * left at zero. This stretches the curvature-derived distribution to fill the full sizing interval.
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
    CGAL_Surface& mesh, FT neighbor_max_ratio = FT(0), bool scale_to_range = false)
    : tol_g_(tol)
    , short_g_(bounds.first)
    , long_g_(bounds.second)
    , neighbor_max_ratio_(neighbor_max_ratio)
    , scale_to_range_(scale_to_range)
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
      const Principal vc  = get(curv_map, v);
      const FT        max_abs = max_abs_principal_(vc);
      put(map_g_, v, vertex_size_(tol_g_, short_g_, long_g_, max_abs));
    }
    gradient_limit_vertex_sizes_(mesh);
    scale_to_range_map_(mesh);
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
   * After ICC per-vertex targets, optionally relax sharp spatial jumps: along every edge, neither
   * endpoint map value may exceed R times the other's (symmetric reduction only). Vertices with
   * non-positive map entries are skipped (patch-uncovered defaults). R <= 1 disables.
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
        const halfedge_descriptor h  = mesh.halfedge(e);
        const vertex_descriptor   va = mesh.source(h);
        const vertex_descriptor   vb = mesh.target(h);
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

  VSizeMap map_g_;
  FT       tol_g_;
  FT       short_g_;
  FT       long_g_;
  FT       neighbor_max_ratio_;
  bool     scale_to_range_   = false;
  static constexpr int gradient_limit_sweeps_ = 32;
};

} // namespace vespa_shyx_air_remesh_internals

#endif
