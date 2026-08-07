#ifndef pqSHYXBoundaryAssignmentInfoWidget_h
#define pqSHYXBoundaryAssignmentInfoWidget_h

#include "pqPropertyWidget.h"

#include <QPointer>
#include <QString>

class QLineEdit;
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
  void syncExportNameDefaults();

private:
  void setTextFromProperty(QTextEdit* edit, vtkSMStringVectorProperty* prop);
  QString currentModeTag() const;
  static QString defaultExoName(const QString& tag);
  static QString defaultOptName(const QString& tag);
  static QString defaultBcName(const QString& tag);

  QPointer<pqPipelineSource> PipelineSource;
  QTextEdit* AssignmentEdit = nullptr;
  QTextEdit* InletOptEdit = nullptr;
  QLineEdit* ExoNameEdit = nullptr;
  QLineEdit* OptNameEdit = nullptr;
  QLineEdit* BcNameEdit = nullptr;
  vtkSMStringVectorProperty* AssignmentProp = nullptr;
  vtkSMStringVectorProperty* InletOptProp = nullptr;
  QString LastAutoTag;
};

#endif
