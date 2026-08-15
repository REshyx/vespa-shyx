/**
 * @class   vtkSHYXDensityBasedSampler
 * @brief   Samples random points inside a closed mesh with density controlled by a scalar field.
 *
 * vtkSHYXDensityBasedSampler accepts either a volume mesh (vtkUnstructuredGrid,
 * e.g. .vtu) or a closed surface mesh (vtkPolyData) and generates random
 * sample points within the enclosed volume. The spatial density of the
 * output point cloud is governed by a user-selected scalar array on the
 * input, automatically linearly mapped from its value range to 0-100%.
 *
 * The output is a vtkPolyData containing only vertices (the sampled point cloud).
 */

#ifndef vtkSHYXDensityBasedSampler_h
#define vtkSHYXDensityBasedSampler_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkSHYXDensityBasedSamplerModule.h" // For export macro
#include <string>

class VTKSHYXDENSITYBASEDSAMPLER_EXPORT vtkSHYXDensityBasedSampler : public vtkPolyDataAlgorithm
{
public:
    static vtkSHYXDensityBasedSampler* New();
    vtkTypeMacro(vtkSHYXDensityBasedSampler, vtkPolyDataAlgorithm);
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
    vtkSHYXDensityBasedSampler();
    ~vtkSHYXDensityBasedSampler() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    int         PreSampleCount   = 100000;
    std::string DensityArrayName;
    int         Seed             = 0;

private:
    vtkSHYXDensityBasedSampler(const vtkSHYXDensityBasedSampler&) = delete;
    void operator=(const vtkSHYXDensityBasedSampler&)              = delete;
};

#endif
