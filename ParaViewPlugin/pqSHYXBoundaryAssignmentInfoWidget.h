#ifndef pqSHYXBoundaryAssignmentInfoWidget_h
#define pqSHYXBoundaryAssignmentInfoWidget_h

#include "pqPropertyWidget.h"

#include <QPointer>
#include <QString>

class QLineEdit;
class QTextEdit;
class pqPipelineSource;
class vtkSMPropertyGroup;
class vtkSMProxy;
class vtkSMStringVectorProperty;

/**
 * Read-only multiline texts for Boundary Assignment / full options file, plus one-click
 * export of port 0 (Exodus via vtkIOSSWriter) and the two text snippets.
 *
 * Default information_only multi_line widgets refresh before RequestData finishes,
 * so the panel stays one Apply behind. This widget re-pulls after dataUpdated.
 *
 * Export basename defaults follow the top-level pipeline reader FileName / FileNames
 * stem (foo.exo / foo.opt / foo.bc). Falls back to PV_0 / HV_0 when no file is found.
 *
 * FlowBoundaryMode is auto-picked from the upstream file basename (case-insensitive):
 * plaque → Single inlet (PV); aorta → Single outlet (HV). Re-applied only when the
 * upstream path changes, so manual mode edits are kept until the source file changes.
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
  void syncFlowModeFromUpstreamFile();

private:
  void setTextFromProperty(QTextEdit* edit, vtkSMStringVectorProperty* prop);
  QString currentModeTag() const;
  /** Stem for auto export names: upstream reader file stem, else PV/HV mode tag. */
  QString resolveExportStem() const;
  QString upstreamFilePath() const;
  static QString filePathFromProxy(vtkSMProxy* proxy);
  static vtkSMProxy* topLevelProducer(vtkSMProxy* proxy);
  /** 0 = Single inlet (PV), 1 = Single outlet (HV), -1 = no / ambiguous hint. */
  static int inferFlowModeFromFileName(const QString& filePath);
  static QString defaultExoName(const QString& stem);
  static QString defaultOptName(const QString& stem);
  static QString defaultBcName(const QString& stem);

  QPointer<pqPipelineSource> PipelineSource;
  QTextEdit* AssignmentEdit = nullptr;
  QTextEdit* InletOptEdit = nullptr;
  QLineEdit* ExoNameEdit = nullptr;
  QLineEdit* OptNameEdit = nullptr;
  QLineEdit* BcNameEdit = nullptr;
  vtkSMStringVectorProperty* AssignmentProp = nullptr;
  vtkSMStringVectorProperty* InletOptProp = nullptr;
  QString LastAutoStem;
  /** Upstream path for which FlowBoundaryMode was last auto-applied (may be empty). */
  QString LastHintFilePath;
  bool ApplyingAutoFlowMode = false;
};

#endif
