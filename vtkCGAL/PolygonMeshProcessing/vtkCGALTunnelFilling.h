/**
 * @class   vtkCGALTunnelFilling
 * @brief   fill tunnels using CGAL
 *
 * vtkCGALTunnelFilling is a filter allowing to fill tunnels on
 * a triangulated polydata. It optionally recieves a vtkSelection containing
 * a list of triangles or points, remove them from the mesh, then fill the
 * corresponding holes with the triangulate_refine_and_fair_hole method.
 * This filter may also be used to locally remesh a selected area,
 * but contrary to the vtkCGALIsotropicRemesh, it won't keep the initial shape.
 */

#ifndef vtkCGALTunnelFilling_h
#define vtkCGALTunnelFilling_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALTunnelFilling : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALTunnelFilling* New();
  vtkTypeMacro(vtkCGALTunnelFilling, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  void SetUpdateAttributes(bool update) override;

protected:
  vtkCGALTunnelFilling();
  ~vtkCGALTunnelFilling() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

private:
  vtkCGALTunnelFilling(const vtkCGALTunnelFilling&) = delete;
  void operator=(const vtkCGALTunnelFilling&) = delete;
};

#endif
