#include "shyx_extended_feature.h"

#include "shyx_snappy.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if SHYX_HAS_OPENFOAM
#include "IOstreams.H"
#include "StringStream.H"
#include "boolList.H"
#include "edgeList.H"
#include "error.H"
#include "extendedEdgeMesh.H"
#include "labelledTri.H"
#include "pointField.H"
#include "surfaceFeatures.H"
#include "triSurface.H"
#endif

namespace
{
void setErr(char* err, int err_len, const std::string& msg)
{
  if (!err || err_len <= 0)
  {
    return;
  }
  const size_t n = static_cast<size_t>(err_len - 1);
  const size_t m = msg.size() < n ? msg.size() : n;
  std::memcpy(err, msg.c_str(), m);
  err[m] = '\0';
}

char* dupStr(const std::string& s)
{
  char* p = static_cast<char*>(std::malloc(s.size() + 1));
  if (!p)
  {
    return nullptr;
  }
  std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

template <typename T>
T* dupPod(const T* src, int n)
{
  if (n <= 0 || !src)
  {
    return nullptr;
  }
  T* p = static_cast<T*>(std::malloc(sizeof(T) * static_cast<size_t>(n)));
  if (!p)
  {
    return nullptr;
  }
  std::memcpy(p, src, sizeof(T) * static_cast<size_t>(n));
  return p;
}

#if SHYX_HAS_OPENFOAM
struct FoamStdoutRedirect
{
  std::ofstream log;
  std::streambuf* oldOut;
  std::streambuf* oldErr;

  explicit FoamStdoutRedirect(const std::string& path)
    : log(path, std::ios::out | std::ios::trunc)
    , oldOut(std::cout.rdbuf())
    , oldErr(std::cerr.rdbuf())
  {
    if (log)
    {
      std::cout.rdbuf(log.rdbuf());
      std::cerr.rdbuf(log.rdbuf());
    }
    std::cout.clear();
    std::cerr.clear();
    Foam::Sout.stdStream().clear();
    Foam::Serr.stdStream().clear();
    Foam::Pout.stdStream().clear();
    Foam::Perr.stdStream().clear();
    Foam::Sout.syncState();
    Foam::Serr.syncState();
    Foam::Pout.syncState();
    Foam::Perr.syncState();
  }

  ~FoamStdoutRedirect()
  {
    std::cout.flush();
    std::cerr.flush();
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);
  }

  FoamStdoutRedirect(const FoamStdoutRedirect&) = delete;
  void operator=(const FoamStdoutRedirect&) = delete;
};

int mapEdgeStatus(Foam::extendedEdgeMesh::edgeStatus s)
{
  using ES = Foam::extendedEdgeMesh::edgeStatus;
  switch (s)
  {
    case ES::EXTERNAL:
      return SHYX_EXT_EDGE_EXTERNAL;
    case ES::INTERNAL:
      return SHYX_EXT_EDGE_INTERNAL;
    case ES::FLAT:
      return SHYX_EXT_EDGE_FLAT;
    case ES::OPEN:
      return SHYX_EXT_EDGE_OPEN;
    case ES::MULTIPLE:
      return SHYX_EXT_EDGE_MULTIPLE;
    default:
      return SHYX_EXT_EDGE_NONE;
  }
}

int mapPointStatus(Foam::extendedEdgeMesh::pointStatus s)
{
  using PS = Foam::extendedEdgeMesh::pointStatus;
  switch (s)
  {
    case PS::CONVEX:
      return SHYX_EXT_POINT_CONVEX;
    case PS::CONCAVE:
      return SHYX_EXT_POINT_CONCAVE;
    case PS::MIXED:
      return SHYX_EXT_POINT_MIXED;
    default:
      return SHYX_EXT_POINT_NONFEATURE;
  }
}

int mapVolumeType(Foam::extendedEdgeMesh::sideVolumeType s)
{
  using VT = Foam::extendedEdgeMesh::sideVolumeType;
  switch (s)
  {
    case VT::INSIDE:
      return SHYX_EXT_VOL_INSIDE;
    case VT::OUTSIDE:
      return SHYX_EXT_VOL_OUTSIDE;
    case VT::BOTH:
      return SHYX_EXT_VOL_BOTH;
    default:
      return SHYX_EXT_VOL_NEITHER;
  }
}

Foam::extendedEdgeMesh fromExtraLines(
  const double* extra_points, int n_extra_points, const int* extra_edges, int n_extra_edges)
{
  Foam::pointField pts(n_extra_points);
  for (int i = 0; i < n_extra_points; ++i)
  {
    pts[i] = Foam::point(extra_points[3 * i], extra_points[3 * i + 1], extra_points[3 * i + 2]);
  }
  Foam::edgeList eds(n_extra_edges);
  for (int i = 0; i < n_extra_edges; ++i)
  {
    eds[i] = Foam::edge(extra_edges[2 * i], extra_edges[2 * i + 1]);
  }
  return Foam::extendedEdgeMesh(pts, eds);
}

int fillOut(const Foam::extendedEdgeMesh& em, ShyxExtendedFeatureMesh* out, char* err, int err_len)
{
  const Foam::pointField& pts = em.points();
  const Foam::edgeList& eds = em.edges();
  out->n_points = pts.size();
  out->n_edges = eds.size();
  out->concave_start = em.concaveStart();
  out->mixed_start = em.mixedStart();
  out->non_feature_start = em.nonFeatureStart();
  out->internal_start = em.internalStart();
  out->flat_start = em.flatStart();
  out->open_start = em.openStart();
  out->multiple_start = em.multipleStart();

  std::vector<double> xyz(static_cast<size_t>(out->n_points) * 3);
  for (int i = 0; i < out->n_points; ++i)
  {
    xyz[static_cast<size_t>(i) * 3] = pts[i].x();
    xyz[static_cast<size_t>(i) * 3 + 1] = pts[i].y();
    xyz[static_cast<size_t>(i) * 3 + 2] = pts[i].z();
  }
  std::vector<int> e2(static_cast<size_t>(out->n_edges) * 2);
  std::vector<int> eStat(static_cast<size_t>(out->n_edges), SHYX_EXT_EDGE_NONE);
  std::vector<int> region(static_cast<size_t>(out->n_edges), 0);
  for (int i = 0; i < out->n_edges; ++i)
  {
    e2[static_cast<size_t>(i) * 2] = eds[i][0];
    e2[static_cast<size_t>(i) * 2 + 1] = eds[i][1];
    eStat[static_cast<size_t>(i)] = mapEdgeStatus(em.getEdgeStatus(i));
  }
  const Foam::labelList& regionEdges = em.regionEdges();
  forAll(regionEdges, i)
  {
    const int ei = regionEdges[i];
    if (ei >= 0 && ei < out->n_edges)
    {
      region[static_cast<size_t>(ei)] = 1;
    }
  }
  std::vector<int> pStat(static_cast<size_t>(out->n_points), SHYX_EXT_POINT_NONFEATURE);
  for (int i = 0; i < out->n_points; ++i)
  {
    pStat[static_cast<size_t>(i)] = mapPointStatus(em.getPointStatus(i));
  }

  const Foam::vectorField& nrm = em.normals();
  out->n_normals = nrm.size();
  std::vector<double> nxyz(static_cast<size_t>(out->n_normals) * 3);
  std::vector<int> nvol(static_cast<size_t>(out->n_normals), SHYX_EXT_VOL_NEITHER);
  const Foam::List<Foam::extendedEdgeMesh::sideVolumeType>& nvt = em.normalVolumeTypes();
  for (int i = 0; i < out->n_normals; ++i)
  {
    nxyz[static_cast<size_t>(i) * 3] = nrm[i].x();
    nxyz[static_cast<size_t>(i) * 3 + 1] = nrm[i].y();
    nxyz[static_cast<size_t>(i) * 3 + 2] = nrm[i].z();
    if (i < nvt.size())
    {
      nvol[static_cast<size_t>(i)] = mapVolumeType(nvt[i]);
    }
  }

  Foam::OStringStream buf;
  buf << "// points" << Foam::nl;
  buf << em;
  const std::string body = buf.str();
  const std::string ascii =
    "FoamFile\n{\n    version     2.0;\n    format      ascii;\n"
    "    class       extendedFeatureEdgeMesh;\n    object      features;\n}\n\n" +
    body;

  out->points = dupPod(xyz.data(), out->n_points * 3);
  out->edges = dupPod(e2.data(), out->n_edges * 2);
  out->edge_status = dupPod(eStat.data(), out->n_edges);
  out->point_status = dupPod(pStat.data(), out->n_points);
  out->region_edge = dupPod(region.data(), out->n_edges);
  out->normals = dupPod(nxyz.data(), out->n_normals * 3);
  out->normal_volume_types = dupPod(nvol.data(), out->n_normals);
  out->foam_ascii = dupStr(ascii);
  if ((out->n_points > 0 && !out->points) || (out->n_edges > 0 && !out->edges) || !out->foam_ascii)
  {
    setErr(err, err_len, "out of memory packing extendedFeatureEdgeMesh");
    shyx_extended_feature_mesh_free(out);
    return 1;
  }
  return 0;
}
#endif
} // namespace

extern "C" void shyx_extended_feature_params_default(ShyxExtendedFeatureParams* p)
{
  if (!p)
  {
    return;
  }
  std::memset(p, 0, sizeof(*p));
  p->included_angle = 150.0;
  p->geometric_test_only = 0;
  p->keep_open_edges = 1;
  p->keep_non_manifold_edges = 1;
  p->keep_region_edges = 1;
}

extern "C" void shyx_extended_feature_mesh_free(ShyxExtendedFeatureMesh* m)
{
  if (!m)
  {
    return;
  }
  std::free(m->points);
  std::free(m->edges);
  std::free(m->edge_status);
  std::free(m->point_status);
  std::free(m->region_edge);
  std::free(m->normals);
  std::free(m->normal_volume_types);
  std::free(m->foam_ascii);
  std::memset(m, 0, sizeof(*m));
}

extern "C" int shyx_extended_feature_extract(const double* points, int n_points, const int* triangles,
  int n_triangles, const int* triangle_regions, const int* baffle_by_region, int n_regions,
  const double* extra_points, int n_extra_points, const int* extra_edges, int n_extra_edges,
  const ShyxExtendedFeatureParams* p, ShyxExtendedFeatureMesh* out, char* err, int err_len)
{
  if (out)
  {
    std::memset(out, 0, sizeof(*out));
  }
  if (!out)
  {
    setErr(err, err_len, "null ShyxExtendedFeatureMesh");
    return 1;
  }
  ShyxExtendedFeatureParams def;
  shyx_extended_feature_params_default(&def);
  if (!p)
  {
    p = &def;
  }
#if !SHYX_HAS_OPENFOAM
  (void)points;
  (void)n_points;
  (void)triangles;
  (void)n_triangles;
  (void)triangle_regions;
  (void)baffle_by_region;
  (void)n_regions;
  (void)extra_points;
  (void)n_extra_points;
  (void)extra_edges;
  (void)n_extra_edges;
  setErr(err, err_len, "OpenFOAM was not linked; cannot build extendedFeatureEdgeMesh");
  return 5;
#else
  if (n_triangles > 0 && (!points || n_points <= 0 || !triangles))
  {
    setErr(err, err_len, "surface triangles need points");
    return 1;
  }
  if (n_triangles <= 0 && n_extra_edges <= 0)
  {
    setErr(err, err_len, "need surface triangles or extra feature lines");
    return 1;
  }
  if (shyx_foam_prepare(err, err_len) != 0)
  {
    return 1;
  }

  try
  {
#ifdef _WIN32
    FoamStdoutRedirect foamIo("NUL");
#else
    FoamStdoutRedirect foamIo("/dev/null");
#endif

    Foam::extendedEdgeMesh em;
    if (n_triangles > 0)
    {
      Foam::pointField pts(n_points);
      for (int i = 0; i < n_points; ++i)
      {
        pts[i] = Foam::point(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
      }
      Foam::List<Foam::labelledTri> faces(n_triangles);
      for (int i = 0; i < n_triangles; ++i)
      {
        const int r = triangle_regions ? std::max(0, triangle_regions[i]) : 0;
        faces[i] = Foam::labelledTri(
          triangles[3 * i], triangles[3 * i + 1], triangles[3 * i + 2], r);
      }
      Foam::triSurface surf(faces, pts);
      const double angle = p->included_angle;
      Foam::surfaceFeatures feat(surf);
      feat.findFeatures(angle, p->geometric_test_only != 0);
      if (p->trim_min_length > 0.0 || p->trim_min_elements > 0)
      {
        feat.trimFeatures(p->trim_min_length, p->trim_min_elements, angle);
      }
      Foam::List<Foam::surfaceFeatures::edgeStatus> st = feat.toStatus();
      if (!p->keep_region_edges)
      {
        forAll(st, ei)
        {
          if (st[ei] == Foam::surfaceFeatures::REGION)
          {
            st[ei] = Foam::surfaceFeatures::NONE;
          }
        }
      }
      if (!p->keep_non_manifold_edges)
      {
        feat.excludeNonManifold(st);
      }
      if (!p->keep_open_edges)
      {
        feat.excludeOpen(st);
      }
      feat.setFromStatus(st, angle);

      // Must match surf.patches().size(). A longer baffle list is indexed as
      // patches[i] inside extendedEdgeMesh and abort()s ParaView.
      Foam::boolList baffle(surf.patches().size(), false);
      if (p->baffle_all_regions)
      {
        baffle = true;
      }
      else if (baffle_by_region && n_regions > 0)
      {
        for (int i = 0; i < n_regions && i < baffle.size(); ++i)
        {
          baffle[i] = baffle_by_region[i] != 0;
        }
      }
      Foam::extendedEdgeMesh classified(feat, baffle);
      em.transfer(classified);
    }

    if (n_extra_edges > 0 && extra_points && extra_edges && n_extra_points > 0)
    {
      Foam::extendedEdgeMesh extra(
        fromExtraLines(extra_points, n_extra_points, extra_edges, n_extra_edges));
      if (n_triangles > 0)
      {
        em.add(extra);
      }
      else
      {
        em.transfer(extra);
      }
    }

    return fillOut(em, out, err, err_len);
  }
  catch (const Foam::IOerror& ex)
  {
    setErr(err, err_len, ex.message());
    return 4;
  }
  catch (const Foam::error& ex)
  {
    setErr(err, err_len, ex.message());
    return 4;
  }
  catch (const std::exception& ex)
  {
    setErr(err, err_len, ex.what());
    return 4;
  }
  catch (...)
  {
    setErr(err, err_len, "OpenFOAM FatalError in extendedFeatureEdgeMesh");
    return 4;
  }
#endif
}
