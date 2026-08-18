#include "shyx_snappy.h"

#include "case_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if SHYX_HAS_OPENFOAM
#include "error.H"
#include "IOstreams.H"
#include "OSspecific.H"
void shyx_force_foam_rts();
#ifdef SHYX_HAS_EMBEDDED_CELL_MODELS
#include "shyx_embedded_etc.h"
#endif
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

bool readStlBounds(const std::string& path, double b[6], std::string* err)
{
    std::ifstream is(path, std::ios::binary);
    if (!is)
    {
        if (err)
        {
            *err = "cannot open STL: " + path;
        }
        return false;
    }
    char hdr[80];
    is.read(hdr, 80);
    if (!is)
    {
        if (err)
        {
            *err = "invalid STL header";
        }
        return false;
    }
    const std::string hs(hdr, hdr + 80);
    const bool ascii = hs.compare(0, 5, "solid") == 0 && hs.find('\0') == std::string::npos;

    bool first = true;
    auto acc = [&](double x, double y, double z) {
        if (first)
        {
            b[0] = b[1] = x;
            b[2] = b[3] = y;
            b[4] = b[5] = z;
            first = false;
            return;
        }
        b[0] = std::min(b[0], x);
        b[1] = std::max(b[1], x);
        b[2] = std::min(b[2], y);
        b[3] = std::max(b[3], y);
        b[4] = std::min(b[4], z);
        b[5] = std::max(b[5], z);
    };

    if (ascii)
    {
        is.seekg(0);
        std::string line;
        while (std::getline(is, line))
        {
            std::istringstream ls(line);
            std::string tok;
            ls >> tok;
            if (tok == "vertex")
            {
                double x = 0, y = 0, z = 0;
                ls >> x >> y >> z;
                acc(x, y, z);
            }
        }
    }
    else
    {
        std::uint32_t ntri = 0;
        is.read(reinterpret_cast<char*>(&ntri), 4);
        for (std::uint32_t t = 0; t < ntri; ++t)
        {
            float buf[12];
            std::uint16_t attr = 0;
            is.read(reinterpret_cast<char*>(buf), sizeof(buf));
            is.read(reinterpret_cast<char*>(&attr), 2);
            if (!is)
            {
                break;
            }
            for (int v = 0; v < 3; ++v)
            {
                acc(buf[3 + 3 * v], buf[4 + 3 * v], buf[5 + 3 * v]);
            }
        }
    }
    if (first)
    {
        if (err)
        {
            *err = "STL has no vertices";
        }
        return false;
    }
    return true;
}

} // namespace

#if SHYX_HAS_OPENFOAM
namespace
{
// ParaView has no console; std::cout is failed and Foam::Sout throws
// "error in IOstream Sout for operation operator<<".
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
}
#endif

#if SHYX_HAS_OPENFOAM
int shyx_snappyHexMesh_main(int argc, char** argv);

static bool shyx_foam_etc_exists(const std::string& projectDir)
{
    return Foam::isFile(projectDir + "/etc/controlDict")
        || Foam::isFile(projectDir + "/etc/cellModels");
}

static void shyx_apply_foam_project_dir(const std::string& dir)
{
    Foam::setEnv("WM_PROJECT_DIR", dir, true);
    Foam::setEnv("FOAM_ETC", dir + "/etc", true);
}

static bool shyx_write_text_file(const std::string& path, const char* data)
{
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os || !data)
    {
        return false;
    }
    os.write(data, static_cast<std::streamsize>(std::strlen(data)));
    return static_cast<bool>(os);
}

static const char kMinEtcControlDict[] =
    "DebugSwitches\n{\n}\n"
    "InfoSwitches\n{\n    writePrecision 6;\n    outputLevel 1;\n}\n"
    "OptimisationSwitches\n{\n    fileHandler uncollated;\n}\n";

// LoadLibrary must not write files. Always materialize embedded etc to %TEMP%.
static bool shyx_materialize_temp_foam(std::string* outDir)
{
#ifdef SHYX_HAS_EMBEDDED_CELL_MODELS
    Foam::fileName tmp(Foam::getEnv("TEMP"));
    if (tmp.empty())
    {
        tmp = Foam::getEnv("TMP");
    }
    if (tmp.empty())
    {
        shyx_load_log("materialize %TEMP%/shyx-openfoam: TEMP unset");
        return false;
    }
    const std::string root = std::string(tmp) + "/shyx-openfoam";
    const std::string etc = root + "/etc";
    std::error_code ec;
    std::filesystem::create_directories(etc, ec);
    if (ec)
    {
        shyx_load_log("materialize %TEMP%/shyx-openfoam: mkdir failed");
        return false;
    }
    if (!shyx_write_text_file(etc + "/controlDict", kMinEtcControlDict)
        || !shyx_write_text_file(etc + "/cellModels", kShyxEmbeddedCellModels))
    {
        shyx_load_log("materialize %TEMP%/shyx-openfoam: write failed");
        return false;
    }
    if (!shyx_foam_etc_exists(root))
    {
        return false;
    }
    *outDir = root;
    shyx_load_log("materialize %TEMP%/shyx-openfoam: ok");
    return true;
#else
    (void)outDir;
    shyx_load_log("materialize %TEMP%/shyx-openfoam: no embedded cellModels");
    return false;
#endif
}

static bool shyx_set_runtime_foam_dir()
{
    std::string tempFoam;
    if (!shyx_materialize_temp_foam(&tempFoam))
    {
        shyx_load_log("runtime foam dir: none");
        return false;
    }
    shyx_apply_foam_project_dir(tempFoam);
    shyx_load_log("runtime foam dir: %TEMP%/shyx-openfoam");
    return true;
}
#endif

extern "C" int shyx_snappy_mesh_only(const char* case_dir, char* err, int err_len)
{
#if SHYX_HAS_OPENFOAM
    if (!case_dir)
    {
        setErr(err, err_len, "null case_dir");
        return 1;
    }
    try
    {
        shyx_load_log("shyx_snappy_mesh_only begin");
        if (!shyx_set_runtime_foam_dir())
        {
            setErr(err, err_len, "failed to write %TEMP%/shyx-openfoam/etc");
            return 1;
        }
        shyx_load_log("shyx_snappy_mesh_only after foam dir");
        shyx_force_foam_rts();
        shyx_load_log("shyx_snappy_mesh_only after RTS");
        Foam::FatalError.throwExceptions();
        Foam::FatalIOError.throwExceptions();
        std::string caseDir(case_dir);
        FoamStdoutRedirect foamIo(caseDir + "/snappyHexMesh.log");
        shyx_load_log("shyx_snappy_mesh_only calling snappyHexMesh_main");
        std::vector<std::string> args = { "snappyHexMesh", "-case", caseDir, "-overwrite" };
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& s : args)
        {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);
        const int rc = shyx_snappyHexMesh_main(static_cast<int>(args.size()), argv.data());
        shyx_load_log("shyx_snappy_mesh_only snappyHexMesh_main returned");
        if (rc != 0)
        {
            setErr(err, err_len, "snappyHexMesh failed");
            return rc;
        }
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
        setErr(err, err_len, "OpenFOAM FatalError");
        return 4;
    }
    return 0;
#else
    (void)case_dir;
    setErr(err, err_len, "OpenFOAM snappyHexMesh was not linked");
    return 5;
#endif
}

extern "C" void shyx_snappy_params_default(ShyxSnappyParams* p)
{
    if (!p)
    {
        return;
    }
    std::memset(p, 0, sizeof(*p));
    p->castellated = 1;
    p->snap = 1;
    p->add_layers = 1;
    p->background_cell_size = 0.0;
    p->bounds_margin = 0.05;
    p->location_specified = 0;
    p->max_global_cells = 2000000;
    p->n_cells_between_levels = 3;
    p->refinement_min = 0;
    p->refinement_max = 2;
    p->n_smooth_patch = 3;
    p->snap_tolerance = 2.0;
    p->n_solve_iter = 30;
    p->n_relax_iter = 5;
    p->n_surface_layers = 3;
    p->expansion_ratio = 1.2;
    p->final_layer_thickness = 0.3;
    p->min_thickness = 0.1;
    p->feature_angle = 30.0;
    p->implicit_feature_snap = 1;
}

extern "C" int shyx_snappy_run(const char* stl_path, const char* case_dir, const ShyxSnappyParams* p,
    char* err, int err_len)
{
    shyx_load_log("shyx_snappy_run begin");
    if (!stl_path || !case_dir || !p)
    {
        setErr(err, err_len, "null argument");
        return 1;
    }
    ShyxSnappyParams params = *p;
    std::string msg;
    double bb[6];
    if (!readStlBounds(stl_path, bb, &msg))
    {
        setErr(err, err_len, msg);
        return 2;
    }
    const double dx = bb[1] - bb[0];
    const double dy = bb[3] - bb[2];
    const double dz = bb[5] - bb[4];
    const double m = params.bounds_margin;
    const double xmin = bb[0] - m * dx;
    const double xmax = bb[1] + m * dx;
    const double ymin = bb[2] - m * dy;
    const double ymax = bb[3] + m * dy;
    const double zmin = bb[4] - m * dz;
    const double zmax = bb[5] + m * dz;

    if (shyx_write_foam_case(case_dir, stl_path, params, xmin, ymin, zmin, xmax, ymax, zmax, &msg) != 0)
    {
        setErr(err, err_len, msg);
        shyx_load_log("shyx_snappy_run write case failed");
        return 3;
    }
    shyx_load_log("shyx_snappy_run case written");

    // Background hex is already in constant/polyMesh. Running snappyHexMesh with
    // all stages off still loads the STL, builds intersections, and can abort()
    // the host process (ParaView) on IOstream / FatalError.
    if (!params.castellated && (params.snap || params.add_layers))
    {
        params.snap = 0;
        params.add_layers = 0;
    }
    if (!params.castellated && !params.snap && !params.add_layers)
    {
        return 0;
    }

#if SHYX_HAS_OPENFOAM
    return shyx_snappy_mesh_only(case_dir, err, err_len);
#else
    setErr(err, err_len,
        "case written; OpenFOAM snappyHexMesh was not linked (SHYX_HAS_OPENFOAM=0)");
    return 5;
#endif
}
