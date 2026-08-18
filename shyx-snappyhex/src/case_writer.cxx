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

bool copyFile(const std::string& from, const std::string& to, std::string* err)
{
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        if (err)
        {
            *err = "copy STL failed: " + ec.message();
        }
        return false;
    }
    return true;
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
    if (!copyFile(stlPath, caseDir + "/constant/triSurface/geometry.stl", err))
    {
        return 1;
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

    std::ofstream os(caseDir + "/system/snappyHexMeshDict");
    headerDict(os, "snappyHexMeshDict");
    os << "castellatedMesh " << (p.castellated ? "true" : "false") << ";\n"
       << "snap            " << (p.snap ? "true" : "false") << ";\n"
       << "addLayers       " << (p.add_layers ? "true" : "false") << ";\n\n"
       << "geometry\n{\n"
          "    geometry.stl\n"
          "    {\n"
          "        type triSurfaceMesh;\n"
          "        name geometry;\n"
          "    }\n"
          "}\n\n"
          "castellatedMeshControls\n{\n"
       << "    maxLocalCells 1000000;\n"
       << "    maxGlobalCells " << p.max_global_cells << ";\n"
       << "    minRefinementCells 0;\n"
       << "    maxLoadUnbalance 0.10;\n"
       << "    nCellsBetweenLevels " << p.n_cells_between_levels << ";\n"
       << "    features ();\n"
          "    refinementSurfaces\n"
          "    {\n"
          "        geometry\n"
          "        {\n"
       << "            level (" << p.refinement_min << " " << p.refinement_max << ");\n"
          "        }\n"
          "    }\n"
          "    resolveFeatureAngle "
       << p.feature_angle
       << ";\n"
          "    refinementRegions {}\n";
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
          "    explicitFeatureSnap false;\n"
          "    multiRegionFeatureSnap false;\n"
          "}\n\n"
          "addLayersControls\n{\n"
          "    relativeSizes true;\n"
          "    layers\n"
          "    {\n"
          "        geometry\n"
          "        {\n"
       << "            nSurfaceLayers " << p.n_surface_layers << ";\n"
          "        }\n"
          "    }\n"
       << "    expansionRatio " << p.expansion_ratio << ";\n"
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
