/**
 * @class   vtkSHYXTetMeshRegionPartition
 * @brief   Connectivity-based domain decomposition of a tetrahedral mesh.
 *
 * vtkSHYXTetMeshRegionPartition partitions a tetrahedral vtkUnstructuredGrid (e.g. a TetGen
 * result) into a requested number of regions and tags every tetrahedron with a "RegionId"
 * cell array (and a matching point array). An optional "Overlap" cell/point array marks the
 * halo near each partition interface: tetrahedra within OverlapLayers dual-graph hops inward
 * from a cut face (staying inside their own region). Default OverlapLayers is 1 (interface
 * cells only); increase to widen the band.
 *
 * The decomposition operates on the dual graph of the mesh: every tetrahedron is a graph node
 * and two tetrahedra are connected when they share a triangular face. Three strategies are
 * provided:
 *
 *  - Connected Components: each maximal set of face-connected tetrahedra becomes one region.
 *    Useful when a "mesh" actually contains several physically disjoint bodies.
 *
 *  - Balanced Min-Cut Bisection: regions are produced by recursively bisecting the dual graph
 *    with a graph-theoretic minimum cut (Dinic max-flow / min-cut). Best cut quality, but the
 *    max-flow does not scale to millions of tetrahedra.
 *
 *  - METIS k-way (default): multilevel k-way partitioning via METIS, run independently per
 *    connected component with the CONTIG option so each region stays connected. Scales to
 *    millions of tetrahedra in seconds. Requires building with METIS (VESPA_USE_METIS).
 *
 * All strategies split connected components first, so a region never straddles disconnected bodies.
 *
 * Non-tetrahedral cells are passed through unchanged and assigned RegionId = -1.
 *
 * Output port 0 is the tagged mesh. Output port 1 is a vtkPolyData dual-graph preview:
 * one vertex per tetrahedron (at the tet centroid) and one line per shared-face adjacency.
 *
 * @sa
 * vtkConnectivityFilter vtkSHYXTetGen
 */

#ifndef vtkSHYXTetMeshRegionPartition_h
#define vtkSHYXTetMeshRegionPartition_h

#include "vtkSHYXTetMeshRegionPartitionModule.h"
#include "vtkUnstructuredGridAlgorithm.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXTETMESHREGIONPARTITION_EXPORT vtkSHYXTetMeshRegionPartition
  : public vtkUnstructuredGridAlgorithm
{
public:
    static vtkSHYXTetMeshRegionPartition* New();
    vtkTypeMacro(vtkSHYXTetMeshRegionPartition, vtkUnstructuredGridAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    enum PartitionMethods
    {
        CONNECTED_COMPONENTS = 0,
        BALANCED_MIN_CUT = 1,
        METIS_KWAY = 2
    };

    ///@{
    /**
     * Partitioning strategy.
     *  - CONNECTED_COMPONENTS (0): label each face-connected body.
     *  - BALANCED_MIN_CUT (1): recursively bisect the dual graph with a built-in Dinic min-cut.
     *    Highest cut quality but does not scale (max-flow on millions of nodes is very slow).
     *  - METIS_KWAY (2, default): multilevel k-way partitioning via METIS. Scales to millions of
     *    tetrahedra in seconds. Run per connected component with the CONTIG option so every region
     *    stays connected. Requires the plugin to be built with METIS (VESPA_USE_METIS).
     */
    vtkSetClampMacro(PartitionMethod, int, CONNECTED_COMPONENTS, METIS_KWAY);
    vtkGetMacro(PartitionMethod, int);
    void SetPartitionMethodToConnectedComponents() { this->SetPartitionMethod(CONNECTED_COMPONENTS); }
    void SetPartitionMethodToBalancedMinCut() { this->SetPartitionMethod(BALANCED_MIN_CUT); }
    void SetPartitionMethodToMetisKway() { this->SetPartitionMethod(METIS_KWAY); }
    ///@}

    ///@{
    /**
     * Target number of regions for BALANCED_MIN_CUT. If the mesh has more connected components
     * than this value, the number of connected components wins (regions are never merged across
     * disconnected bodies). Ignored by CONNECTED_COMPONENTS. Default 4.
     */
    vtkSetClampMacro(NumberOfRegions, int, 1, VTK_INT_MAX);
    vtkGetMacro(NumberOfRegions, int);
    ///@}

    ///@{
    /**
     * Width of the middle band (fraction of the nodes in the set being bisected) that is left
     * free for the min-cut to act on. The remaining (1 - BalanceBand) nodes are anchored, half
     * to each flow terminal, by their relative graph distance, so each part keeps at least
     * n*(1-BalanceBand)/2 nodes. 0 anchors everything and forces the bisection at the balanced
     * median (perfectly balanced, the cut is not optimized); 1 anchors nothing and yields a pure
     * global min-cut (smallest interface but possibly very unbalanced). Default 0.3 balances the
     * parts while still letting the cut minimize the interface. Only used by BALANCED_MIN_CUT.
     */
    vtkSetClampMacro(BalanceBand, double, 0.0, 1.0);
    vtkGetMacro(BalanceBand, double);
    ///@}

    ///@{
    /**
     * When ON, min-cut edge capacities are the shared-face areas, so bisections minimize
     * interface area. When OFF (default), every shared face has unit capacity (minimize cut face count).
     */
    vtkSetMacro(UseFaceAreaWeights, bool);
    vtkGetMacro(UseFaceAreaWeights, bool);
    vtkBooleanMacro(UseFaceAreaWeights, bool);
    ///@}

    ///@{
    /**
     * Width of the overlap halo measured in dual-graph hops inward from each partition
     * interface (tetrahedra that share a face with a different region). Expansion never
     * crosses into another region. 0 disables overlap marking (Overlap array is all 0).
     * Default 1 marks interface tetrahedra only; 2 adds one layer of interior neighbors, etc.
     */
    vtkSetClampMacro(OverlapLayers, int, 0, VTK_INT_MAX);
    vtkGetMacro(OverlapLayers, int);
    ///@}

protected:
    vtkSHYXTetMeshRegionPartition();
    ~vtkSHYXTetMeshRegionPartition() override;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    int PartitionMethod = METIS_KWAY;
    int NumberOfRegions = 4;
    double BalanceBand = 0.3;
    bool UseFaceAreaWeights = false;
    int OverlapLayers = 1;

private:
    vtkSHYXTetMeshRegionPartition(const vtkSHYXTetMeshRegionPartition&) = delete;
    void operator=(const vtkSHYXTetMeshRegionPartition&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
