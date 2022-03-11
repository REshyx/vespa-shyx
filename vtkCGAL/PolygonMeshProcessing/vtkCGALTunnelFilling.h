/**
 * @class   vtkCGALTunnelFilling
 * @brief   fill tunnels using CGAL
 *
 * vtkCGALTunnelFilling is a filter allowing to fill tunnels on
 * a triangulated polydata. It recieve the list of triangles / points
 * describing the tunnels, remove them from the mesh, then fill
 * the remaining holes with the triangulate_refine_and_fair_hole_method
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
