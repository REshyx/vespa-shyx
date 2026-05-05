#ifndef vtkSHYXFeatureAwareAdaptiveSizingField_h
#define vtkSHYXFeatureAwareAdaptiveSizingField_h

#include "vtkCGALHelper.h"

#include <CGAL/Kernel_traits.h>
#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <CGAL/Polygon_mesh_processing/interpolated_corrected_curvatures.h>
#include <CGAL/boost/graph/Face_filtered_graph.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/boost/graph/selection.h>
#include <CGAL/number_utils.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <optional>
#include <utility>
#include <vector>

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
 * Optional `use_polyline_curvature_for_feature`: after filling both maps from surface principal
 * curvatures, replace map_f_ at feature vertices with Dunyach sizing from **Menger curvature**
 * of the sharp polyline (max over neighbor pairs), treating creases as 3D curves. Degree-1
 * polyline vertices fall back to surface curvature.
 *
 * Between CGAL remesh iterations, `recompute_curvature(mesh)` may be called to run ICC again on the
 * **current full mesh** and refill both maps (polyline pass included when enabled). Patch remesh
 * still uses the expanded Face_filtered_graph only for the **initial** constructor pass; refresh
 * uses the whole surface so face indices stay valid after topology changes.
 *
 * ICC vertex normals: build property `v:vespa_icc_normal` with
 * `PrepareIccVertexNormalsForAdaptiveSizing` before constructing this object. Mask-adjacent
 * vertices use mask-face normals only (see vtkSHYXAdaptiveIsotropicRemesherInternals.h).
 * `recompute_curvature` refreshes that map with plain `compute_vertex_normals` (VTK mask indices
 * are not tracked across remesh topology changes).
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
    FeatureEdgeMap feat_map, bool use_polyline_curvature_for_feature)
    : feat_(feat_map)
    , tol_g_(tol_global)
    , short_g_(bounds_global.first)
    , long_g_(bounds_global.second)
    , tol_f_(tol_feature)
    , short_f_(bounds_feature.first)
    , long_f_(bounds_feature.second)
    , use_polyline_for_feature_(use_polyline_curvature_for_feature)
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
   * Re-run interpolated_corrected_curvatures on the current geometry and refill map_g_ / map_f_.
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
  /** Menger magnitude κ = 4A/(abc) for triangle (p_prev, p_mid, p_next); 0 if degenerate. */
  static FT menger_curve_curvature_mag(const Point_3& p_prev, const Point_3& p_mid,
    const Point_3& p_next)
  {
    const FT lab = CGAL::approximate_sqrt(CGAL::squared_distance(p_prev, p_mid));
    const FT lbc = CGAL::approximate_sqrt(CGAL::squared_distance(p_mid, p_next));
    const FT lac = CGAL::approximate_sqrt(CGAL::squared_distance(p_prev, p_next));
    const FT den = lab * lbc * lac;
    if (!(den > FT(1e-24)))
    {
      return FT(0);
    }
    const auto cp = CGAL::cross_product(p_prev - p_mid, p_next - p_mid);
    const FT area = FT(0.5) * CGAL::approximate_sqrt(FT(cp.squared_length()));
    return FT(4) * area / den;
  }

  template <typename Principal>
  static FT max_abs_principal_(const Principal& vc)
  {
    return (CGAL::max)(CGAL::abs(vc.max_curvature), CGAL::abs(vc.min_curvature));
  }

  template <typename FaceGraph>
  void compute_sizes_(FaceGraph& fg, CGAL_Surface& mesh)
  {
    using Kernel = typename CGAL::Kernel_traits<Point_3>::Kernel;
    using Principal = pmp_sf::Principal_curvatures_and_directions<Kernel>;
    using CTag = CGAL::dynamic_vertex_property_t<Principal>;
    auto curv_map = get(CTag(), fg);
    const auto vn_opt =
      mesh.property_map<vertex_descriptor, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");
    if (vn_opt.has_value())
    {
      pmp_sf::interpolated_corrected_curvatures(fg,
        pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map)
          .vertex_normal_map(*vn_opt));
    }
    else
    {
      pmp_sf::interpolated_corrected_curvatures(
        fg, pmp_sf::parameters::vertex_principal_curvatures_and_directions_map(curv_map));
    }

    for (auto v : vertices(fg))
    {
      const Principal vc = get(curv_map, v);
      const FT max_abs = max_abs_principal_(vc);
      put(map_g_, v, vertex_size_(tol_g_, short_g_, long_g_, max_abs));
      put(map_f_, v, vertex_size_(tol_f_, short_f_, long_f_, max_abs));
    }
    if (!use_polyline_for_feature_)
    {
      return;
    }
    for (auto v : vertices(fg))
    {
      if (!is_vertex_feature_(v, mesh))
      {
        continue;
      }
      const Principal vc = get(curv_map, v);
      const FT surf_k = max_abs_principal_(vc);
      FT k_eff = surf_k;
      std::vector<vertex_descriptor> fn;
      fn.reserve(8);
      const auto h0 = mesh.halfedge(v);
      if (h0 == CGAL_Surface::null_halfedge())
      {
        continue;
      }
      for (halfedge_descriptor h : CGAL::halfedges_around_target(h0, mesh))
      {
        if (boost::get(feat_, mesh.edge(h)))
        {
          fn.push_back(mesh.source(h));
        }
      }
      if (fn.size() >= 2)
      {
        FT k_poly = FT(0);
        for (std::size_t i = 0; i < fn.size(); ++i)
        {
          for (std::size_t j = i + 1; j < fn.size(); ++j)
          {
            if (fn[i] == fn[j])
            {
              continue;
            }
            const FT km =
              menger_curve_curvature_mag(mesh.point(fn[i]), mesh.point(v), mesh.point(fn[j]));
            k_poly = (CGAL::max)(k_poly, km);
          }
        }
        k_eff = k_poly;
      }
      put(map_f_, v, vertex_size_(tol_f_, short_f_, long_f_, k_eff));
    }
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

  using VSizeMap = typename CGAL_Surface::template Property_map<vertex_descriptor, FT>;

  FeatureEdgeMap feat_;
  VSizeMap map_g_;
  VSizeMap map_f_;
  FT tol_g_;
  FT short_g_;
  FT long_g_;
  FT tol_f_;
  FT short_f_;
  FT long_f_;
  bool use_polyline_for_feature_;

};

} // namespace vespa_shyx_air_remesh_internals

#endif
