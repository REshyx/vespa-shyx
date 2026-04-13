/**
 * @class   vtkSHYXArrayProbabilityPointCull
 * @brief   Probabilistically subsamples input points using point-data scalars as literal keep probabilities.
 *
 * Each input point is kept with probability \f$ \max(0, \min(1, v)) \f$ where \f$ v \f$ is the point's
 * scalar (first component). There is **no** normalization by the array's global range: e.g. values in
 * [0.1, 0.5] give keep probabilities between 10% and 50%; values outside [0, 1] are clamped.
 *
 * Scalars are read from point data only (no volumetric interpolation). Input may be any vtkDataSet;
 * geometry uses vtkDataSet::GetPoint. Output is a vtkPolyData point cloud (vertex cells only) with
 * point attributes copied for surviving points.
 *
 * If the weight array name is empty or "(Uniform)", every point is kept (probability 1).
 *
 * When VESPA_USE_SMP is enabled at build time, selection and output assembly use vtkSMPTools.
 * In weighted mode the draws are from a deterministic SplitMix64 stream keyed by (Seed, point id),
 * so results differ from the non-SMP path (VTK minimal standard RNG) but are reproducible for a
 * fixed Seed and thread count.
 */

#ifndef vtkSHYXArrayProbabilityPointCull_h
#define vtkSHYXArrayProbabilityPointCull_h

#include "vtkSHYXArrayProbabilityPointCullModule.h"
#include "vtkDataSetAlgorithm.h"

#include <string>

class VTKSHYXARRAYPROBABILITYPOINTCULL_EXPORT vtkSHYXArrayProbabilityPointCull : public vtkDataSetAlgorithm
{
public:
    static vtkSHYXArrayProbabilityPointCull* New();
    vtkTypeMacro(vtkSHYXArrayProbabilityPointCull, vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Point-data scalar interpreted as per-point keep probability after clamping to [0, 1].
     * Empty or "(Uniform)" keeps all points.
     */
    vtkGetMacro(WeightArrayName, std::string);
    vtkSetMacro(WeightArrayName, std::string);
    ///@}

    ///@{
    /** Random seed for reproducible subsampling. Default 0. */
    vtkGetMacro(Seed, int);
    vtkSetMacro(Seed, int);
    ///@}

protected:
    vtkSHYXArrayProbabilityPointCull();
    ~vtkSHYXArrayProbabilityPointCull() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    std::string WeightArrayName;
    int         Seed = 0;

private:
    vtkSHYXArrayProbabilityPointCull(const vtkSHYXArrayProbabilityPointCull&) = delete;
    void operator=(const vtkSHYXArrayProbabilityPointCull&) = delete;
};

#endif
