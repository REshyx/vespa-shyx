#include "shyx_snappy.h"

#include "case_writer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
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

bool progressAborted(ShyxSnappyProgressFn cb, void* user, double frac, const char* text)
{
    return cb && cb(frac, text, user) != 0;
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
struct ShyxSnappyAborted : std::exception
{
    const char* what() const noexcept override { return "cancelled"; }
};

int lastInt(const std::string& s)
{
    int n = -1;
    int cur = 0;
    bool in = false;
    for (unsigned char c : s)
    {
        if (std::isdigit(c))
        {
            cur = in ? cur * 10 + (c - '0') : (c - '0');
            in = true;
            n = cur;
        }
        else
        {
            in = false;
            cur = 0;
        }
    }
    return n;
}

double climb(double lo, double hi, int iter)
{
    if (iter < 0)
    {
        iter = 0;
    }
    const double t = static_cast<double>(iter + 1) / static_cast<double>(iter + 8);
    return lo + (hi - lo) * t;
}

// Parse snappyHexMesh Info lines into a monotonic 0..1 fraction.
bool mapSnappyInfoLine(const std::string& raw, double last, double* frac, std::string* text)
{
    std::string line = raw;
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
    {
        line.erase(line.begin());
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
    {
        line.pop_back();
    }
    if (line.empty() || line[0] == '-')
    {
        return false;
    }

    auto hit = [&](const char* prefix) {
        const size_t n = std::strlen(prefix);
        return line.size() >= n && line.compare(0, n, prefix) == 0;
    };

    double mapped = last;
    const char* label = nullptr;

    if (hit("Small surface feature refinement iteration"))
    {
        mapped = climb(0.42, 0.46, lastInt(line));
        label = "Castellated: small features";
    }
    else if (hit("Directional shell refinement iteration"))
    {
        mapped = climb(0.54, 0.56, lastInt(line));
        label = "Castellated: directional shells";
    }
    else if (hit("Refinement transition refinement iteration"))
    {
        mapped = climb(0.56, 0.57, lastInt(line));
        label = "Castellated: transition";
    }
    else if (hit("Dangling coarse cells refinement iteration"))
    {
        mapped = climb(0.46, 0.48, lastInt(line));
        label = "Castellated: dangling cells";
    }
    else if (hit("Big gap refinement iteration"))
    {
        mapped = climb(0.45, 0.47, lastInt(line));
        label = "Castellated: big gaps";
    }
    else if (hit("Gap refinement iteration"))
    {
        mapped = climb(0.43, 0.45, lastInt(line));
        label = "Castellated: gaps";
    }
    else if (hit("Gap blocking iteration"))
    {
        mapped = climb(0.47, 0.48, lastInt(line));
        label = "Castellated: gap blocking";
    }
    else if (hit("Feature refinement iteration"))
    {
        mapped = climb(0.22, 0.30, lastInt(line));
        label = "Castellated: feature refinement";
    }
    else if (hit("Surface refinement iteration"))
    {
        mapped = climb(0.30, 0.42, lastInt(line));
        label = "Castellated: surface refinement";
    }
    else if (hit("Shell refinement iteration"))
    {
        mapped = climb(0.50, 0.54, lastInt(line));
        label = "Castellated: shell refinement";
    }
    else if (hit("Layer addition iteration"))
    {
        mapped = climb(0.80, 0.88, lastInt(line));
        label = "Layers";
    }
    else if (hit("Morph iteration"))
    {
        mapped = climb(0.62, 0.74, lastInt(line));
        label = "Snap: morph";
    }
    else if (hit("Scaling iteration"))
    {
        mapped = climb(0.68, 0.76, lastInt(line));
        label = "Snap: scaling";
    }
    else if (hit("Smoothing iteration"))
    {
        mapped = climb(0.64, 0.70, lastInt(line));
        label = "Snap: smoothing";
    }
    else if (hit("Outer iteration"))
    {
        mapped = climb(0.82, 0.88, lastInt(line));
        label = "Layers: outer";
    }
    else if (hit("Removing mesh beyond surface intersections"))
    {
        mapped = 0.48;
        label = "Castellated: removing outside cells";
    }
    else if (hit("Splitting mesh at surface intersections"))
    {
        mapped = 0.57;
        label = "Castellated: splitting at surfaces";
    }
    else if (hit("Handling cells with snap problems"))
    {
        mapped = 0.58;
        label = "Castellated: snap problems";
    }
    else if (hit("Merge refined boundary faces"))
    {
        mapped = 0.59;
        label = "Castellated: merging faces";
    }
    else if (hit("Directional expansion ratio smoothing"))
    {
        mapped = 0.56;
        label = "Castellated: expansion smoothing";
    }
    else if (hit("Erode non-manifold zone faces"))
    {
        mapped = 0.585;
        label = "Castellated: erode zones";
    }
    else if (hit("Adding patches for surface regions"))
    {
        mapped = 0.20;
        label = "Adding patches";
    }
    else if (hit("Calculated surface intersections"))
    {
        mapped = 0.18;
        label = "Surface intersections";
    }
    else if (hit("Checking for geometry size"))
    {
        mapped = 0.14;
        label = "Checking geometry size";
    }
    else if (hit("Reading refinement surfaces"))
    {
        mapped = 0.08;
        label = "Reading refinement surfaces";
    }
    else if (hit("Reading refinement shells"))
    {
        mapped = 0.10;
        label = "Reading refinement shells";
    }
    else if (hit("Reading limit shells"))
    {
        mapped = 0.11;
        label = "Reading limit shells";
    }
    else if (hit("Reading features"))
    {
        mapped = 0.12;
        label = "Reading features";
    }
    else if (hit("Refinement phase"))
    {
        mapped = 0.22;
        label = "Castellated mesh";
    }
    else if (hit("Morphing phase"))
    {
        mapped = 0.62;
        label = "Snap";
    }
    else if (hit("Mesh refined in"))
    {
        mapped = 0.60;
        label = "Castellated done";
    }
    else if (hit("Mesh snapped in"))
    {
        mapped = 0.78;
        label = "Snap done";
    }
    else if (hit("Layers added in"))
    {
        mapped = 0.90;
        label = "Layers done";
    }
    else if (hit("Checking final mesh"))
    {
        mapped = 0.94;
        label = "Checking mesh";
    }
    else if (hit("Finished meshing"))
    {
        mapped = 0.98;
        label = "Finished snappyHexMesh";
    }
    else if (hit("Read mesh in"))
    {
        mapped = 0.06;
        label = "Reading background mesh";
    }
    else if (hit("Writing mesh to time"))
    {
        mapped = last + 0.002;
        label = "Writing mesh";
    }
    else if (line == "End")
    {
        mapped = 1.0;
        label = "End";
    }
    else
    {
        return false;
    }

    if (mapped < last)
    {
        mapped = last;
    }
    if (mapped > 1.0)
    {
        mapped = 1.0;
    }
    *frac = mapped;
    if (text && label)
    {
        *text = label;
        const int iter = lastInt(line);
        if (iter >= 0 && line.find("iteration") != std::string::npos)
        {
            std::ostringstream os;
            os << label << " " << iter;
            *text = os.str();
        }
    }
    return true;
}

// Tee Foam Info to snappyHexMesh.log and map stage lines to a progress callback.
struct TeeProgressBuf : public std::streambuf
{
    std::filebuf file;
    std::string line;
    ShyxSnappyProgressFn cb = nullptr;
    void* user = nullptr;
    double last = 0.0;
    bool aborted = false;

    bool open(const std::string& path)
    {
        return file.open(path.c_str(), std::ios::out | std::ios::trunc) != nullptr;
    }

    void close() { file.close(); }

protected:
    int_type overflow(int_type ch) override
    {
        if (traits_type::eq_int_type(ch, traits_type::eof()))
        {
            return traits_type::not_eof(ch);
        }
        const char c = traits_type::to_char_type(ch);
        if (file.sputc(c) == traits_type::eof())
        {
            return traits_type::eof();
        }
        consume(c);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        const auto w = file.sputn(s, n);
        for (std::streamsize i = 0; i < w; ++i)
        {
            consume(s[i]);
        }
        return w;
    }

    int sync() override { return file.pubsync(); }

    void consume(char c)
    {
        if (c == '\n' || c == '\r')
        {
            if (!line.empty())
            {
                parseLine(line);
                line.clear();
            }
        }
        else if (line.size() < 1024)
        {
            line.push_back(c);
        }
    }

    void parseLine(const std::string& raw)
    {
        if (!cb || aborted)
        {
            return;
        }
        double frac = last;
        std::string text;
        if (!mapSnappyInfoLine(raw, last, &frac, &text))
        {
            return;
        }
        last = frac;
        if (cb(frac, text.c_str(), user) != 0)
        {
            aborted = true;
            throw ShyxSnappyAborted();
        }
    }
};

// ParaView has no console; std::cout is failed and Foam::Sout throws
// "error in IOstream Sout for operation operator<<".
struct FoamStdoutRedirect
{
    TeeProgressBuf buf;
    std::streambuf* oldOut;
    std::streambuf* oldErr;
    bool redirected = false;

    FoamStdoutRedirect(const std::string& path, ShyxSnappyProgressFn progress, void* user)
        : oldOut(std::cout.rdbuf())
        , oldErr(std::cerr.rdbuf())
    {
        buf.cb = progress;
        buf.user = user;
        if (buf.open(path))
        {
            std::cout.rdbuf(&buf);
            std::cerr.rdbuf(&buf);
            redirected = true;
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
        buf.aborted = true;
        std::cout.flush();
        std::cerr.flush();
        if (redirected)
        {
            std::cout.rdbuf(oldOut);
            std::cerr.rdbuf(oldErr);
        }
        buf.close();
    }

    FoamStdoutRedirect(const FoamStdoutRedirect&) = delete;
    void operator=(const FoamStdoutRedirect&) = delete;
};

struct CwdGuard
{
    std::filesystem::path prev;
    explicit CwdGuard(const std::string& dir)
    {
        std::error_code ec;
        prev = std::filesystem::current_path(ec);
        std::filesystem::current_path(dir, ec);
    }
    ~CwdGuard()
    {
        if (prev.empty())
        {
            return;
        }
        std::error_code ec;
        std::filesystem::current_path(prev, ec);
    }
    CwdGuard(const CwdGuard&) = delete;
    void operator=(const CwdGuard&) = delete;
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
        return false;
    }
    const std::string root = std::string(tmp) + "/shyx-openfoam";
    const std::string etc = root + "/etc";
    std::error_code ec;
    std::filesystem::create_directories(etc, ec);
    if (ec)
    {
        return false;
    }
    if (!shyx_write_text_file(etc + "/controlDict", kMinEtcControlDict)
        || !shyx_write_text_file(etc + "/cellModels", kShyxEmbeddedCellModels))
    {
        return false;
    }
    if (!shyx_foam_etc_exists(root))
    {
        return false;
    }
    *outDir = root;
    return true;
#else
    (void)outDir;
    return false;
#endif
}

static bool shyx_set_runtime_foam_dir()
{
    std::string tempFoam;
    if (!shyx_materialize_temp_foam(&tempFoam))
    {
        return false;
    }
    shyx_apply_foam_project_dir(tempFoam);
    return true;
}
#endif

extern "C" int shyx_snappy_mesh_only(const char* case_dir, char* err, int err_len,
    ShyxSnappyProgressFn progress, void* progress_user)
{
#if SHYX_HAS_OPENFOAM
    if (!case_dir)
    {
        setErr(err, err_len, "null case_dir");
        return 1;
    }
    try
    {
        if (progressAborted(progress, progress_user, 0.02, "Running snappyHexMesh"))
        {
            setErr(err, err_len, "cancelled");
            return 6;
        }
        if (!shyx_set_runtime_foam_dir())
        {
            setErr(err, err_len, "failed to write %TEMP%/shyx-openfoam/etc");
            return 1;
        }
        shyx_force_foam_rts();
        Foam::FatalError.throwExceptions();
        Foam::FatalIOError.throwExceptions();
        std::string caseDir(case_dir);
        FoamStdoutRedirect foamIo(caseDir + "/snappyHexMesh.log", progress, progress_user);
        CwdGuard cwd(caseDir);
        Foam::FatalError.throwExceptions();
        Foam::FatalIOError.throwExceptions();
        std::vector<std::string> args = { "snappyHexMesh", "-case", caseDir, "-overwrite" };
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& s : args)
        {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);
        const int rc = shyx_snappyHexMesh_main(static_cast<int>(args.size()), argv.data());
        if (rc != 0)
        {
            setErr(err, err_len, "snappyHexMesh failed");
            return rc;
        }
    }
    catch (const ShyxSnappyAborted&)
    {
        setErr(err, err_len, "cancelled");
        return 6;
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
    (void)progress;
    (void)progress_user;
    setErr(err, err_len, "OpenFOAM snappyHexMesh was not linked");
    return 5;
#endif
}

extern "C" int shyx_foam_prepare(char* err, int err_len)
{
#if SHYX_HAS_OPENFOAM
    shyx_touch_foam_env();
    if (!shyx_set_runtime_foam_dir())
    {
        setErr(err, err_len, "failed to write %TEMP%/shyx-openfoam/etc");
        return 1;
    }
    shyx_force_foam_rts();
    Foam::FatalError.throwExceptions();
    Foam::FatalIOError.throwExceptions();
    return 0;
#else
    setErr(err, err_len, "OpenFOAM was not linked");
    return 5;
#endif
}

extern "C" int shyx_snappy_params_abi(void)
{
    return SHYX_SNAPPY_PARAMS_ABI;
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
    p->n_geometries = 0;
    p->geometries = nullptr;
    p->n_ref_surfaces = 0;
    p->ref_surfaces = nullptr;
    p->n_ref_regions = 0;
    p->ref_regions = nullptr;
    p->n_layer_patches = 0;
    p->layer_patches = nullptr;
    p->emesh_path = nullptr;
    p->feature_level = 2;
    p->emesh_is_extended = 0;
}

extern "C" int shyx_snappy_run(const char* stl_path, const char* case_dir, const ShyxSnappyParams* p,
    char* err, int err_len, ShyxSnappyProgressFn progress, void* progress_user)
{
    if (!case_dir || case_dir[0] == '\0')
    {
        setErr(err, err_len, "null/empty case_dir");
        return 1;
    }
    if (!p)
    {
        setErr(err, err_len, "null ShyxSnappyParams");
        return 1;
    }
    const bool hasMulti = p->n_geometries > 0 && p->geometries;
    if (!hasMulti && (!stl_path || stl_path[0] == '\0'))
    {
        std::ostringstream os;
        os << "no STL: stl_path is null/empty and n_geometries=" << p->n_geometries;
        setErr(err, err_len, os.str());
        return 1;
    }
    ShyxSnappyParams params = *p;
    std::string msg;
    double bb[6];
    bool haveBb = false;
    auto accBounds = [&](const double b[6]) {
        if (!haveBb)
        {
            for (int i = 0; i < 6; ++i)
            {
                bb[i] = b[i];
            }
            haveBb = true;
            return;
        }
        bb[0] = std::min(bb[0], b[0]);
        bb[1] = std::max(bb[1], b[1]);
        bb[2] = std::min(bb[2], b[2]);
        bb[3] = std::max(bb[3], b[3]);
        bb[4] = std::min(bb[4], b[4]);
        bb[5] = std::max(bb[5], b[5]);
    };
    if (hasMulti)
    {
        for (int i = 0; i < params.n_geometries; ++i)
        {
            const char* path = params.geometries[i].stl_path;
            if (!path || path[0] == '\0')
            {
                setErr(err, err_len, "empty STL path in geometries");
                return 2;
            }
            double one[6];
            if (!readStlBounds(path, one, &msg))
            {
                setErr(err, err_len, msg);
                return 2;
            }
            accBounds(one);
        }
    }
    else if (!readStlBounds(stl_path, bb, &msg))
    {
        setErr(err, err_len, msg);
        return 2;
    }
    else
    {
        haveBb = true;
    }
    if (!haveBb)
    {
        setErr(err, err_len, "no STL bounds");
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

    if (progressAborted(progress, progress_user, 0.0, "Writing OpenFOAM case"))
    {
        setErr(err, err_len, "cancelled");
        return 6;
    }
    if (shyx_write_foam_case(case_dir, stl_path ? stl_path : "", params, xmin, ymin, zmin, xmax, ymax, zmax, &msg) != 0)
    {
        setErr(err, err_len, msg);
        return 3;
    }

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
    return shyx_snappy_mesh_only(case_dir, err, err_len, progress, progress_user);
#else
    (void)progress;
    (void)progress_user;
    setErr(err, err_len,
        "case written; OpenFOAM snappyHexMesh was not linked (SHYX_HAS_OPENFOAM=0)");
    return 5;
#endif
}
