#ifndef pqSHYXSnappyCastellatedWidget_h
#define pqSHYXSnappyCastellatedWidget_h

#include "pqPropertyWidget.h"

class pqSHYXSnappyPatchTableWidget;
class vtkSMPropertyGroup;

/**
 * Castellated (细分) panel: flags plus Surface patches and Region patches tables.
 */
class pqSHYXSnappyCastellatedWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXSnappyCastellatedWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXSnappyCastellatedWidget() override;

  void apply() override;
  void reset() override;

private:
  pqSHYXSnappyPatchTableWidget* SurfacesTable = nullptr;
  pqSHYXSnappyPatchTableWidget* RegionsTable = nullptr;
};

#endif
