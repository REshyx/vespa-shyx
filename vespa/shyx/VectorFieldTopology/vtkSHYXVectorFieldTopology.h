#ifndef vtkSHYXVectorFieldTopology_h
#define vtkSHYXVectorFieldTopology_h

#include "vtkSHYXVectorFieldTopologyModule.h"
#include "vtkVectorFieldTopology.h"

VTK_ABI_NAMESPACE_BEGIN

/**
 * Thin SHYX wrapper around VTK's vtkVectorFieldTopology so the filter lives in
 * a vespa/shyx module (Filters → SHYX) instead of only a server-manager XML alias.
 */
class VTKSHYXVECTORFIELDTOPOLOGY_EXPORT vtkSHYXVectorFieldTopology : public vtkVectorFieldTopology
{
public:
  static vtkSHYXVectorFieldTopology* New();
  vtkTypeMacro(vtkSHYXVectorFieldTopology, vtkVectorFieldTopology);

protected:
  vtkSHYXVectorFieldTopology() = default;
  ~vtkSHYXVectorFieldTopology() override = default;

private:
  vtkSHYXVectorFieldTopology(const vtkSHYXVectorFieldTopology&) = delete;
  void operator=(const vtkSHYXVectorFieldTopology&) = delete;
};

VTK_ABI_NAMESPACE_END

#endif
