/**
 * @class   vtkSHYXMinimumOBBFilter
 * @brief   Build a tight oriented bounding box (OBB) for a vtkDataSet and output it as vtkPolyData.
 *
 * The box is computed with the same covariance / eigenvector method as vtkOBBTree::ComputeOBB on
 * all points of the input (unique point ids of the dataset). This yields a PCA-style OBB that
 * tightly encloses the point set; it is not the globally minimum-volume rectangular box (which is
 * much more expensive to compute).
 *
 * Output is a closed triangle mesh of the box. Field arrays document the result: OBB.Center,
 * OBB.HalfLength0..2 (axis half-lengths), OBB.Axis0..2 (unit axis directions in world space).
 *
 * @sa
 * vtkOBBTree
 */

#ifndef vtkSHYXMinimumOBBFilter_h
#define vtkSHYXMinimumOBBFilter_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXMinimumOBBFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXMINIMUMOBBFILTER_EXPORT vtkSHYXMinimumOBBFilter : public vtkDataObjectAlgorithm
{
public:
    static vtkSHYXMinimumOBBFilter* New();
    vtkTypeMacro(vtkSHYXMinimumOBBFilter, vtkDataObjectAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /** When true (default), copy input points into a contiguous buffer before OBB (slightly more
     *  memory, safe for all vtkDataSet types). When false, vtkPointSet inputs use GetPoints()
     *  directly without copying. */
    vtkSetMacro(CopyInputPoints, int);
    vtkGetMacro(CopyInputPoints, int);
    vtkBooleanMacro(CopyInputPoints, int);

protected:
    vtkSHYXMinimumOBBFilter();
    ~vtkSHYXMinimumOBBFilter() override = default;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    int CopyInputPoints = 1;

private:
    vtkSHYXMinimumOBBFilter(const vtkSHYXMinimumOBBFilter&) = delete;
    void operator=(const vtkSHYXMinimumOBBFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
