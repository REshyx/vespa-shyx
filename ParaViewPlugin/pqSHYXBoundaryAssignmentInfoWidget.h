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
 * Export name defaults from top-level reader FileName / FileNames, e.g.
 * K2-1_plaque.stl → PV_K2-1.exo / options_PV_K2-1 / Nodeset_PV_K2-1
 * (options and Nodeset have no file extension). Falls back to PV_0 / HV_0.
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
  /** Case id from upstream file (K2-1 from K2-1_plaque.stl), else "0". */
  QString resolveCaseId() const;
  /** Sync key: "PV|K2-1" — changes with mode and/or upstream file. */
  QString resolveExportKey() const;
  QString upstreamFilePath() const;
  static QString filePathFromProxy(vtkSMProxy* proxy);
  static vtkSMProxy* topLevelProducer(vtkSMProxy* proxy);
  static QString caseIdFromFilePath(const QString& filePath);
  /** 0 = Single inlet (PV), 1 = Single outlet (HV), -1 = no / ambiguous hint. */
  static int inferFlowModeFromFileName(const QString& filePath);
  static QString defaultExoName(const QString& tag, const QString& caseId);
  static QString defaultOptName(const QString& tag, const QString& caseId);
  static QString defaultNodesetName(const QString& tag, const QString& caseId);
  static void splitExportKey(const QString& key, QString& tag, QString& caseId);

  QPointer<pqPipelineSource> PipelineSource;
  QTextEdit* AssignmentEdit = nullptr;
  QTextEdit* InletOptEdit = nullptr;
  QLineEdit* ExoNameEdit = nullptr;
  QLineEdit* OptNameEdit = nullptr;
  QLineEdit* BcNameEdit = nullptr;
  vtkSMStringVectorProperty* AssignmentProp = nullptr;
  vtkSMStringVectorProperty* InletOptProp = nullptr;
  QString LastAutoKey;
  /** Upstream path for which FlowBoundaryMode was last auto-applied (may be empty). */
  QString LastHintFilePath;
  bool ApplyingAutoFlowMode = false;
};

#endif
