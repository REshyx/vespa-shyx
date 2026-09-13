#ifndef vtkPVSHYXSelectionBoundsInformation_h
#define vtkPVSHYXSelectionBoundsInformation_h

#include "vtkPVInformation.h"
#include "vtkSMSHYXSelectionBoundsDomainModule.h"

/**
 * Server-side bounds of selected cells on a vtkAlgorithm (port 0 datasets, optional
 * port 1 vtkSelection, optional cell-array mask name). Used by
 * vtkSMSHYXSelectionBoundsDomain so Scale/Reset can follow the selection AABB.
 */
class VTKSMSHYXSELECTIONBOUNDSDOMAIN_EXPORT vtkPVSHYXSelectionBoundsInformation
  : public vtkPVInformation
{
public:
  static vtkPVSHYXSelectionBoundsInformation* New();
  vtkTypeMacro(vtkPVSHYXSelectionBoundsInformation, vtkPVInformation);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

  void GetBounds(double bounds[6]) const;
  bool GetValid() const { return this->Valid; }

  void CopyFromObject(vtkObject* obj) override;
  void AddInformation(vtkPVInformation* info) override;
  void CopyToStream(vtkClientServerStream* css) override;
  void CopyFromStream(const vtkClientServerStream* css) override;
  void CopyParametersToStream(vtkMultiProcessStream& str) override;
  void CopyParametersFromStream(vtkMultiProcessStream& str) override;

protected:
  vtkPVSHYXSelectionBoundsInformation();
  ~vtkPVSHYXSelectionBoundsInformation() override;

  char* SelectionCellArrayName = nullptr;
  double Bounds[6];
  bool Valid = false;

private:
  vtkPVSHYXSelectionBoundsInformation(const vtkPVSHYXSelectionBoundsInformation&) = delete;
  void operator=(const vtkPVSHYXSelectionBoundsInformation&) = delete;
};

#endif
