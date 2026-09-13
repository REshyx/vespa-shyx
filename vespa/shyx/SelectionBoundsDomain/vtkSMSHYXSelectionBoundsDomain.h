#ifndef vtkSMSHYXSelectionBoundsDomain_h
#define vtkSMSHYXSelectionBoundsDomain_h

#include "vtkSMBoundsDomain.h"
#include "vtkSMSHYXSelectionBoundsDomainModule.h"

/**
 * Like BoundsDomain scaled_extent, but Reset uses the AABB of selected cells
 * (Input + Selection, optional SelectionCellArrayName) instead of the full Input.
 *
 * XML tag: SHYXSelectionBoundsDomain. Required properties: Input, Selection;
 * optional: SelectionCellArrayName.
 */
class VTKSMSHYXSELECTIONBOUNDSDOMAIN_EXPORT vtkSMSHYXSelectionBoundsDomain : public vtkSMBoundsDomain
{
public:
  static vtkSMSHYXSelectionBoundsDomain* New();
  vtkTypeMacro(vtkSMSHYXSelectionBoundsDomain, vtkSMBoundsDomain);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  void Update(vtkSMProperty* prop) override;

protected:
  vtkSMSHYXSelectionBoundsDomain();
  ~vtkSMSHYXSelectionBoundsDomain() override;

  int ReadXMLAttributes(vtkSMProperty* prop, vtkPVXMLElement* element) override;

private:
  vtkSMSHYXSelectionBoundsDomain(const vtkSMSHYXSelectionBoundsDomain&) = delete;
  void operator=(const vtkSMSHYXSelectionBoundsDomain&) = delete;
};

#endif
