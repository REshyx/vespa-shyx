#include "vtkSHYXSubsetCoarsen.h"

#include "vtkCGALHelper.h"

#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Bounded_normal_change_placement.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_count_ratio_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_plane_policies.h>
#include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/number_utils.h>
#include <CGAL/property_map.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>

vtkStandardNewMacro(vtkSHYXSubsetCoarsen);

namespace SMS = CGAL::Surface_mesh_simplification;
namespace PMP = CGAL::Polygon_mesh_processing;

namespace
{

using Edge_bool_map = CGAL_Surface::Property_map<CGAL_Surface::Edge_index, bool>;

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
 * onto q2 (CGAL Edge_profile: skip the one or two faces incident to the edge; remaining
 * Triangle.v1 is the vertex being placed). Refuses zero-area or orientation-reversing
 * triangles.
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

} // namespace

//------------------------------------------------------------------------------
void vtkSHYXSubsetCoarsen::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "EdgeCountRatio: " << this->EdgeCountRatio << std::endl;
  os << indent << "CostStrategy: " << this->CostStrategy << std::endl;
  os << indent << "PreserveBoundary: " << (this->PreserveBoundary ? "on" : "off") << std::endl;
  os << indent << "DetectFeatureEdges: " << (this->DetectFeatureEdges ? "on" : "off") << std::endl;
  os << indent << "ProtectAngle: " << this->ProtectAngle << std::endl;
  os << indent << "PreventNormalFlip: " << (this->PreventNormalFlip ? "on" : "off") << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXSubsetCoarsen::RequestData(
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

  if (!(this->EdgeCountRatio > 0.0 && this->EdgeCountRatio <= 1.0))
  {
    vtkErrorMacro("EdgeCountRatio must be in (0, 1], got " << this->EdgeCountRatio);
    return 0;
  }

  if (this->DetectFeatureEdges && !(this->ProtectAngle > 0.0 && this->ProtectAngle < 180.0))
  {
    vtkErrorMacro("ProtectAngle must be in (0, 180) when DetectFeatureEdges is ON, got "
      << this->ProtectAngle);
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

  const double initialEdges = static_cast<double>(num_edges(cgalMesh->surface));

  try
  {
    SMS::Edge_count_ratio_stop_predicate<CGAL_Surface> stop(this->EdgeCountRatio);

    int nCollapsed = 0;
    if (this->CostStrategy == 1)
    {
      MinAngleEndpointPlacement place(ecm);
      const MinAngleCost cost;
      if (this->PreventNormalFlip)
      {
        SMS::Bounded_normal_change_placement<MinAngleEndpointPlacement> filtered(place);
        nCollapsed = SMS::edge_collapse(cgalMesh->surface, stop,
          CGAL::parameters::get_cost(cost)
            .get_placement(filtered)
            .edge_is_constrained_map(ecm));
      }
      else
      {
        nCollapsed = SMS::edge_collapse(cgalMesh->surface, stop,
          CGAL::parameters::get_cost(cost)
            .get_placement(place)
            .edge_is_constrained_map(ecm));
      }
    }
    else
    {
      QemEndpointPlacement::GH gh(cgalMesh->surface);
      QemEndpointPlacement place(gh, ecm);
      if (this->PreventNormalFlip)
      {
        SMS::Bounded_normal_change_placement<QemEndpointPlacement> filtered(place);
        nCollapsed = SMS::edge_collapse(cgalMesh->surface, stop,
          CGAL::parameters::get_cost(gh.get_cost())
            .get_placement(filtered)
            .edge_is_constrained_map(ecm));
      }
      else
      {
        nCollapsed = SMS::edge_collapse(cgalMesh->surface, stop,
          CGAL::parameters::get_cost(gh.get_cost())
            .get_placement(place)
            .edge_is_constrained_map(ecm));
      }
    }

    vtkDebugMacro("Collapsed " << nCollapsed << " edges.");
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Surface_mesh_simplification::edge_collapse: " << e.what());
    return 0;
  }

  if (cgalMesh->surface.has_garbage())
  {
    cgalMesh->surface.collect_garbage();
  }

  if (initialEdges > 0.0 && this->EdgeCountRatio < 1.0)
  {
    const double ratio =
      static_cast<double>(num_edges(cgalMesh->surface)) / initialEdges;
    if (ratio >= this->EdgeCountRatio)
    {
      vtkWarningMacro("Subset coarsen stopped at edge ratio "
        << ratio << " (>= " << this->EdgeCountRatio
        << "); link condition or constraints blocked further collapses.");
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
