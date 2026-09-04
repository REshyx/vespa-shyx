#ifndef pqSHYXMultiSelectionInputWidget_h
#define pqSHYXMultiSelectionInputWidget_h

#include "pqPropertyWidget.h"

class QLabel;
class vtkSMProperty;
class vtkSMProxy;

/**
 * Selection property widget for filters with repeatable Input + Selection.
 * Copies each Input connection's GetSelectionInput() in Input order (empty ID
 * source as a placeholder so indices stay aligned). Stock Copy Active Selection
 * only clones the selection-manager primary port.
 *
 * After loading a .pvsm the producers no longer hold a view selection, but this
 * filter's Selection property does. Do not recopy from live Inputs in that case.
 */
class pqSHYXMultiSelectionInputWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXMultiSelectionInputWidget(
    vtkSMProxy* smproxy, vtkSMProperty* smproperty, QWidget* parent = nullptr);
  ~pqSHYXMultiSelectionInputWidget() override;

  void apply() override;
  void reset() override;

private Q_SLOTS:
  void copyFromInputs();
  void onInputsChanged();
  void updateStatusFromProperty();

private:
  vtkSMProperty* selectionProperty() const;
  void registerCopiedProxies();
  void unregisterOrphanSelectionSources();

  vtkSMProperty* SelectionProperty = nullptr;
  QLabel* StatusLabel = nullptr;
  bool Copying = false;

  Q_DISABLE_COPY(pqSHYXMultiSelectionInputWidget)
};

#endif
