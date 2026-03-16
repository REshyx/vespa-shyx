/**
 * @class   vtkCGALSurfaceToVolumeMesh
 * @brief   Generates tetrahedral volume mesh from a closed surface using CGAL Mesh_3.
 *
 * vtkCGALSurfaceToVolumeMesh takes a closed triangulated surface mesh (vtkPolyData)
 * as input and generates a tetrahedral volume mesh (vtkUnstructuredGrid) that fills
 * the interior of the surface. It uses CGAL's Mesh_3 package with Polyhedral_mesh_domain_3
 * and Delaunay refinement.
 *
 * The input surface must be:
 * - Closed (watertight) and manifold
 * - Composed of triangles only
 * - Free of self-intersections
 */

#ifndef vtkCGALSurfaceToVolumeMesh_h
#define vtkCGALSurfaceToVolumeMesh_h

#include "vtkCGALPMPModule.h" // For export macro
#include "vtkDataSetAlgorithm.h"

class VTKCGALPMP_EXPORT vtkCGALSurfaceToVolumeMesh : public vtkDataSetAlgorithm
{
public:
    static vtkCGALSurfaceToVolumeMesh* New();
    vtkTypeMacro(vtkCGALSurfaceToVolumeMesh, vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Facet angle (degrees). Lower bound for angles of surface facets.
     * Default 25. Smaller values give finer surface approximation.
     */
    vtkGetMacro(FacetAngle, double);
    vtkSetClampMacro(FacetAngle, double, 1.0, 90.0);
    ///@}

    ///@{
    /**
     * Facet size. Upper bound for radii of surface Delaunay balls.
     * Default 0.15. Smaller values give finer surface mesh.
     */
    vtkGetMacro(FacetSize, double);
    vtkSetClampMacro(FacetSize, double, 1e-6, 1e6);
    ///@}

    ///@{
    /**
     * Facet distance. Upper bound for distance between circumcenter of surface facet
     * and center of its surface Delaunay ball. Default 0.008.
     * Smaller values give more accurate surface approximation.
     */
    vtkGetMacro(FacetDistance, double);
    vtkSetClampMacro(FacetDistance, double, 1e-9, 1.0);
    ///@}

    ///@{
    /**
     * Cell radius-edge ratio. Upper bound for ratio of circumradius to shortest edge
     * of tetrahedra. Default 3.0. Must be > 2 for algorithm termination.
     */
    vtkGetMacro(CellRadiusEdgeRatio, double);
    vtkSetClampMacro(CellRadiusEdgeRatio, double, 2.01, 10.0);
    ///@}

    ///@{
    /**
     * Cell size. Upper bound on circumradii of tetrahedra. Default 0.0 means
     * no explicit bound (driven by surface criteria). Set > 0 for coarser volume mesh.
     */
    vtkGetMacro(CellSize, double);
    vtkSetClampMacro(CellSize, double, 0.0, 1e6);
    ///@}

    ///@{
    /**
     * When ON, detects sharp feature edges and corners from the input mesh.
     * Edges with dihedral angle >= FeatureAngle are treated as features.
     * Feature points (corners) are auto-detected at feature edge junctions.
     * Default ON. Turn OFF for smooth surfaces to skip feature protection.
     */
    vtkGetMacro(DetectFeatures, bool);
    vtkSetMacro(DetectFeatures, bool);
    vtkBooleanMacro(DetectFeatures, bool);
    ///@}

    ///@{
    /**
     * Feature detection angle (degrees). Edges where adjacent face normals
     * differ by >= this angle are marked as feature edges. Default 60.
     * Only used when DetectFeatures is ON.
     */
    vtkGetMacro(FeatureAngle, double);
    vtkSetClampMacro(FeatureAngle, double, 1.0, 180.0);
    ///@}

    ///@{
    /**
     * Upper bound on length of segments along feature edges. Smaller values
     * give denser sampling on sharp edges. Default 0.0 means no explicit
     * bound (driven by facet criteria). Only used when DetectFeatures is ON.
     */
    vtkGetMacro(EdgeSize, double);
    vtkSetClampMacro(EdgeSize, double, 0.0, 1e6);
    ///@}

protected:
    vtkCGALSurfaceToVolumeMesh();
    ~vtkCGALSurfaceToVolumeMesh() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    double FacetAngle         = 25.0;
    double FacetSize          = 0.15;
    double FacetDistance      = 0.008;
    double CellRadiusEdgeRatio = 3.0;
    double CellSize           = 0.0;
    bool   DetectFeatures     = true;
    double FeatureAngle       = 60.0;
    double EdgeSize           = 0.0;

private:
    vtkCGALSurfaceToVolumeMesh(const vtkCGALSurfaceToVolumeMesh&) = delete;
    void operator=(const vtkCGALSurfaceToVolumeMesh&)              = delete;
};

#endif
