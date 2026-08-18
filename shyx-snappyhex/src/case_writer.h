#ifndef SHYX_CASE_WRITER_H
#define SHYX_CASE_WRITER_H

#include "shyx_snappy.h"
#include <string>

int shyx_write_foam_case(const std::string& caseDir, const std::string& stlPath,
    const ShyxSnappyParams& p, double xmin, double ymin, double zmin, double xmax, double ymax,
    double zmax, std::string* err);

#endif
