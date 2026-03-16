/**
 * @class   vtkSHYXTetGen
 * @brief   Generates tetrahedral volume mesh from a closed surface using TetGen.
 *
 * vtkSHYXTetGen takes a closed triangulated surface mesh (vtkPolyData) as input
 * and generates a tetrahedral volume mesh (vtkUnstructuredGrid) that fills the
 * interior of the surface. It uses the TetGen library to create constrained
 * Delaunay tetrahedralization.
 *
 * The input surface must be:
 * - Closed (watertight) and manifold
 * - Composed of triangles only
 */

#ifndef vtkSHYXTetGen_h
#define vtkSHYXTetGen_h

#include "vtkCGALPMPModule.h"
#include "vtkDataSetAlgorithm.h"

class VTKCGALPMP_EXPORT vtkSHYXTetGen : public vtkDataSetAlgorithm
{
public:
    static vtkSHYXTetGen* New();
    vtkTypeMacro(vtkSHYXTetGen, vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Maximum tetrahedron volume constraint. TetGen will refine tets larger than
     * this. Set to 0 or negative to disable. Default 0 (no constraint).
     */
    vtkGetMacro(MaxVolume, double);
    vtkSetMacro(MaxVolume, double);
    ///@}

    ///@{
    /**
     * Maximum radius-edge ratio for quality meshing. TetGen -q.
     * When > 0, enables quality mesh generation with this constraint.
     * Default 1.8. When enabled (value > 0), must be >= 1.2. Set to 0 for no limit on radius ratio.
     */
    vtkGetMacro(MaxRadiusEdgeRatio, double);
    vtkSetClampMacro(MaxRadiusEdgeRatio, double, 0.0, 10.0);
    ///@}

    ///@{
    /**
     * Minimum dihedral angle (degrees) for quality meshing. TetGen -q.
     * When > 0, enables quality mesh generation with this constraint.
     * Default 0.0 (no limit on dihedral angle). Must be > 0 and < 90 when enabled. Set to 0 for no limit.
     */
    vtkGetMacro(MinDihedralAngle, double);
    vtkSetClampMacro(MinDihedralAngle, double, 0.0, 89.0);
    ///@}

    ///@{
    /**
     * Preserve input surface (no Steiner points on boundary). TetGen -Y.
     * When ON, the input surface mesh is not modified. Default ON.
     */
    vtkGetMacro(Nobisect, bool);
    vtkSetMacro(Nobisect, bool);
    vtkBooleanMacro(Nobisect, bool);
    ///@}

    ///@{
    /** Check mesh consistency after generation. TetGen -C. Default OFF. */
    vtkGetMacro(DoCheck, bool);
    vtkSetMacro(DoCheck, bool);
    vtkBooleanMacro(DoCheck, bool);
    ///@}

    ///@{
    /** Use constrained Delaunay refinement. TetGen -D. Incompatible with Nobisect. */
    vtkGetMacro(UseCDT, bool);
    vtkSetMacro(UseCDT, bool);
    vtkBooleanMacro(UseCDT, bool);
    ///@}

    ///@{
    /** CDT refinement level (1-7). TetGen -D#. Only used when UseCDT is ON. Default 7. */
    vtkGetMacro(CDTRefine, int);
    vtkSetClampMacro(CDTRefine, int, 1, 7);
    ///@}

    ///@{
    /** Tolerance for coplanar test. TetGen -T. Default 1e-8. */
    vtkGetMacro(Epsilon, double);
    vtkSetMacro(Epsilon, double);
    ///@}

protected:
    vtkSHYXTetGen();
    ~vtkSHYXTetGen() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    double MaxVolume = 0.0;
    double MaxRadiusEdgeRatio = 1.8;
    double MinDihedralAngle = 0.0;
    bool Nobisect = true;
    bool DoCheck = false;
    bool UseCDT = false;
    int CDTRefine = 7;
    double Epsilon = 1e-8;

private:
    vtkSHYXTetGen(const vtkSHYXTetGen&) = delete;
    void operator=(const vtkSHYXTetGen&) = delete;
};

#endif
