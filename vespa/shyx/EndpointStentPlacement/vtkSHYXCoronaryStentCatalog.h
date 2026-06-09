/**
 * Common coronary stent catalog sizes. Index 0 means custom / measured values.
 */

#ifndef vtkSHYXCoronaryStentCatalog_h
#define vtkSHYXCoronaryStentCatalog_h

#include <array>
#include <cmath>
#include <cstddef>

VTK_ABI_NAMESPACE_BEGIN

namespace vtkSHYXCoronaryStentCatalog
{
constexpr int kCustomIndex = 0;

/** Catalog diameter; index 0 unused (custom). */
constexpr std::array<double, 7> kDiameterMm = { 0.0, 2.25, 2.5, 2.75, 3.0, 3.5, 4.0 };
/** Catalog length; index 0 unused (custom). */
constexpr std::array<double, 12> kLengthMm = { 0.0, 13, 16, 18, 21, 23, 26, 29, 31, 33, 35, 38 };

inline int DiameterCatalogCount()
{
    return static_cast<int>(kDiameterMm.size()) - 1;
}

inline int LengthCatalogCount()
{
    return static_cast<int>(kLengthMm.size()) - 1;
}

inline double DiameterMm(int index)
{
    if (index <= kCustomIndex || index >= static_cast<int>(kDiameterMm.size()))
    {
        return 0.0;
    }
    return kDiameterMm[static_cast<size_t>(index)];
}

inline double LengthMm(int index)
{
    if (index <= kCustomIndex || index >= static_cast<int>(kLengthMm.size()))
    {
        return 0.0;
    }
    return kLengthMm[static_cast<size_t>(index)];
}

inline double RadiusMm(int diameterIndex)
{
    return 0.5 * DiameterMm(diameterIndex);
}

inline int NearestDiameterIndex(double diameterMm)
{
    if (!std::isfinite(diameterMm) || diameterMm <= 0.0)
    {
        return kCustomIndex;
    }
    int best = kCustomIndex;
    double bestDiff = 1e300;
    for (int i = 1; i <= DiameterCatalogCount(); ++i)
    {
        const double d = std::fabs(DiameterMm(i) - diameterMm);
        if (d < bestDiff)
        {
            bestDiff = d;
            best = i;
        }
    }
    return best;
}

inline int NearestLengthIndex(double lengthMm)
{
    if (!std::isfinite(lengthMm) || lengthMm <= 0.0)
    {
        return kCustomIndex;
    }
    int best = kCustomIndex;
    double bestDiff = 1e300;
    for (int i = 1; i <= LengthCatalogCount(); ++i)
    {
        const double d = std::fabs(LengthMm(i) - lengthMm);
        if (d < bestDiff)
        {
            bestDiff = d;
            best = i;
        }
    }
    return best;
}

} // namespace vtkSHYXCoronaryStentCatalog

VTK_ABI_NAMESPACE_END
#endif
