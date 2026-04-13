/**
 * @class   vtkSHYXClebschMapFilter
 * @brief   Inside Fluids: Clebsch Maps for visualization (SIGGRAPH 2017).
 *
 * vtkSHYXClebschMapFilter maps a velocity field η0 to a wave function ψ on S3
 * such that ψ*α ≈ η0, enabling vortex tube visualization. Requires tetrahedral
 * mesh with PointData vector array (e.g. "Velocity").
 *
 * @sa
 * vtkFTLEFilter, vtkStreamTracer
 */

#ifndef vtkSHYXClebschMapFilter_h
#define vtkSHYXClebschMapFilter_h

#include "vtkDataSetAlgorithm.h"
#include "vtkSHYXClebschMapFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXCLEBSCHMAPFILTER_EXPORT vtkSHYXClebschMapFilter : public vtkDataSetAlgorithm
{
public:
    static vtkSHYXClebschMapFilter* New();
    vtkTypeMacro(vtkSHYXClebschMapFilter, vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    vtkSetMacro(Hbar, double);
    vtkGetMacro(Hbar, double);

    vtkSetMacro(AutoHbar, bool);
    vtkGetMacro(AutoHbar, bool);
    vtkBooleanMacro(AutoHbar, bool);

    vtkSetMacro(DeltaT, double);
    vtkGetMacro(DeltaT, double);

    vtkSetMacro(MaxIterations, int);
    vtkGetMacro(MaxIterations, int);

    vtkSetMacro(OuterLoops, int);
    vtkGetMacro(OuterLoops, int);

    vtkSetMacro(RandomSeed, int);
    vtkGetMacro(RandomSeed, int);

    vtkSetStringMacro(VelocityArrayName);
    vtkGetStringMacro(VelocityArrayName);

    vtkSetMacro(UseMKL, bool);
    vtkGetMacro(UseMKL, bool);
    vtkBooleanMacro(UseMKL, bool);

protected:
    vtkSHYXClebschMapFilter();
    ~vtkSHYXClebschMapFilter() override;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    double Hbar = 0.1;
    bool AutoHbar = true;
    double DeltaT = 1.0;
    int MaxIterations = 20;
    int OuterLoops = 5;
    int RandomSeed = 42;
    char* VelocityArrayName = nullptr;
    bool UseMKL = false;

private:
    vtkSHYXClebschMapFilter(const vtkSHYXClebschMapFilter&) = delete;
    void operator=(const vtkSHYXClebschMapFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
