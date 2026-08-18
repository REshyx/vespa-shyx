#include "case_writer.h"

#include "background_hex.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
void headerDict(std::ostream& os, const char* object)
{
    os << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
          "\\*---------------------------------------------------------------------------*/\n"
          "FoamFile\n"
          "{\n"
          "    version     2.0;\n"
          "    format      ascii;\n"
          "    class       dictionary;\n"
          "    object      "
       << object
       << ";\n"
          "}\n";
}

bool samePath(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    if (a.empty() || b.empty())
    {
        return false;
    }
    if (fs::equivalent(a, b, ec) && !ec)
    {
        return true;
    }
    return fs::absolute(a, ec) == fs::absolute(b, ec);
}

bool copyFileIfNeeded(const std::string& from, const std::string& to, std::string* err)
{
    if (from.empty())
    {
        if (err)
        {
            *err = "empty source path for " + to;
        }
        return false;
    }
    if (samePath(from, to))
    {
        return true;
    }
    std::error_code ec;
    fs::create_directories(fs::path(to).parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        if (err)
        {
            *err = "copy file failed: " + ec.message();
        }
        return false;
    }
    return true;
}

const char* orDefault(const char* s, const char* fallback)
{
    return (s && s[0] != '\0') ? s : fallback;
}

void writeGeometryBlock(std::ostream& os, const ShyxSnappyParams& p, const std::string& fallbackName)
{
    os << "geometry\n{\n";
    if (p.n_geometries > 0 && p.geometries)
    {
        for (int i = 0; i < p.n_geometries; ++i)
        {
            const char* name = orDefault(p.geometries[i].name, "geometry");
            os << "    " << name << ".stl\n"
                  "    {\n"
                  "        type triSurfaceMesh;\n"
                  "        name "
               << name << ";\n"
                  "    }\n";
        }
    }
    else
    {
        os << "    " << fallbackName
           << ".stl\n"
              "    {\n"
              "        type triSurfaceMesh;\n"
              "        name "
           << fallbackName << ";\n"
              "    }\n";
    }
    os << "}\n\n";
}

void writeFeatures(std::ostream& os, const ShyxSnappyParams& p)
{
    if (p.emesh_path && p.emesh_path[0] != '\0')
    {
        const int lvl = (p.feature_level > 0) ? p.feature_level : 2;
        os << "    features\n"
              "    (\n"
              "        {\n"
              "            file \"features.eMesh\";\n"
              "            level "
           << lvl
           << ";\n"
              "        }\n"
              "    );\n";
        return;
    }
    os << "    features ();\n";
}

void writeRefinementSurfaces(std::ostream& os, const ShyxSnappyParams& p, const std::string& fallbackName)
{
    os << "    refinementSurfaces\n    {\n";
    if (p.n_ref_surfaces > 0 && p.ref_surfaces)
    {
        for (int i = 0; i < p.n_ref_surfaces; ++i)
        {
            const char* name = orDefault(p.ref_surfaces[i].name, fallbackName.c_str());
            const char* ptype = orDefault(p.ref_surfaces[i].patch_type, "wall");
            os << "        " << name
               << "\n"
                  "        {\n"
                  "            level ("
               << p.ref_surfaces[i].level_min << " " << p.ref_surfaces[i].level_max
               << ");\n"
                  "            patchInfo\n"
                  "            {\n"
                  "                type "
               << ptype
               << ";\n"
                  "            }\n"
                  "        }\n";
        }
    }
    else if (p.n_geometries > 0 && p.geometries)
    {
        for (int i = 0; i < p.n_geometries; ++i)
        {
            const char* name = orDefault(p.geometries[i].name, fallbackName.c_str());
            os << "        " << name
               << "\n"
                  "        {\n"
                  "            level ("
               << p.refinement_min << " " << p.refinement_max
               << ");\n"
                  "            patchInfo\n"
                  "            {\n"
                  "                type wall;\n"
                  "            }\n"
                  "        }\n";
        }
    }
    else
    {
        os << "        " << fallbackName
           << "\n"
              "        {\n"
              "            level ("
           << p.refinement_min << " " << p.refinement_max
           << ");\n"
              "            patchInfo\n"
              "            {\n"
              "                type wall;\n"
              "            }\n"
              "        }\n";
    }
    os << "    }\n";
}

void writeRefinementRegions(std::ostream& os, const ShyxSnappyParams& p)
{
    os << "    refinementRegions\n    {\n";
    if (p.n_ref_regions > 0 && p.ref_regions)
    {
        for (int i = 0; i < p.n_ref_regions; ++i)
        {
            const char* name = orDefault(p.ref_regions[i].name, "region");
            const char* mode = orDefault(p.ref_regions[i].mode, "inside");
            os << "        " << name
               << "\n"
                  "        {\n"
                  "            mode "
               << mode << ";\n";
            if (std::string(mode) == "distance")
            {
                os << "            levels ((" << p.ref_regions[i].distance << " "
                   << p.ref_regions[i].level << "));\n";
            }
            else
            {
                os << "            levels ((1e15 " << p.ref_regions[i].level << "));\n";
            }
            os << "        }\n";
        }
    }
    os << "    }\n";
}

void writeLayers(std::ostream& os, const ShyxSnappyParams& p, const std::string& fallbackName)
{
    os << "    layers\n    {\n";
    if (p.n_layer_patches > 0 && p.layer_patches)
    {
        for (int i = 0; i < p.n_layer_patches; ++i)
        {
            const char* name = orDefault(p.layer_patches[i].name, fallbackName.c_str());
            os << "        " << name
               << "\n"
                  "        {\n"
                  "            nSurfaceLayers "
               << p.layer_patches[i].n_surface_layers
               << ";\n"
                  "        }\n";
        }
    }
    else if (p.n_ref_surfaces > 0 && p.ref_surfaces)
    {
        for (int i = 0; i < p.n_ref_surfaces; ++i)
        {
            const char* name = orDefault(p.ref_surfaces[i].name, fallbackName.c_str());
            os << "        " << name
               << "\n"
                  "        {\n"
                  "            nSurfaceLayers "
               << p.n_surface_layers
               << ";\n"
                  "        }\n";
        }
    }
    else if (p.n_geometries > 0 && p.geometries)
    {
        for (int i = 0; i < p.n_geometries; ++i)
        {
            const char* name = orDefault(p.geometries[i].name, fallbackName.c_str());
            os << "        " << name
               << "\n"
                  "        {\n"
                  "            nSurfaceLayers "
               << p.n_surface_layers
               << ";\n"
                  "        }\n";
        }
    }
    else
    {
        os << "        " << fallbackName
           << "\n"
              "        {\n"
              "            nSurfaceLayers "
           << p.n_surface_layers
           << ";\n"
              "        }\n";
    }
    os << "    }\n";
}
} // namespace

int shyx_write_foam_case(const std::string& caseDir, const std::string& stlPath,
    const ShyxSnappyParams& p, double xmin, double ymin, double zmin, double xmax, double ymax,
    double zmax, std::string* err)
{
    std::error_code ec;
    fs::create_directories(caseDir + "/system", ec);
    fs::create_directories(caseDir + "/constant/triSurface", ec);
    if (ec)
    {
        if (err)
        {
            *err = "cannot create case directories";
        }
        return 1;
    }

    const std::string fallbackName = "geometry";
    if (p.n_geometries > 0 && p.geometries)
    {
        for (int i = 0; i < p.n_geometries; ++i)
        {
            const char* name = orDefault(p.geometries[i].name, fallbackName.c_str());
            const char* src = p.geometries[i].stl_path;
            if (!src || src[0] == '\0')
            {
                if (err)
                {
                    *err = std::string("empty STL path for geometry ") + name;
                }
                return 1;
            }
            if (!copyFileIfNeeded(src, caseDir + "/constant/triSurface/" + name + ".stl", err))
            {
                return 1;
            }
        }
    }
    else if (!stlPath.empty())
    {
        if (!copyFileIfNeeded(stlPath, caseDir + "/constant/triSurface/" + fallbackName + ".stl", err))
        {
            return 1;
        }
    }
    else
    {
        if (err)
        {
            *err = "no STL geometry to copy";
        }
        return 1;
    }

    if (p.emesh_path && p.emesh_path[0] != '\0')
    {
        if (!copyFileIfNeeded(p.emesh_path, caseDir + "/constant/triSurface/features.eMesh", err))
        {
            return 1;
        }
    }

    const fs::path meshDir = fs::path(caseDir) / "constant" / "polyMesh";
    if (fs::exists(meshDir))
    {
        for (const char* extra : {"cellLevel", "pointLevel", "level0Edge", "surfaceIndex",
                 "refinementHistory", "cellDist", "cellDecomposition"})
        {
            fs::remove(meshDir / extra, ec);
        }
    }

    const double cell = (p.background_cell_size > 0.0) ? p.background_cell_size
                                                       : std::max({ xmax - xmin, ymax - ymin, zmax - zmin }) / 16.0;
    auto nDiv = [&](double lo, double hi) {
        const int n = static_cast<int>((hi - lo) / cell + 0.5);
        return std::max(1, n);
    };
    const int nx = nDiv(xmin, xmax);
    const int ny = nDiv(ymin, ymax);
    const int nz = nDiv(zmin, zmax);
    const long long nBg = static_cast<long long>(nx) * ny * nz;
    const long long cap = std::max<long long>(1000, p.max_global_cells);
    if (nBg > cap)
    {
        if (err)
        {
            *err = "background hex would have " + std::to_string(nBg) + " cells (" +
                std::to_string(nx) + "x" + std::to_string(ny) + "x" + std::to_string(nz) +
                "); increase Background cell size (current " + std::to_string(cell) +
                ") or Max global cells";
        }
        return 1;
    }
    if (shyx_write_background_hex(caseDir, xmin, ymin, zmin, xmax, ymax, zmax, nx, ny, nz, err) != 0)
    {
        return 1;
    }

    std::vector<double> keepPts;
    if (p.n_locations > 0 && p.locations)
    {
        keepPts.assign(p.locations, p.locations + 3 * p.n_locations);
    }
    else if (p.location_specified)
    {
        keepPts = { p.location_in_mesh[0], p.location_in_mesh[1], p.location_in_mesh[2] };
    }
    else
    {
        keepPts = { 0.5 * (xmin + xmax), 0.5 * (ymin + ymax), 0.5 * (zmin + zmax) };
    }
    if (keepPts.size() < 3)
    {
        keepPts = { 0.5 * (xmin + xmax), 0.5 * (ymin + ymax), 0.5 * (zmin + zmax) };
    }
    const int nKeep = static_cast<int>(keepPts.size() / 3);

    {
        std::ofstream os(caseDir + "/system/controlDict");
        headerDict(os, "controlDict");
        os << "application     snappyHexMesh;\n"
              "startFrom       startTime;\n"
              "startTime       0;\n"
              "stopAt          endTime;\n"
              "endTime         1;\n"
              "deltaT          1;\n"
              "writeControl    timeStep;\n"
              "writeInterval   1;\n"
              "purgeWrite      0;\n"
              "writeFormat     ascii;\n"
              "writePrecision  10;\n"
              "writeCompression off;\n"
              "timeFormat      general;\n"
              "timePrecision   6;\n"
              "runTimeModifiable true;\n";
    }
    {
        std::ofstream os(caseDir + "/system/fvSchemes");
        headerDict(os, "fvSchemes");
        os << "ddtSchemes { default Euler; }\n"
              "gradSchemes { default Gauss linear; }\n"
              "divSchemes { default none; }\n"
              "laplacianSchemes { default Gauss linear corrected; }\n"
              "interpolationSchemes { default linear; }\n"
              "snGradSchemes { default corrected; }\n";
    }
    {
        std::ofstream os(caseDir + "/system/fvSolution");
        headerDict(os, "fvSolution");
        os << "solvers {}\n";
    }
    {
        std::ofstream os(caseDir + "/system/meshQualityDict");
        headerDict(os, "meshQualityDict");
        os << "maxNonOrtho 65;\n"
              "maxBoundarySkewness 20;\n"
              "maxInternalSkewness 4;\n"
              "maxConcave 80;\n"
              "minVol 1e-13;\n"
              "minTetQuality 1e-15;\n"
              "minArea -1;\n"
              "minTwist 0.02;\n"
              "minDeterminant 0.001;\n"
              "minFaceWeight 0.05;\n"
              "minVolRatio 0.01;\n"
              "minTriangleTwist -1;\n"
              "minEdgeLength -1;\n";
    }

    const bool explicitSnap = (p.emesh_path && p.emesh_path[0] != '\0');

    std::ofstream os(caseDir + "/system/snappyHexMeshDict");
    headerDict(os, "snappyHexMeshDict");
    os << "castellatedMesh " << (p.castellated ? "true" : "false") << ";\n"
       << "snap            " << (p.snap ? "true" : "false") << ";\n"
       << "addLayers       " << (p.add_layers ? "true" : "false") << ";\n\n";
    writeGeometryBlock(os, p, fallbackName);
    os << "castellatedMeshControls\n{\n"
       << "    maxLocalCells 1000000;\n"
       << "    maxGlobalCells " << p.max_global_cells << ";\n"
       << "    minRefinementCells 0;\n"
       << "    maxLoadUnbalance 0.10;\n"
       << "    nCellsBetweenLevels " << p.n_cells_between_levels << ";\n";
    writeFeatures(os, p);
    writeRefinementSurfaces(os, p, fallbackName);
    os << "    resolveFeatureAngle " << p.feature_angle << ";\n";
    writeRefinementRegions(os, p);
    if (nKeep <= 1)
    {
        const double lx = keepPts[0];
        const double ly = keepPts[1];
        const double lz = keepPts[2];
        os << "    locationInMesh (" << lx << " " << ly << " " << lz << ");\n";
    }
    else
    {
        os << "    locationsInMesh\n    (\n";
        for (int i = 0; i < nKeep; ++i)
        {
            os << "        ((" << keepPts[static_cast<size_t>(i) * 3] << " "
               << keepPts[static_cast<size_t>(i) * 3 + 1] << " "
               << keepPts[static_cast<size_t>(i) * 3 + 2] << ") none)\n";
        }
        os << "    );\n";
    }
    os << "    allowFreeStandingZoneFaces true;\n"
          "}\n\n"
          "snapControls\n{\n"
       << "    nSmoothPatch " << p.n_smooth_patch << ";\n"
       << "    tolerance " << p.snap_tolerance << ";\n"
       << "    nSolveIter " << p.n_solve_iter << ";\n"
       << "    nRelaxIter " << p.n_relax_iter << ";\n"
       << "    nFeatureSnapIter 10;\n"
       << "    implicitFeatureSnap " << (p.implicit_feature_snap ? "true" : "false") << ";\n"
       << "    explicitFeatureSnap " << (explicitSnap ? "true" : "false") << ";\n"
          "    multiRegionFeatureSnap false;\n"
          "}\n\n"
          "addLayersControls\n{\n"
          "    relativeSizes true;\n";
    writeLayers(os, p, fallbackName);
    os << "    expansionRatio " << p.expansion_ratio << ";\n"
       << "    finalLayerThickness " << p.final_layer_thickness << ";\n"
       << "    minThickness " << p.min_thickness << ";\n"
          "    nGrow 0;\n"
          "    featureAngle "
       << p.feature_angle
       << ";\n"
          "    slipFeatureAngle 30;\n"
          "    nRelaxIter 5;\n"
          "    nSmoothSurfaceNormals 1;\n"
          "    nSmoothNormals 3;\n"
          "    nSmoothThickness 10;\n"
          "    maxFaceThicknessRatio 0.5;\n"
          "    maxThicknessToMedialRatio 0.3;\n"
          "    minMedialAxisAngle 90;\n"
          "    nBufferCellsNoExtrude 0;\n"
          "    nLayerIter 50;\n"
          "}\n\n"
          "meshQualityControls\n{\n"
          "    maxNonOrtho 65;\n"
          "    maxBoundarySkewness 20;\n"
          "    maxInternalSkewness 4;\n"
          "    maxConcave 80;\n"
          "    minVol 1e-13;\n"
          "    minTetQuality 1e-15;\n"
          "    minArea -1;\n"
          "    minTwist 0.02;\n"
          "    minDeterminant 0.001;\n"
          "    minFaceWeight 0.05;\n"
          "    minVolRatio 0.01;\n"
          "    minTriangleTwist -1;\n"
          "    minEdgeLength -1;\n"
          "    nSmoothScale 4;\n"
          "    errorReduction 0.75;\n"
          "}\n\n"
          "mergeTolerance 1e-6;\n";
    return 0;
}
