#ifndef pqSHYXBoundaryAssignmentInfoWidget_h
#define pqSHYXBoundaryAssignmentInfoWidget_h

#include "pqPropertyWidget.h"

#include <QPointer>

class QTextEdit;
class pqPipelineSource;
class vtkSMPropertyGroup;
class vtkSMStringVectorProperty;

/**
 * Read-only multiline texts for Boundary Assignment / full options file, plus one-click
 * export of port 0 (Exodus via vtkIOSSWriter) and the two text snippets.
 *
 * Default information_only multi_line widgets refresh before RequestData finishes,
 * so the panel stays one Apply behind. This widget re-pulls after dataUpdated.
 */
class pqSHYXBoundaryAssignmentInfoWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXBoundaryAssignmentInfoWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXBoundaryAssignmentInfoWidget() override;

private Q_SLOTS:
  void refreshTexts();
  void onExportClicked();
  void configureDebugPointLabels();

private:
  void setTextFromProperty(QTextEdit* edit, vtkSMStringVectorProperty* prop);

  QPointer<pqPipelineSource> PipelineSource;
  QTextEdit* AssignmentEdit = nullptr;
  QTextEdit* InletOptEdit = nullptr;
  vtkSMStringVectorProperty* AssignmentProp = nullptr;
  vtkSMStringVectorProperty* InletOptProp = nullptr;
};

#endif
