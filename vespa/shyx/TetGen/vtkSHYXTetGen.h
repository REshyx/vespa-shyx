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

#include "vtkSHYXTetGenModule.h"
#include "vtkDataSetAlgorithm.h"

class VTKSHYXTETGEN_EXPORT vtkSHYXTetGen : public vtkDataSetAlgorithm
{
public:
    static vtkSHYXTetGen* New();
    vtkTypeMacro(vtkSHYXTetGen, vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * When ON, apply a maximum tetrahedron volume constraint (TetGen -a) if MaxVolume is positive.
     * Default OFF: no volume limit is passed to TetGen.
     */
    vtkGetMacro(LimitMaxVolume, bool);
    vtkSetMacro(LimitMaxVolume, bool);
    vtkBooleanMacro(LimitMaxVolume, bool);
    ///@}

    ///@{
    /**
     * Maximum tetrahedron volume constraint. TetGen refines tets larger than this (-a).
     * When LimitMaxVolume is ON, a positive value is required; otherwise -a is not passed.
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

    ///@{
    /**
     * When ON and the input has point data arrays, run vtkProbeFilter after meshing:
     * volume mesh points sample the input surface (point data only; input cell data is not used).
     * No new cell data is produced. Default OFF.
     */
    vtkGetMacro(ProbeInputPointData, bool);
    vtkSetMacro(ProbeInputPointData, bool);
    vtkBooleanMacro(ProbeInputPointData, bool);
    ///@}

    ///@{
    /**
     * When ON (and Probe input point data is ON), binarize the chosen input array in place
     * (values <= 0 -> 0, > 0 -> 1). Cell-centered arrays are converted to point data via
     * vtkCellDataToPointData before probing. Array slot 0 (ParaView picker).
     */
    vtkGetMacro(MaskArrayEnabled, bool);
    vtkSetMacro(MaskArrayEnabled, bool);
    vtkBooleanMacro(MaskArrayEnabled, bool);
    ///@}

    /** Mask array name (input-array slot 0). Empty when unset. */
    const char* GetMaskArrayName();

protected:
    vtkSHYXTetGen();
    ~vtkSHYXTetGen() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    bool LimitMaxVolume = false;
    double MaxVolume = 0.0;
    double MaxRadiusEdgeRatio = 1.8;
    double MinDihedralAngle = 0.0;
    bool Nobisect = true;
    bool DoCheck = false;
    bool UseCDT = false;
    int CDTRefine = 7;
    double Epsilon = 1e-8;
    bool ProbeInputPointData = false;
    bool MaskArrayEnabled = false;

private:
    vtkSHYXTetGen(const vtkSHYXTetGen&) = delete;
    void operator=(const vtkSHYXTetGen&) = delete;
};

#endif
