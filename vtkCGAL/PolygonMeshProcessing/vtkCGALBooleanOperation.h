/**
 * @class   vtkCGALBooleanOperation
 * @brief   Performs a boolean operation between two vtkPolyData objects
 *
 * vtkCGALBooleanOperation is a filter allowing to perform a boolean operation
 * between two closed polygonal meshes. These operations include union, intersection,
 * and difference. The resulting mesh is closed.
 */

#ifndef vtkCGALBooleanOperation_h
#define vtkCGALBooleanOperation_h

#include "vtkPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALBooleanOperation : public vtkPolyDataAlgorithm
{
public:
  static vtkCGALBooleanOperation* New();
  vtkTypeMacro(vtkCGALBooleanOperation, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * List of available boolean operations between the volume bounded by the input
   * and the volume bounded by the source. The result is a closed mesh.
   **/
  enum Operation
  {
    DIFFERENCE = 0,
    INTERSECTION,
    UNION
  };

  ///@{
  /**
   * Get/set the type of boolean operation.
   * Default is DIFFERENCE.
   **/
  vtkGetMacro(OperationType, int);
  vtkSetClampMacro(
    OperationType, int, vtkCGALBooleanOperation::DIFFERENCE, vtkCGALBooleanOperation::UNION);
  ///@}

  ///@{
  /**
   * Choose if the result mesh should have the
   * point / cell data attributes of the input.
   * If so, a vtkProbeFilter is called in order
   * to interpolate values to the new mesh.
   * Default is true.
   **/
  vtkGetMacro(UpdateAttributes, bool);
  vtkSetMacro(UpdateAttributes, bool);
  vtkBooleanMacro(UpdateAttributes, bool);
  ///@}

  /**
   * Set input connection for the second vtkPolyData.
   **/
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

protected:
  vtkCGALBooleanOperation();
  ~vtkCGALBooleanOperation() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  bool UpdateAttributes = true;
  int  OperationType    = vtkCGALBooleanOperation::DIFFERENCE;

private:
  vtkCGALBooleanOperation(const vtkCGALBooleanOperation&) = delete;
  void operator=(const vtkCGALBooleanOperation&) = delete;
};

#endif
