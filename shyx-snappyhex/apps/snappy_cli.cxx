#include "shyx_snappy.h"

#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    std::string stl;
    std::string caseDir = "snappy-case";
    std::string runCase;
    ShyxSnappyParams p;
    shyx_snappy_params_default(&p);
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto need = [&](double* dst) {
            if (i + 1 >= argc)
            {
                return false;
            }
            *dst = std::stod(argv[++i]);
            return true;
        };
        auto needi = [&](int* dst) {
            if (i + 1 >= argc)
            {
                return false;
            }
            *dst = std::stoi(argv[++i]);
            return true;
        };
        if (a == "-stl" && i + 1 < argc)
        {
            stl = argv[++i];
        }
        else if (a == "-case" && i + 1 < argc)
        {
            caseDir = argv[++i];
        }
        else if (a == "-runCase" && i + 1 < argc)
        {
            runCase = argv[++i];
        }
        else if (a == "-cell")
        {
            need(&p.background_cell_size);
        }
        else if (a == "-margin")
        {
            need(&p.bounds_margin);
        }
        else if (a == "-layers")
        {
            needi(&p.n_surface_layers);
        }
        else if (a == "-level" && i + 2 < argc)
        {
            p.refinement_min = std::stoi(argv[++i]);
            p.refinement_max = std::stoi(argv[++i]);
        }
        else if (a == "-noLayers")
        {
            p.add_layers = 0;
        }
        else if (a == "-h" || a == "--help")
        {
            std::cout << "snappy_cli -stl surface.stl [-case dir] [-cell size] [-margin frac]\n"
                         "           [-level min max] [-layers n] [-noLayers]\n"
                         "snappy_cli -runCase dir\n";
            return 0;
        }
        else
        {
            std::cerr << "unknown arg: " << a << "\n";
            return 2;
        }
    }
    char err[2048];
    err[0] = '\0';
    if (!runCase.empty())
    {
        const int rc = shyx_snappy_mesh_only(runCase.c_str(), err, 2048);
        if (rc != 0)
        {
            std::cerr << "snappyHexMesh failed (" << rc << "): " << err << "\n";
            return rc;
        }
        return 0;
    }
    if (stl.empty())
    {
        std::cerr << "missing -stl\n";
        return 2;
    }
    const int rc = shyx_snappy_run(stl.c_str(), caseDir.c_str(), &p, err, 2048);
    if (rc != 0)
    {
        std::cerr << "shyx_snappy_run failed (" << rc << "): " << err << "\n";
        return rc;
    }
    std::cout << "OK: mesh in " << caseDir << "/constant/polyMesh\n";
    return 0;
}
