#ifndef SHYX_SNAPPY_H
#define SHYX_SNAPPY_H

/** Bump when ShyxSnappyParams layout changes. vtk and lib must log the same value. */
#define SHYX_SNAPPY_PARAMS_ABI 3

#ifdef __cplusplus
extern "C" {
#endif

/** One searchable STL written as constant/triSurface/<name>.stl. Caller-owned strings. */
typedef struct ShyxSnappyGeometry
{
    const char* name;     /* OpenFOAM searchable / patch name */
    const char* stl_path; /* source STL; skipped if already at the dest path */
} ShyxSnappyGeometry;

typedef struct ShyxSnappyRefinementSurface
{
    const char* name;
    int level_min;
    int level_max;
    const char* patch_type; /* "wall" or "patch"; NULL => wall */
} ShyxSnappyRefinementSurface;

typedef struct ShyxSnappyRefinementRegion
{
    const char* name;
    const char* mode; /* inside, outside, distance; NULL => inside */
    int level;
    double distance; /* used when mode is distance */
} ShyxSnappyRefinementRegion;

typedef struct ShyxSnappyLayerPatch
{
    const char* name;
    int n_surface_layers;
} ShyxSnappyLayerPatch;

/** POD parameters for snappyHexMesh (castellated + snap + addLayers). */
typedef struct ShyxSnappyParams
{
    int castellated;          /* 1 = on */
    int snap;                 /* 1 = on */
    int add_layers;           /* 1 = on */
    double background_cell_size;
    double bounds_margin;
    double location_in_mesh[3];
    int location_specified;   /* 1 = use location_in_mesh, else bbox centre */
    int n_locations;          /* 0 = fall back to location_specified / bbox */
    const double* locations;  /* packed xyz, length 3*n_locations; caller-owned */
    int max_global_cells;
    int n_cells_between_levels;
    int refinement_min;
    int refinement_max;
    int n_smooth_patch;
    double snap_tolerance;
    int n_solve_iter;
    int n_relax_iter;
    int n_surface_layers;
    double expansion_ratio;
    double final_layer_thickness;
    double min_thickness;
    double feature_angle;
    int implicit_feature_snap;

    /* Optional multi-STL tables. n_geometries==0 => single stl_path as "geometry". */
    int n_geometries;
    const ShyxSnappyGeometry* geometries;
    int n_ref_surfaces;
    const ShyxSnappyRefinementSurface* ref_surfaces;
    int n_ref_regions;
    const ShyxSnappyRefinementRegion* ref_regions;
    int n_layer_patches;
    const ShyxSnappyLayerPatch* layer_patches;
    const char* emesh_path; /* nullable; copied to constant/triSurface/features.eMesh
                               or constant/extendedFeatureEdgeMesh/ when emesh_is_extended */
    int feature_level;
    int emesh_is_extended; /* 1 = Foam::extendedFeatureEdgeMesh (classifications) */
} ShyxSnappyParams;

void shyx_snappy_params_default(ShyxSnappyParams* p);

/**
 * Optional progress hook (ParaView / VTK UpdateProgress).
 * fraction is 0..1 within snappyHexMesh itself. text may be NULL.
 * Return non-zero to cancel (shyx_snappy_run / mesh_only then return 6).
 */
typedef int (*ShyxSnappyProgressFn)(double fraction, const char* text, void* user);

/**
 * Write a cartesian background hex mesh + snappyHexMeshDict, then run
 * castellated / snap / addLayers into case_dir/constant/polyMesh.
 *
 * stl_path: closed triangulated surface (ASCII or binary STL). May be NULL when
 *           p->n_geometries > 0.
 * case_dir: OpenFOAM case root (created if needed).
 * err/err_len: optional error buffer.
 * progress / progress_user: optional; NULL disables. Invoked from Foam Info
 *           lines so a host (ParaView) can update its progress bar.
 *
 * Returns 0 on success, 6 if progress requested cancel, other non-zero on failure.
 *
 * Runs snappyHexMesh in-process through the statically linked OpenFOAM
 * archive (no snappy_cli.exe). Foam FatalError is thrown as C++ exceptions.
 */
int shyx_snappy_run(const char* stl_path, const char* case_dir,
    const ShyxSnappyParams* p, char* err, int err_len,
    ShyxSnappyProgressFn progress, void* progress_user);

/** Must match SHYX_SNAPPY_PARAMS_ABI in the header used to compile the plugin. */
int shyx_snappy_params_abi(void);

/** Run snappyHexMesh on an already-written case (used by snappy_cli -runCase). */
int shyx_snappy_mesh_only(const char* case_dir, char* err, int err_len,
    ShyxSnappyProgressFn progress, void* progress_user);

/* Linker anchor: pull foam_env_early.obj (FOAM_SIGFPE/FOAM_ABORT before OpenFOAM ctors). */
void shyx_touch_foam_env(void);

/** Materialize %TEMP%/shyx-openfoam/etc, RTS, throwExceptions. 0 = ok. */
int shyx_foam_prepare(char* err, int err_len);

#ifdef __cplusplus
}
#endif

#endif
