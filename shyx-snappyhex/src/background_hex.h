#ifndef SHYX_BACKGROUND_HEX_H
#define SHYX_BACKGROUND_HEX_H

#include <string>

/** Write OpenFOAM constant/polyMesh cartesian hex block (6 patches). */
int shyx_write_background_hex(const std::string& caseDir, double xmin, double ymin, double zmin,
    double xmax, double ymax, double zmax, int nx, int ny, int nz, std::string* err);

#endif
