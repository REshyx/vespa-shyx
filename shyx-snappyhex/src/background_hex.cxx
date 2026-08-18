#include "background_hex.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
void writeFoamHeader(std::ostream& os, const char* cls, const char* object, const char* location)
{
    os << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
          "| =========                 |                                                 |\n"
          "| \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |\n"
          "|  \\\\    /   O peration     | Version:  v2412                                 |\n"
          "|   \\\\  /    A nd           | Website:  www.openfoam.com                      |\n"
          "|    \\\\/     M anipulation  |                                                 |\n"
          "\\*---------------------------------------------------------------------------*/\n"
          "FoamFile\n"
          "{\n"
          "    version     2.0;\n"
          "    format      ascii;\n"
          "    class       "
       << cls << ";\n"
       << "    location    \"" << location << "\";\n"
       << "    object      " << object << ";\n"
          "}\n"
          "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n";
}

bool ensureDir(const std::string& path, std::string* err)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec)
    {
        if (err)
        {
            *err = "mkdir failed: " + path + " (" + ec.message() + ")";
        }
        return false;
    }
    return true;
}
} // namespace

int shyx_write_background_hex(const std::string& caseDir, double xmin, double ymin, double zmin,
    double xmax, double ymax, double zmax, int nx, int ny, int nz, std::string* err)
{
    if (nx < 1 || ny < 1 || nz < 1)
    {
        if (err)
        {
            *err = "background hex divisions must be >= 1";
        }
        return 1;
    }
    const std::string meshDir = caseDir + "/constant/polyMesh";
    if (!ensureDir(caseDir + "/constant", err) || !ensureDir(meshDir, err))
    {
        return 1;
    }

    const int npx = nx + 1;
    const int npy = ny + 1;
    const int npz = nz + 1;
    const int nPoints = npx * npy * npz;
    const int nCells = nx * ny * nz;
    const auto pid = [npx, npy](int i, int j, int k) { return i + npx * (j + npy * k); };
    const double dx = (xmax - xmin) / nx;
    const double dy = (ymax - ymin) / ny;
    const double dz = (zmax - zmin) / nz;

    {
        std::ofstream os(meshDir + "/points");
        if (!os)
        {
            if (err)
            {
                *err = "cannot write points";
            }
            return 1;
        }
        writeFoamHeader(os, "vectorField", "points", "constant/polyMesh");
        os << nPoints << "\n(\n";
        for (int k = 0; k < npz; ++k)
        {
            for (int j = 0; j < npy; ++j)
            {
                for (int i = 0; i < npx; ++i)
                {
                    os << "(" << (xmin + i * dx) << " " << (ymin + j * dy) << " " << (zmin + k * dz)
                       << ")\n";
                }
            }
        }
        os << ")\n";
    }

    // Owner/neighbour/faces: internal faces first, then 6 boundary patches.
    struct Face
    {
        int a, b, c, d;
        int own;
        int nei; // -1 boundary
    };
    std::vector<Face> faces;
    faces.reserve(nCells * 6);

    auto addInternal = [&](int a, int b, int c, int d, int own, int nei) {
        Face f{ a, b, c, d, own, nei };
        faces.push_back(f);
    };

    const auto cellId = [nx, ny](int i, int j, int k) { return i + nx * (j + ny * k); };

    // x-normal internals (i = 1..nx-1)
    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 1; i < nx; ++i)
            {
                const int p0 = pid(i, j, k);
                const int p1 = pid(i, j + 1, k);
                const int p2 = pid(i, j + 1, k + 1);
                const int p3 = pid(i, j, k + 1);
                addInternal(p0, p1, p2, p3, cellId(i - 1, j, k), cellId(i, j, k));
            }
        }
    }
    // y-normal internals
    for (int k = 0; k < nz; ++k)
    {
        for (int j = 1; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                const int p0 = pid(i, j, k);
                const int p1 = pid(i + 1, j, k);
                const int p2 = pid(i + 1, j, k + 1);
                const int p3 = pid(i, j, k + 1);
                addInternal(p0, p3, p2, p1, cellId(i, j - 1, k), cellId(i, j, k));
            }
        }
    }
    // z-normal internals
    for (int k = 1; k < nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                const int p0 = pid(i, j, k);
                const int p1 = pid(i + 1, j, k);
                const int p2 = pid(i + 1, j + 1, k);
                const int p3 = pid(i, j + 1, k);
                addInternal(p0, p1, p2, p3, cellId(i, j, k - 1), cellId(i, j, k));
            }
        }
    }
    const int nInternal = static_cast<int>(faces.size());

    std::vector<Face> xminF, xmaxF, yminF, ymaxF, zminF, zmaxF;
    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            xminF.push_back(Face{ pid(0, j, k), pid(0, j, k + 1), pid(0, j + 1, k + 1),
                pid(0, j + 1, k), cellId(0, j, k), -1 });
            xmaxF.push_back(Face{ pid(nx, j, k), pid(nx, j + 1, k), pid(nx, j + 1, k + 1),
                pid(nx, j, k + 1), cellId(nx - 1, j, k), -1 });
        }
    }
    for (int k = 0; k < nz; ++k)
    {
        for (int i = 0; i < nx; ++i)
        {
            yminF.push_back(Face{ pid(i, 0, k), pid(i + 1, 0, k), pid(i + 1, 0, k + 1),
                pid(i, 0, k + 1), cellId(i, 0, k), -1 });
            ymaxF.push_back(Face{ pid(i, ny, k), pid(i, ny, k + 1), pid(i + 1, ny, k + 1),
                pid(i + 1, ny, k), cellId(i, ny - 1, k), -1 });
        }
    }
    for (int j = 0; j < ny; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            zminF.push_back(Face{ pid(i, j, 0), pid(i, j + 1, 0), pid(i + 1, j + 1, 0),
                pid(i + 1, j, 0), cellId(i, j, 0), -1 });
            zmaxF.push_back(Face{ pid(i, j, nz), pid(i + 1, j, nz), pid(i + 1, j + 1, nz),
                pid(i, j + 1, nz), cellId(i, j, nz - 1), -1 });
        }
    }

    auto appendPatch = [&](const std::vector<Face>& p) {
        faces.insert(faces.end(), p.begin(), p.end());
    };
    appendPatch(xminF);
    appendPatch(xmaxF);
    appendPatch(yminF);
    appendPatch(ymaxF);
    appendPatch(zminF);
    appendPatch(zmaxF);
    const int nFaces = static_cast<int>(faces.size());

    {
        std::ofstream os(meshDir + "/faces");
        writeFoamHeader(os, "faceList", "faces", "constant/polyMesh");
        os << nFaces << "\n(\n";
        for (const Face& f : faces)
        {
            os << "4(" << f.a << " " << f.b << " " << f.c << " " << f.d << ")\n";
        }
        os << ")\n";
    }
    {
        std::ofstream os(meshDir + "/owner");
        writeFoamHeader(os, "labelList", "owner", "constant/polyMesh");
        os << nFaces << "\n(\n";
        for (const Face& f : faces)
        {
            os << f.own << "\n";
        }
        os << ")\n";
    }
    {
        std::ofstream os(meshDir + "/neighbour");
        writeFoamHeader(os, "labelList", "neighbour", "constant/polyMesh");
        os << nInternal << "\n(\n";
        for (int i = 0; i < nInternal; ++i)
        {
            os << faces[static_cast<size_t>(i)].nei << "\n";
        }
        os << ")\n";
    }
    {
        std::ofstream os(meshDir + "/boundary");
        writeFoamHeader(os, "polyBoundaryMesh", "boundary", "constant/polyMesh");
        int start = nInternal;
        os << "6\n(\n";
        auto patch = [&](const char* name, int n) {
            os << "    " << name << "\n    {\n"
               << "        type            patch;\n"
               << "        nFaces          " << n << ";\n"
               << "        startFace       " << start << ";\n"
               << "    }\n";
            start += n;
        };
        patch("xmin", static_cast<int>(xminF.size()));
        patch("xmax", static_cast<int>(xmaxF.size()));
        patch("ymin", static_cast<int>(yminF.size()));
        patch("ymax", static_cast<int>(ymaxF.size()));
        patch("zmin", static_cast<int>(zminF.size()));
        patch("zmax", static_cast<int>(zmaxF.size()));
        os << ")\n";
    }
    (void)nCells;
    return 0;
}
