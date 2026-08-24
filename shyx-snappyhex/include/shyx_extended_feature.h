#ifndef SHYX_EXTENDED_FEATURE_H
#define SHYX_EXTENDED_FEATURE_H

/**
 * In-process OpenFOAM extendedFeatureEdgeMesh extraction (statically linked
 * meshTools). Mirrors surfaceFeatureExtract: included angle, trim, subset,
 * baffle (sideVolumeType), and the convex/concave/mixed/nonFeature point
 * plus external/internal/flat/open/multiple edge classifications.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** EdgeStatus values (OpenFOAM extendedEdgeMesh::edgeStatus). */
enum ShyxExtendedEdgeStatus
{
  SHYX_EXT_EDGE_EXTERNAL = 0,
  SHYX_EXT_EDGE_INTERNAL = 1,
  SHYX_EXT_EDGE_FLAT = 2,
  SHYX_EXT_EDGE_OPEN = 3,
  SHYX_EXT_EDGE_MULTIPLE = 4,
  SHYX_EXT_EDGE_NONE = 5
};

/** PointStatus values (OpenFOAM extendedEdgeMesh::pointStatus). */
enum ShyxExtendedPointStatus
{
  SHYX_EXT_POINT_CONVEX = 0,
  SHYX_EXT_POINT_CONCAVE = 1,
  SHYX_EXT_POINT_MIXED = 2,
  SHYX_EXT_POINT_NONFEATURE = 3
};

/** sideVolumeType on feature normals (baffle / inside / outside). */
enum ShyxExtendedVolumeType
{
  SHYX_EXT_VOL_INSIDE = 0,
  SHYX_EXT_VOL_OUTSIDE = 1,
  SHYX_EXT_VOL_BOTH = 2,
  SHYX_EXT_VOL_NEITHER = 3
};

typedef struct ShyxExtendedFeatureParams
{
  double included_angle;          /* OpenFOAM includedAngle, degrees (150 typical) */
  int geometric_test_only;        /* 1 = ignore region edges, geometry only */
  double trim_min_length;         /* 0 = off */
  int trim_min_elements;          /* 0 = off */
  int keep_open_edges;            /* 1 = keep 1-face edges (default) */
  int keep_non_manifold_edges;    /* 1 = keep >2-face edges (default) */
  int keep_region_edges;          /* 1 = keep patch-boundary edges (default) */
  int baffle_all_regions;         /* 1 = every patch is a baffle (BOTH) */
} ShyxExtendedFeatureParams;

typedef struct ShyxExtendedFeatureMesh
{
  int n_points;
  double* points; /* 3 * n_points, malloc */
  int n_edges;
  int* edges; /* 2 * n_edges, malloc */
  int* edge_status; /* n_edges, malloc, ShyxExtendedEdgeStatus */
  int* point_status; /* n_points, malloc, ShyxExtendedPointStatus */
  int* region_edge; /* n_edges, malloc, 0/1 */
  int concave_start;
  int mixed_start;
  int non_feature_start;
  int internal_start;
  int flat_start;
  int open_start;
  int multiple_start;
  int n_normals;
  double* normals; /* 3 * n_normals, malloc */
  int* normal_volume_types; /* n_normals, malloc, ShyxExtendedVolumeType */
  char* foam_ascii; /* malloc, FoamFile + extendedFeatureEdgeMesh body */
} ShyxExtendedFeatureMesh;

void shyx_extended_feature_params_default(ShyxExtendedFeatureParams* p);
void shyx_extended_feature_mesh_free(ShyxExtendedFeatureMesh* m);

/**
 * Extract classified feature edges/points via Foam::extendedFeatureEdgeMesh.
 *
 * points: 3 * n_points.
 * triangles: 3 * n_triangles (ignored when n_triangles==0).
 * triangle_regions: nullable, n_triangles region ids (>=0).
 * baffle_by_region: nullable, indexed by region id; 1 = baffle.
 * extra_points / extra_edges: optional extra polylines merged with add().
 *
 * Returns 0 on success. Caller must free *out.
 */
int shyx_extended_feature_extract(const double* points, int n_points, const int* triangles,
  int n_triangles, const int* triangle_regions, const int* baffle_by_region, int n_regions,
  const double* extra_points, int n_extra_points, const int* extra_edges, int n_extra_edges,
  const ShyxExtendedFeatureParams* p, ShyxExtendedFeatureMesh* out, char* err, int err_len);

#ifdef __cplusplus
}
#endif

#endif
