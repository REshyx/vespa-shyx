/**
 * @class   vtkSHYXTetGenMeshOptimize
 * @brief   Improves an existing tetrahedral mesh with TetGen without inserting Steiner points.
 *
 * Reads a vtkUnstructuredGrid that contains tetrahedra (VTK_TETRA). Non-tetrahedral cells
 * are ignored. TetGen reconstructs the mesh and runs mesh improvement with -S0/-Y so
 * Delaunay refinement does not add Steiner points. By default the improvement pass is
 * flip-only (OptIterations = 0); larger OptIterations enables TetGen's smoother/Steiner loop.
 *
 * TetGen's default optimization aspect bound is very loose (1000); OptMaxAspectRatio defaults
 * tighter so flips actually target bad tetrahedra—otherwise the mesh often appears unchanged.
 */

#ifndef vtkSHYXTetGenMeshOptimize_h
#define vtkSHYXTetGenMeshOptimize_h

#include "vtkSHYXTetGenModule.h"
#include "vtkDataSetAlgorithm.h"

class VTKSHYXTETGEN_EXPORT vtkSHYXTetGenMeshOptimize : public vtkDataSetAlgorithm
{
public:
    static vtkSHYXTetGenMeshOptimize* New();
    vtkTypeMacro(vtkSHYXTetGenMeshOptimize, vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /** TetGen -O# maximum flip level (1–9). Default 3. */
    vtkGetMacro(OptMaxFlipLevel, int);
    vtkSetClampMacro(OptMaxFlipLevel, int, 1, 9);
    ///@}

    ///@{
    /** TetGen -O/# optimization scheme (0–7). Default 7. */
    vtkGetMacro(OptScheme, int);
    vtkSetClampMacro(OptScheme, int, 0, 7);
    ///@}

    ///@{
    /**
     * TetGen -o// (max aspect ratio bound for mesh improvement). TetGen's internal default is
     * 1000, which labels almost no tet as bad—set lower (e.g. 10–30) for visible flip-based
     * improvement. Use 1000 to match TetGen's default (usually little or no change).
     */
    vtkGetMacro(OptMaxAspectRatio, double);
    vtkSetClampMacro(OptMaxAspectRatio, double, 1.05, 1000.0);
    ///@}

    ///@{
    /**
     * TetGen -O//# extra improvement iterations (smooth + Steiner-capable phase).
     * Default 0: only the flip-only pass runs, so TetGen does not insert Steiner points
     * during mesh improvement. Values greater than 0 may allow internal Steiner insertion.
     */
    vtkGetMacro(OptIterations, int);
    vtkSetClampMacro(OptIterations, int, 0, 100);
    ///@}

    ///@{
    /** TetGen -C: check mesh consistency after optimization. Default OFF. */
    vtkGetMacro(DoCheck, bool);
    vtkSetMacro(DoCheck, bool);
    vtkBooleanMacro(DoCheck, bool);
    ///@}

    ///@{
    /** TetGen -J: do not jettison unused input vertices. Default ON. */
    vtkGetMacro(NoJettison, bool);
    vtkSetMacro(NoJettison, bool);
    vtkBooleanMacro(NoJettison, bool);
    ///@}

    ///@{
    /** TetGen -T coplanar tolerance. Default 1e-8. */
    vtkGetMacro(Epsilon, double);
    vtkSetMacro(Epsilon, double);
    ///@}

protected:
    vtkSHYXTetGenMeshOptimize();
    ~vtkSHYXTetGenMeshOptimize() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    int OptMaxFlipLevel = 3;
    int OptScheme = 7;
    double OptMaxAspectRatio = 20.0;
    int OptIterations = 0;
    bool DoCheck = false;
    bool NoJettison = true;
    double Epsilon = 1e-8;

private:
    vtkSHYXTetGenMeshOptimize(const vtkSHYXTetGenMeshOptimize&) = delete;
    void operator=(const vtkSHYXTetGenMeshOptimize&) = delete;
};

#endif
