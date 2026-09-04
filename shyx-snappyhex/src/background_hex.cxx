#include "background_hex.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{
void writeFoamHeader
(
    std::ostream& os,
    const char* cls,
    const char* object,
    const char* location,
    const bool binary
)
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
          "    format      "
       << (binary ? "binary" : "ascii") << ";\n";
    if (binary)
    {
        os << "    arch        \"LSB;label=32;scalar=64\";\n";
    }
    os << "    class       " << cls << ";\n"
       << "    location    \"" << location << "\";\n"
       << "    object      " << object << ";\n"
          "}\n"
          "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n";
}

void writeBinaryBlock(std::ostream& os, const void* data, const std::size_t nBytes, const int n)
{
    os << '\n' << n << '\n';
    os.put('(');
    if (nBytes)
    {
        os.write(static_cast<const char*>(data), static_cast<std::streamsize>(nBytes));
    }
    os.put(')');
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
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    std::vector<double> xyz(static_cast<std::size_t>(nPoints) * 3);
    {
        std::size_t w = 0;
        for (int k = 0; k < npz; ++k)
        {
            for (int j = 0; j < npy; ++j)
            {
                for (int i = 0; i < npx; ++i)
                {
                    xyz[w++] = xmin + i * dx;
                    xyz[w++] = ymin + j * dy;
                    xyz[w++] = zmin + k * dz;
                }
            }
        }
        std::ofstream os(meshDir + "/points", std::ios::binary);
        if (!os)
        {
            if (err)
            {
                *err = "cannot write points";
            }
            return 1;
        }
        writeFoamHeader(os, "vectorField", "points", "constant/polyMesh", true);
        writeBinaryBlock(os, xyz.data(), xyz.size() * sizeof(double), nPoints);
        os.put('\n');
    }
    const auto tPoints = Clock::now();

    struct Face
    {
        int a, b, c, d;
        int own;
        int nei;
    };
    std::vector<Face> faces;
    faces.reserve(static_cast<std::size_t>(nCells) * 6);

    auto addInternal = [&](int a, int b, int c, int d, int own, int nei) {
        faces.push_back(Face{ a, b, c, d, own, nei });
    };

    const auto cellId = [nx, ny](int i, int j, int k) { return i + nx * (j + ny * k); };

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
    const auto tBuild = Clock::now();

    {
        std::vector<std::int32_t> start(static_cast<std::size_t>(nFaces) + 1);
        std::vector<std::int32_t> elems(static_cast<std::size_t>(nFaces) * 4);
        start[0] = 0;
        for (int i = 0; i < nFaces; ++i)
        {
            start[static_cast<std::size_t>(i) + 1] = (i + 1) * 4;
            const Face& f = faces[static_cast<std::size_t>(i)];
            std::int32_t* e = elems.data() + static_cast<std::size_t>(i) * 4;
            e[0] = f.a;
            e[1] = f.b;
            e[2] = f.c;
            e[3] = f.d;
        }
        std::ofstream os(meshDir + "/faces", std::ios::binary);
        writeFoamHeader(os, "faceCompactList", "faces", "constant/polyMesh", true);
        writeBinaryBlock(os, start.data(), start.size() * sizeof(std::int32_t), nFaces + 1);
        writeBinaryBlock(os, elems.data(), elems.size() * sizeof(std::int32_t), nFaces * 4);
        os.put('\n');
    }
    {
        std::vector<std::int32_t> own(static_cast<std::size_t>(nFaces));
        for (int i = 0; i < nFaces; ++i)
        {
            own[static_cast<std::size_t>(i)] = faces[static_cast<std::size_t>(i)].own;
        }
        std::ofstream os(meshDir + "/owner", std::ios::binary);
        writeFoamHeader(os, "labelList", "owner", "constant/polyMesh", true);
        writeBinaryBlock(os, own.data(), own.size() * sizeof(std::int32_t), nFaces);
        os.put('\n');
    }
    {
        std::vector<std::int32_t> nei(static_cast<std::size_t>(nInternal));
        for (int i = 0; i < nInternal; ++i)
        {
            nei[static_cast<std::size_t>(i)] = faces[static_cast<std::size_t>(i)].nei;
        }
        std::ofstream os(meshDir + "/neighbour", std::ios::binary);
        writeFoamHeader(os, "labelList", "neighbour", "constant/polyMesh", true);
        writeBinaryBlock(os, nei.data(), nei.size() * sizeof(std::int32_t), nInternal);
        os.put('\n');
    }
    {
        std::ofstream os(meshDir + "/boundary");
        writeFoamHeader(os, "polyBoundaryMesh", "boundary", "constant/polyMesh", false);
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
    const auto tWrite = Clock::now();
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    };
    std::cout << "SHYX io background nCells=" << nCells
              << " nPoints=" << nPoints << " nFaces=" << nFaces
              << " pointsBin=" << ms(t0, tPoints)
              << "s facesBuild=" << ms(tPoints, tBuild)
              << "s restBin=" << ms(tBuild, tWrite)
              << "s total=" << ms(t0, tWrite) << " s\n";
    return 0;
}
