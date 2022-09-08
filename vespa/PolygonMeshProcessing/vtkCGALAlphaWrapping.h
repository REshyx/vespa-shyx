/**
 * @class   vtkCGALAlphaWrapping
 * @brief   remesh point clouds / triangle soups using the CGAL Alpha wrapping.
 *
 * vtkCGALAlphaWrapping is a filter allowing to reconstruct
 * a 2-manifold from an arbitrary polygon soup, for now
 * on only points / triangles are supported.
 */

#ifndef vtkCGALAlphaWrapping_h
#define vtkCGALAlphaWrapping_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALAlphaWrapping : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALAlphaWrapping* New();
  vtkTypeMacro(vtkCGALAlphaWrapping, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Get / Set the AbsoluteThresholds mode.
   * When disabled, Alpha and Offset are given
   * as percentage of the diagonal length.
   **/
  vtkGetMacro(AbsoluteThresholds, bool);
  vtkSetMacro(AbsoluteThresholds, bool);
  vtkBooleanMacro(AbsoluteThresholds, bool);
  //@}

  //@{
  /**
   * Get / Set the Alpha property,
   * driving the maximum circumcenter of the faces in the resulting mesh.
   * A smaller Alpha means a better penetration into concave parts of the mesh.
   **/
  vtkGetMacro(Alpha, double);
  vtkSetMacro(Alpha, double);
  //@}

  //@{
  /**
   * Get / Set the Offset property, driving a dilatation
   * off the resulting mesh. This non-null dilatation fix degeneracies
   * and garantee a 2-manifold output.
   **/
  vtkGetMacro(Offset, double);
  vtkSetMacro(Offset, double);
  //@}

protected:
  vtkCGALAlphaWrapping()           = default;
  ~vtkCGALAlphaWrapping() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  // Fields

  bool   AbsoluteThresholds = false;
  double Alpha              = 5;
  double Offset             = 3;

private:
  vtkCGALAlphaWrapping(const vtkCGALAlphaWrapping&) = delete;
  void operator=(const vtkCGALAlphaWrapping&)       = delete;
};

#endif
