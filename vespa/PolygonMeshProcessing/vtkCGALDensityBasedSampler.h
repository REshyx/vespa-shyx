/**
 * @class   vtkCGALDensityBasedSampler
 * @brief   Samples random points inside a closed mesh with density controlled by a scalar field.
 *
 * vtkCGALDensityBasedSampler accepts either a volume mesh (vtkUnstructuredGrid,
 * e.g. .vtu) or a closed surface mesh (vtkPolyData) and generates random
 * sample points within the enclosed volume. The spatial density of the
 * output point cloud is governed by a user-selected scalar array on the
 * input, mapped through a piecewise-linear transfer function whose control
 * points are editable in the ParaView property panel.
 *
 * The output is a vtkPolyData containing only vertices (the sampled point cloud).
 */

#ifndef vtkCGALDensityBasedSampler_h
#define vtkCGALDensityBasedSampler_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro
#include <string>
#include <vector>

class vtkPiecewiseFunction;

class VTKCGALPMP_EXPORT vtkCGALDensityBasedSampler : public vtkCGALPolyDataAlgorithm
{
public:
    static vtkCGALDensityBasedSampler* New();
    vtkTypeMacro(vtkCGALDensityBasedSampler, vtkCGALPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Target number of sample points to generate inside the volume.
     * Default is 1000.
     */
    vtkGetMacro(NumberOfPoints, int);
    vtkSetClampMacro(NumberOfPoints, int, 1, 10000000);
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

    ///@{
    /**
     * Maximum number of candidate draws before giving up.
     * Prevents infinite loops when the geometry is very thin relative
     * to its bounding box.  Default is 0 (auto = NumberOfPoints * 100).
     */
    vtkGetMacro(MaxIterations, int);
    vtkSetMacro(MaxIterations, int);
    ///@}

    ///@{
    /**
     * Add a control point (x, y) to the density transfer curve.
     * x = normalised scalar [0,1], y = sampling density [0,1].
     * Called by ParaView via repeat_command for each (x,y) pair.
     */
    void AddDensityTransferPoint(double x, double y);
    /**
     * Remove all control points. Called by ParaView's clean_command
     * before re-pushing the full set of points.
     */
    void RemoveAllDensityTransferPoints();
    ///@}

protected:
    vtkCGALDensityBasedSampler();
    ~vtkCGALDensityBasedSampler() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    int         NumberOfPoints   = 1000;
    std::string DensityArrayName;
    int         Seed             = 0;
    int         MaxIterations    = 0;

    std::vector<double> TransferPoints;

private:
    vtkCGALDensityBasedSampler(const vtkCGALDensityBasedSampler&) = delete;
    void operator=(const vtkCGALDensityBasedSampler&)              = delete;
};

#endif
