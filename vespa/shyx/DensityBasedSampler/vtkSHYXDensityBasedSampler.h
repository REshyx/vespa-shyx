/**
 * @class   vtkCGALDensityBasedSampler
 * @brief   Samples random points inside a closed mesh with density controlled by a scalar field.
 *
 * vtkCGALDensityBasedSampler accepts either a volume mesh (vtkUnstructuredGrid,
 * e.g. .vtu) or a closed surface mesh (vtkPolyData) and generates random
 * sample points within the enclosed volume. The spatial density of the
 * output point cloud is governed by a user-selected scalar array on the
 * input, automatically linearly mapped from its value range to 0-100%.
 *
 * The output is a vtkPolyData containing only vertices (the sampled point cloud).
 * Historical class name still has CGAL in it; the implementation is VTK-only.
 */

#ifndef vtkCGALDensityBasedSampler_h
#define vtkCGALDensityBasedSampler_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkSHYXDensityBasedSamplerModule.h" // For export macro
#include <string>

class VTKSHYXDENSITYBASEDSAMPLER_EXPORT vtkCGALDensityBasedSampler : public vtkPolyDataAlgorithm
{
public:
    static vtkCGALDensityBasedSampler* New();
    vtkTypeMacro(vtkCGALDensityBasedSampler, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Number of pre-sample grid points (Cartesian lattice).
     * Grid resolution is derived from bounding box aspect ratio.
     * Output count = interior points kept after density filtering.
     */
    vtkGetMacro(PreSampleCount, int);
    vtkSetClampMacro(PreSampleCount, int, 1, 100000000);
    ///@}

    ///@{
    /**
     * Name of the point-data scalar array used as the density weight.
     * Leave empty (or choose "(Uniform)") for uniform sampling.
     */
    vtkGetMacro(DensityArrayName, std::string);
    vtkSetMacro(DensityArrayName, std::string);
    ///@}

    ///@{
    /**
     * Random seed for reproducibility.  Default is 0.
     */
    vtkGetMacro(Seed, int);
    vtkSetMacro(Seed, int);
    ///@}

protected:
    vtkCGALDensityBasedSampler();
    ~vtkCGALDensityBasedSampler() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    int         PreSampleCount   = 100000;
    std::string DensityArrayName;
    int         Seed             = 0;

private:
    vtkCGALDensityBasedSampler(const vtkCGALDensityBasedSampler&) = delete;
    void operator=(const vtkCGALDensityBasedSampler&)              = delete;
};

#endif
