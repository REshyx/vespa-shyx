#include "vtkSHYXMeshSimplification.h"

#include "vtkCGALHelper.h"

#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Bounded_normal_change_placement.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Constrained_placement.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_count_ratio_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_count_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_length_cost.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_length_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_plane_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_probabilistic_triangle_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_triangle_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/LindstromTurk.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Midpoint_placement.h>
#include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/number_utils.h>
#include <CGAL/property_map.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>

vtkStandardNewMacro(vtkSHYXMeshSimplification);

namespace SMS = CGAL::Surface_mesh_simplification;
namespace PMP = CGAL::Polygon_mesh_processing;

namespace
{

using Edge_bool_map = CGAL_Surface::Property_map<CGAL_Surface::Edge_index, bool>;

struct SimplifyOptions
{
  int Algorithm = 0;
  int EndpointRanking = 0;
  bool PreventNormalFlip = true;
  double VolumeWeight = 0.5;
  double BoundaryWeight = 0.5;
  double ShapeWeight = 0.0;
  double DiscontinuityMultiplier = 100.0;
};

bool VertexIncidentToConstrainedEdge(
  const CGAL_Surface& mesh, Edge_bool_map ecm, CGAL_Surface::Vertex_index v)
{
  for (const auto h : CGAL::halfedges_around_target(v, mesh))
  {
    if (get(ecm, edge(h, mesh)))
    {
      return true;
    }
  }
  return false;
}

/**
 * -1 = both endpoints constrained (refuse); 0 = must keep v0; 1 = must keep v1;
 * 2 = free to pick by cost.
 */
template <typename Profile>
int ConstrainedKeepIndex(const Profile& profile, Edge_bool_map ecm)
{
  const CGAL_Surface& mesh = profile.surface_mesh();
  const bool c0 = VertexIncidentToConstrainedEdge(mesh, ecm, profile.v0());
  const bool c1 = VertexIncidentToConstrainedEdge(mesh, ecm, profile.v1());
  if (c0 && c1)
  {
    return -1;
  }
  if (c0)
  {
    return 0;
  }
  if (c1)
  {
    return 1;
  }
  return 2;
}

template <typename Point, typename GeomTraits>
std::optional<typename GeomTraits::FT> TriangleMinAngle(
  const Point& a, const Point& b, const Point& c, const GeomTraits& gt)
{
  using FT = typename GeomTraits::FT;
  const auto vec = gt.construct_vector_3_object();
  const auto dot = gt.compute_scalar_product_3_object();
  const auto slen = gt.compute_squared_length_3_object();

  const auto ab = vec(a, b);
  const auto ac = vec(a, c);
  const auto bc = vec(b, c);
  const FT lab2 = slen(ab);
  const FT lac2 = slen(ac);
  const FT lbc2 = slen(bc);
  if (!(lab2 > FT(0)) || !(lac2 > FT(0)) || !(lbc2 > FT(0)))
  {
    return std::nullopt;
  }

  const auto angleAt = [&](const auto& u, const auto& v, const FT u2, const FT v2) -> std::optional<FT> {
    const double denom = std::sqrt(CGAL::to_double(u2) * CGAL::to_double(v2));
    if (!(denom > 0.0))
    {
      return std::nullopt;
    }
    double cosav = CGAL::to_double(dot(u, v)) / denom;
    cosav = std::min(1.0, std::max(-1.0, cosav));
    return FT(std::acos(cosav));
  };

  const auto ba = vec(b, a);
  const auto ca = vec(c, a);
  const auto cb = vec(c, b);
  const auto angA = angleAt(ab, ac, lab2, lac2);
  const auto angB = angleAt(ba, bc, lab2, lbc2);
  const auto angC = angleAt(ca, cb, lac2, lbc2);
  if (!angA || !angB || !angC)
  {
    return std::nullopt;
  }
  return std::min(*angA, std::min(*angB, *angC));
}

/**
 * Minimum interior angle among triangles that remain after collapsing the profile edge
 * onto q2. Refuses zero-area or orientation-reversing triangles.
 */
template <typename Profile>
std::optional<typename Profile::FT> MinAngleAfterCollapse(
  const Profile& profile, const typename Profile::Point& q2)
{
  using FT = typename Profile::FT;
  using Point = typename Profile::Point;

  const auto& triangles = profile.triangles();
  auto it = triangles.begin();
  if (profile.left_face_exists())
  {
    ++it;
  }
  if (profile.right_face_exists())
  {
    ++it;
  }
  if (it == triangles.end())
  {
    return std::nullopt;
  }

  const auto& gt = profile.geom_traits();
  const auto vpm = profile.vertex_point_map();
  const auto vec = gt.construct_vector_3_object();
  const auto cross = gt.construct_cross_product_vector_3_object();
  const auto dot = gt.compute_scalar_product_3_object();

  FT minA = FT(0);
  bool any = false;
  for (; it != triangles.end(); ++it)
  {
    const Point p = get(vpm, it->v0);
    const Point q = get(vpm, it->v1);
    const Point r = get(vpm, it->v2);
    const auto n1 = cross(vec(q, p), vec(q, r));
    const auto n2 = cross(vec(q2, p), vec(q2, r));
    if (!CGAL::is_positive(dot(n1, n2)))
    {
      return std::nullopt;
    }
    const auto ang = TriangleMinAngle(p, q2, r, gt);
    if (!ang)
    {
      return std::nullopt;
    }
    if (!any || *ang < minA)
    {
      minA = *ang;
    }
    any = true;
  }
  if (!any)
  {
    return std::nullopt;
  }
  return minA;
}

/**
 * Placement is always one of the two original endpoints. If exactly one endpoint is
 * incident to a constrained edge, that endpoint is kept. If both are, refuse.
 * Otherwise pick the endpoint with the smaller Garland–Heckbert plane-quadric cost.
 */
class QemEndpointPlacement
{
public:
  using GH = SMS::GarlandHeckbert_plane_policies<CGAL_Surface, CGAL_Kernel>;

  QemEndpointPlacement(const GH& gh, Edge_bool_map ecm)
    : Gh(&gh)
    , Ecm(ecm)
  {
  }

  template <typename Profile>
  std::optional<typename Profile::Point> operator()(const Profile& profile) const
  {
    using Point = typename Profile::Point;
    const int keep = ConstrainedKeepIndex(profile, this->Ecm);
    if (keep < 0)
    {
      return std::nullopt;
    }
    if (keep == 0)
    {
      return profile.p0();
    }
    if (keep == 1)
    {
      return profile.p1();
    }

    const auto cost0 = (*this->Gh)(profile, std::optional<Point>(profile.p0()));
    const auto cost1 = (*this->Gh)(profile, std::optional<Point>(profile.p1()));
    if (cost0 && cost1)
    {
      return (*cost0 <= *cost1) ? profile.p0() : profile.p1();
    }
    if (cost0)
    {
      return profile.p0();
    }
    if (cost1)
    {
      return profile.p1();
    }
    return std::nullopt;
  }

private:
  const GH* Gh = nullptr;
  Edge_bool_map Ecm;
};

/**
 * Same constrained-endpoint rules; otherwise keep the endpoint that yields the larger
 * minimum angle among remaining star triangles.
 */
class MinAngleEndpointPlacement
{
public:
  explicit MinAngleEndpointPlacement(Edge_bool_map ecm)
    : Ecm(ecm)
  {
  }

  template <typename Profile>
  std::optional<typename Profile::Point> operator()(const Profile& profile) const
  {
    using Point = typename Profile::Point;
    const int keep = ConstrainedKeepIndex(profile, this->Ecm);
    if (keep < 0)
    {
      return std::nullopt;
    }
    if (keep == 0)
    {
      return MinAngleAfterCollapse(profile, profile.p0()) ? std::optional<Point>(profile.p0())
                                                          : std::nullopt;
    }
    if (keep == 1)
    {
      return MinAngleAfterCollapse(profile, profile.p1()) ? std::optional<Point>(profile.p1())
                                                          : std::nullopt;
    }

    const auto a0 = MinAngleAfterCollapse(profile, profile.p0());
    const auto a1 = MinAngleAfterCollapse(profile, profile.p1());
    if (a0 && a1)
    {
      return (*a0 >= *a1) ? profile.p0() : profile.p1();
    }
    if (a0)
    {
      return profile.p0();
    }
    if (a1)
    {
      return profile.p1();
    }
    return std::nullopt;
  }

private:
  Edge_bool_map Ecm;
};

struct MinAngleCost
{
  template <typename Profile>
  std::optional<typename Profile::FT> operator()(
    const Profile& profile, const std::optional<typename Profile::Point>& placement) const
  {
    if (!placement)
    {
      return std::nullopt;
    }
    const auto minA = MinAngleAfterCollapse(profile, *placement);
    if (!minA)
    {
      return std::nullopt;
    }
    return -(*minA);
  }
};

/**
 * Relocating placement. Stock Constrained_placement keeps v0 when both endpoints touch a
 * constrained edge; this refuses that collapse, then delegates (one pinned endpoint stays,
 * otherwise the base placement runs).
 */
template <typename BasePlacement>
class PinnedPlacement
{
public:
  PinnedPlacement(Edge_bool_map ecm, const BasePlacement& base)
    : Ecm(ecm)
    , Inner(ecm, base)
  {
  }

  template <typename Profile>
  std::optional<typename Profile::Point> operator()(const Profile& profile) const
  {
    if (ConstrainedKeepIndex(profile, this->Ecm) < 0)
    {
      return std::nullopt;
    }
    return this->Inner(profile);
  }

private:
  Edge_bool_map Ecm;
  SMS::Constrained_placement<BasePlacement, Edge_bool_map> Inner;
};

template <typename Stop, typename Cost, typename Placement>
int CollapseEdges(CGAL_Surface& mesh, const Stop& stop, const Cost& cost, const Placement& placement,
  Edge_bool_map ecm, bool preventFlip)
{
  if (preventFlip)
  {
    SMS::Bounded_normal_change_placement<Placement> filtered(placement);
    return static_cast<int>(SMS::edge_collapse(mesh, stop,
      CGAL::parameters::get_cost(cost).get_placement(filtered).edge_is_constrained_map(ecm)));
  }
  return static_cast<int>(SMS::edge_collapse(mesh, stop,
    CGAL::parameters::get_cost(cost).get_placement(placement).edge_is_constrained_map(ecm)));
}

template <typename Policy, typename Stop>
int CollapseQuadric(CGAL_Surface& mesh, const Stop& stop, Edge_bool_map ecm, double discontinuity, bool preventFlip)
{
  Policy gh(mesh, static_cast<CGAL_Kernel::FT>(discontinuity));
  PinnedPlacement<Policy> pinned(ecm, gh.get_placement());
  return CollapseEdges(mesh, stop, gh.get_cost(), pinned, ecm, preventFlip);
}

template <typename Stop>
int Run(CGAL_Surface& mesh, const Stop& stop, Edge_bool_map ecm, const SimplifyOptions& opt)
{
  switch (opt.Algorithm)
  {
    case vtkSHYXMeshSimplification::LINDSTROM_TURK:
    {
      using Params = SMS::LindstromTurk_cost<CGAL_Surface>::LindstromTurk_params;
      const Params params(opt.VolumeWeight, opt.BoundaryWeight, opt.ShapeWeight);
      SMS::LindstromTurk_cost<CGAL_Surface> cost(params);
      SMS::LindstromTurk_placement<CGAL_Surface> base(params);
      PinnedPlacement<SMS::LindstromTurk_placement<CGAL_Surface>> pinned(ecm, base);
      return CollapseEdges(mesh, stop, cost, pinned, ecm, opt.PreventNormalFlip);
    }
    case vtkSHYXMeshSimplification::GARLAND_HECKBERT_PLANE:
      return CollapseQuadric<SMS::GarlandHeckbert_plane_policies<CGAL_Surface, CGAL_Kernel>>(
        mesh, stop, ecm, opt.DiscontinuityMultiplier, opt.PreventNormalFlip);
    case vtkSHYXMeshSimplification::GARLAND_HECKBERT_TRIANGLE:
      return CollapseQuadric<SMS::GarlandHeckbert_triangle_policies<CGAL_Surface, CGAL_Kernel>>(
        mesh, stop, ecm, opt.DiscontinuityMultiplier, opt.PreventNormalFlip);
    case vtkSHYXMeshSimplification::GARLAND_HECKBERT_PROB_TRIANGLE:
      return CollapseQuadric<
        SMS::GarlandHeckbert_probabilistic_triangle_policies<CGAL_Surface, CGAL_Kernel>>(
        mesh, stop, ecm, opt.DiscontinuityMultiplier, opt.PreventNormalFlip);
    case vtkSHYXMeshSimplification::EDGE_LENGTH_MIDPOINT:
    {
      SMS::Edge_length_cost<CGAL_Surface> cost;
      SMS::Midpoint_placement<CGAL_Surface> base;
      PinnedPlacement<SMS::Midpoint_placement<CGAL_Surface>> pinned(ecm, base);
      return CollapseEdges(mesh, stop, cost, pinned, ecm, opt.PreventNormalFlip);
    }
    case vtkSHYXMeshSimplification::ENDPOINT_SUBSET:
    {
      if (opt.EndpointRanking == vtkSHYXMeshSimplification::ENDPOINT_MIN_ANGLE)
      {
        MinAngleEndpointPlacement place(ecm);
        const MinAngleCost cost;
        return CollapseEdges(mesh, stop, cost, place, ecm, opt.PreventNormalFlip);
      }
      if (opt.EndpointRanking != vtkSHYXMeshSimplification::ENDPOINT_PLANE_QEM)
      {
        return -1;
      }
      QemEndpointPlacement::GH gh(mesh);
      QemEndpointPlacement place(gh, ecm);
      return CollapseEdges(mesh, stop, gh.get_cost(), place, ecm, opt.PreventNormalFlip);
    }
    default:
      return -1;
  }
}

} // namespace

//------------------------------------------------------------------------------
void vtkSHYXMeshSimplification::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "Algorithm: " << this->Algorithm << std::endl;
  os << indent << "StopCriterion: " << this->StopCriterion << std::endl;
  os << indent << "EdgeCountRatio: " << this->EdgeCountRatio << std::endl;
  os << indent << "EdgeCount: " << this->EdgeCount << std::endl;
  os << indent << "EdgeLength: " << this->EdgeLength << std::endl;
  os << indent << "PreserveBoundary: " << (this->PreserveBoundary ? "on" : "off") << std::endl;
  os << indent << "DetectFeatureEdges: " << (this->DetectFeatureEdges ? "on" : "off") << std::endl;
  os << indent << "ProtectAngle: " << this->ProtectAngle << std::endl;
  os << indent << "PreventNormalFlip: " << (this->PreventNormalFlip ? "on" : "off") << std::endl;
  os << indent << "VolumeWeight: " << this->VolumeWeight << std::endl;
  os << indent << "BoundaryWeight: " << this->BoundaryWeight << std::endl;
  os << indent << "ShapeWeight: " << this->ShapeWeight << std::endl;
  os << indent << "DiscontinuityMultiplier: " << this->DiscontinuityMultiplier << std::endl;
  os << indent << "EndpointRanking: " << this->EndpointRanking << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXMeshSimplification::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);
  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }

  if (input->GetNumberOfCells() == 0)
  {
    output->Initialize();
    return 1;
  }

  if (this->Algorithm < LINDSTROM_TURK || this->Algorithm > ENDPOINT_SUBSET)
  {
    vtkErrorMacro("Invalid Algorithm " << this->Algorithm);
    return 0;
  }
  if (this->StopCriterion < STOP_EDGE_COUNT_RATIO || this->StopCriterion > STOP_EDGE_LENGTH)
  {
    vtkErrorMacro("Invalid StopCriterion " << this->StopCriterion);
    return 0;
  }
  if (this->StopCriterion == STOP_EDGE_COUNT_RATIO &&
    !(this->EdgeCountRatio > 0.0 && this->EdgeCountRatio <= 1.0))
  {
    vtkErrorMacro("EdgeCountRatio must be in (0, 1], got " << this->EdgeCountRatio);
    return 0;
  }
  if (this->StopCriterion == STOP_EDGE_COUNT && this->EdgeCount < 0)
  {
    vtkErrorMacro("EdgeCount must be >= 0, got " << this->EdgeCount);
    return 0;
  }
  if (this->StopCriterion == STOP_EDGE_LENGTH && !(this->EdgeLength > 0.0))
  {
    vtkErrorMacro("EdgeLength must be > 0, got " << this->EdgeLength);
    return 0;
  }
  if (this->DetectFeatureEdges && !(this->ProtectAngle > 0.0 && this->ProtectAngle < 180.0))
  {
    vtkErrorMacro("ProtectAngle must be in (0, 180) when DetectFeatureEdges is ON, got "
      << this->ProtectAngle);
    return 0;
  }
  if (this->Algorithm == LINDSTROM_TURK &&
    (this->VolumeWeight < 0.0 || this->BoundaryWeight < 0.0 || this->ShapeWeight < 0.0))
  {
    vtkErrorMacro("Lindstrom-Turk weights must be >= 0, got volume " << this->VolumeWeight
      << " boundary " << this->BoundaryWeight << " shape " << this->ShapeWeight);
    return 0;
  }
  if (this->Algorithm >= GARLAND_HECKBERT_PLANE && this->Algorithm <= GARLAND_HECKBERT_PROB_TRIANGLE &&
    !(this->DiscontinuityMultiplier >= 0.0))
  {
    vtkErrorMacro("DiscontinuityMultiplier must be >= 0, got " << this->DiscontinuityMultiplier);
    return 0;
  }
  if (this->Algorithm == ENDPOINT_SUBSET &&
    this->EndpointRanking != ENDPOINT_PLANE_QEM && this->EndpointRanking != ENDPOINT_MIN_ANGLE)
  {
    vtkErrorMacro("Invalid EndpointRanking " << this->EndpointRanking);
    return 0;
  }

  auto cgalMesh = std::make_unique<vtkCGALHelper::Vespa_surface>();
  if (!vtkCGALHelper::toCGAL(input, cgalMesh.get()))
  {
    vtkErrorMacro("vtkCGALHelper::toCGAL failed.");
    return 0;
  }

  if (!CGAL::is_triangle_mesh(cgalMesh->surface))
  {
    vtkErrorMacro("Input must be a pure triangle mesh (CGAL::is_triangle_mesh). "
                  "Use vtkTriangleFilter upstream.");
    return 0;
  }

  auto ecmPair =
    cgalMesh->surface.add_property_map<CGAL_Surface::Edge_index, bool>("e:is_feature", false);
  Edge_bool_map ecm = ecmPair.first;
  for (const auto e : cgalMesh->surface.edges())
  {
    put(ecm, e, false);
  }

  if (this->DetectFeatureEdges)
  {
    PMP::detect_sharp_edges(cgalMesh->surface, this->ProtectAngle, ecm);
  }

  if (this->PreserveBoundary)
  {
    for (const auto e : cgalMesh->surface.edges())
    {
      if (cgalMesh->surface.is_border(e))
      {
        put(ecm, e, true);
      }
    }
  }

  const std::size_t initialEdges = cgalMesh->surface.number_of_edges();

  SimplifyOptions opt;
  opt.Algorithm = this->Algorithm;
  opt.EndpointRanking = this->EndpointRanking;
  opt.PreventNormalFlip = this->PreventNormalFlip;
  opt.VolumeWeight = this->VolumeWeight;
  opt.BoundaryWeight = this->BoundaryWeight;
  opt.ShapeWeight = this->ShapeWeight;
  opt.DiscontinuityMultiplier = this->DiscontinuityMultiplier;

  int nCollapsed = 0;
  try
  {
    switch (this->StopCriterion)
    {
      case STOP_EDGE_COUNT_RATIO:
        nCollapsed = Run(cgalMesh->surface,
          SMS::Edge_count_ratio_stop_predicate<CGAL_Surface>(this->EdgeCountRatio), ecm, opt);
        break;
      case STOP_EDGE_COUNT:
        nCollapsed = Run(cgalMesh->surface,
          SMS::Edge_count_stop_predicate<CGAL_Surface>(static_cast<std::size_t>(this->EdgeCount)), ecm,
          opt);
        break;
      case STOP_EDGE_LENGTH:
        nCollapsed = Run(cgalMesh->surface,
          SMS::Edge_length_stop_predicate<CGAL_Kernel::FT>(
            static_cast<CGAL_Kernel::FT>(this->EdgeLength)),
          ecm, opt);
        break;
      default:
        vtkErrorMacro("Invalid StopCriterion " << this->StopCriterion);
        return 0;
    }
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Surface_mesh_simplification::edge_collapse: " << e.what());
    return 0;
  }

  if (nCollapsed < 0)
  {
    vtkErrorMacro("Invalid Algorithm " << this->Algorithm);
    return 0;
  }

  vtkDebugMacro("Collapsed " << nCollapsed << " edges.");

  if (cgalMesh->surface.has_garbage())
  {
    cgalMesh->surface.collect_garbage();
  }

  const std::size_t finalEdges = cgalMesh->surface.number_of_edges();
  if (initialEdges > 0)
  {
    if (this->StopCriterion == STOP_EDGE_COUNT_RATIO && this->EdgeCountRatio < 1.0)
    {
      const double ratio = static_cast<double>(finalEdges) / static_cast<double>(initialEdges);
      if (!(ratio < this->EdgeCountRatio))
      {
        vtkWarningMacro("Mesh simplification stopped at edge ratio " << ratio << " (>= "
          << this->EdgeCountRatio << "); link condition or constraints blocked further collapses.");
      }
    }
    else if (this->StopCriterion == STOP_EDGE_COUNT)
    {
      const auto target = static_cast<std::size_t>(this->EdgeCount);
      const bool missed =
        (target == 0) ? (finalEdges > 0) : (initialEdges >= target && finalEdges >= target);
      if (missed)
      {
        vtkWarningMacro("Mesh simplification stopped at " << finalEdges
          << " undirected edges (target is fewer than " << this->EdgeCount
          << "); link condition or constraints blocked further collapses.");
      }
    }
    else if (this->StopCriterion == STOP_EDGE_LENGTH)
    {
      const double sqMax = this->EdgeLength * this->EdgeLength;
      std::size_t shortLeft = 0;
      for (const auto e : cgalMesh->surface.edges())
      {
        const auto h = cgalMesh->surface.halfedge(e);
        const double sq = CGAL::to_double(CGAL::squared_distance(
          cgalMesh->surface.point(cgalMesh->surface.source(h)),
          cgalMesh->surface.point(cgalMesh->surface.target(h))));
        if (!(sq > sqMax))
        {
          ++shortLeft;
        }
      }
      if (shortLeft > 0)
      {
        vtkWarningMacro("Mesh simplification stopped with " << shortLeft
          << " undirected edges no longer than " << this->EdgeLength
          << "; link condition, constraints, or cost order blocked further collapses.");
      }
    }
  }

  if (!vtkCGALHelper::toVTK(cgalMesh.get(), output))
  {
    vtkErrorMacro("vtkCGALHelper::toVTK failed.");
    return 0;
  }

  this->interpolateAttributes(input, output);
  return 1;
}
