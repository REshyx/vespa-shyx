#ifndef SHYX_SNAPPY_H
#define SHYX_SNAPPY_H

#ifdef __cplusplus
extern "C" {
#endif

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
} ShyxSnappyParams;

void shyx_snappy_params_default(ShyxSnappyParams* p);

/**
 * Write a cartesian background hex mesh + snappyHexMeshDict, then run
 * castellated / snap / addLayers into case_dir/constant/polyMesh.
 *
 * stl_path: closed triangulated surface (ASCII or binary STL).
 * case_dir: OpenFOAM case root (created if needed).
 * err/err_len: optional error buffer.
 *
 * Returns 0 on success, non-zero on failure.
 *
 * Runs snappyHexMesh in-process through the statically linked OpenFOAM
 * archive (no snappy_cli.exe). Foam FatalError is thrown as C++ exceptions.
 */
int shyx_snappy_run(const char* stl_path, const char* case_dir,
    const ShyxSnappyParams* p, char* err, int err_len);

/** Run snappyHexMesh on an already-written case (used by snappy_cli -runCase). */
int shyx_snappy_mesh_only(const char* case_dir, char* err, int err_len);

/* Linker anchor: pull foam_env_early.obj (FOAM_SIGFPE/FOAM_ABORT before OpenFOAM ctors). */
void shyx_touch_foam_env(void);

/** Append one line to %TEMP%/vespa-snappy-load.log (flushed). Safe-ish during LoadLibrary. */
void shyx_load_log(const char* msg);

#ifdef __cplusplus
}
#endif

#endif
