#include "pqSHYXBoundaryAssignmentInfoWidget.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkCommand.h"
#include "vtkDataAssembly.h"
#include "vtkErrorCode.h"
#include "vtkIOSSWriter.h"
#include "vtkNew.h"
#include "vtkPartitionedDataSetCollection.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#include <string>

namespace
{

vtkSMProperty* propertyFromGroup(
  vtkSMPropertyGroup* group, vtkSMProxy* proxy, const char* function, const char* fallbackName)
{
  if (group)
  {
    if (auto* p = group->GetProperty(function))
    {
      return p;
    }
    const unsigned int n = group->GetNumberOfProperties();
    for (unsigned int i = 0; i < n; ++i)
    {
      const char* name = group->GetPropertyName(i);
      if (name && fallbackName && std::string(name) == fallbackName)
      {
        return group->GetProperty(i);
      }
    }
  }
  return proxy ? proxy->GetProperty(fallbackName) : nullptr;
}

QTextEdit* makeReadonlyMultiline(QWidget* parent, int minRows)
{
  auto* edit = new QTextEdit(parent);
  edit->setReadOnly(true);
  edit->setAcceptRichText(false);
  edit->setLineWrapMode(QTextEdit::NoWrap);
  edit->setUndoRedoEnabled(false);
  QFont mono = edit->font();
  mono.setStyleHint(QFont::Monospace);
  mono.setFamily(QStringLiteral("Consolas"));
  edit->setFont(mono);
  const int line = edit->fontMetrics().lineSpacing();
  edit->setMinimumHeight(line * minRows + 12);
  return edit;
}

bool writeTextFile(const QString& path, const QString& text, QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
  {
    if (error)
    {
      *error = QObject::tr("Cannot write \"%1\": %2").arg(path, file.errorString());
    }
    return false;
  }
  QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  stream.setEncoding(QStringConverter::Utf8);
#else
  stream.setCodec("UTF-8");
#endif
  stream << text;
  if (stream.status() != QTextStream::Ok)
  {
    if (error)
    {
      *error = QObject::tr("Failed while writing \"%1\".").arg(path);
    }
    return false;
  }
  return true;
}

bool writeExodusIossDefault(vtkPartitionedDataSetCollection* input, const QString& filePath,
  QString* error)
{
  if (!input)
  {
    if (error)
    {
      *error = QObject::tr("Port 0 has no vtkPartitionedDataSetCollection.");
    }
    return false;
  }

  vtkNew<vtkIOSSWriter> writer;
  writer->SetInputData(input);
  writer->SetFileName(filePath.toUtf8().constData());

  vtkDataAssembly* asmTree = input->GetDataAssembly();
  const char* assemblyName =
    (asmTree && asmTree->GetRootNodeName() && asmTree->GetRootNodeName()[0] != '\0')
    ? asmTree->GetRootNodeName()
    : "IOSS";
  writer->SetAssemblyName(assemblyName);
  const std::string asmPrefix = std::string("/") + assemblyName;
  writer->AddElementBlockSelector((asmPrefix + "/element_blocks").c_str());
  writer->AddNodeSetSelector((asmPrefix + "/node_sets").c_str());
  writer->AddSideSetSelector((asmPrefix + "/side_sets").c_str());

  writer->Write();
  if (writer->GetErrorCode() != vtkErrorCode::NoError)
  {
    if (error)
    {
      *error = QObject::tr("vtkIOSSWriter failed for \"%1\" (error code %2).")
                 .arg(filePath)
                 .arg(static_cast<int>(writer->GetErrorCode()));
    }
    return false;
  }
  return true;
}

QString ensureExtension(QString name, const QString& ext)
{
  name = name.trimmed();
  if (name.isEmpty())
  {
    return name;
  }
  if (!name.endsWith(ext, Qt::CaseInsensitive))
  {
    name += ext;
  }
  return name;
}

} // namespace

//-----------------------------------------------------------------------------
QString pqSHYXBoundaryAssignmentInfoWidget::defaultExoName(
  const QString& tag, const QString& caseId)
{
  return QStringLiteral("%1_%2.exo").arg(tag, caseId);
}

QString pqSHYXBoundaryAssignmentInfoWidget::defaultOptName(
  const QString& tag, const QString& caseId)
{
  // No .opt suffix by design (solver expects extensionless options files).
  return QStringLiteral("options_%1_%2").arg(tag, caseId);
}

QString pqSHYXBoundaryAssignmentInfoWidget::defaultNodesetName(
  const QString& tag, const QString& caseId)
{
  // No .bc suffix by design.
  return QStringLiteral("Nodeset_%1_%2").arg(tag, caseId);
}

void pqSHYXBoundaryAssignmentInfoWidget::splitExportKey(
  const QString& key, QString& tag, QString& caseId)
{
  const int bar = key.indexOf(QLatin1Char('|'));
  if (bar < 0)
  {
    tag = QStringLiteral("PV");
    caseId = key.isEmpty() ? QStringLiteral("0") : key;
    return;
  }
  tag = key.left(bar);
  caseId = key.mid(bar + 1);
  if (tag.isEmpty())
  {
    tag = QStringLiteral("PV");
  }
  if (caseId.isEmpty())
  {
    caseId = QStringLiteral("0");
  }
}

QString pqSHYXBoundaryAssignmentInfoWidget::currentModeTag() const
{
  vtkSMProxy* filter = this->proxy();
  if (!filter || !filter->GetProperty("FlowBoundaryMode"))
  {
    return QStringLiteral("PV");
  }
  // 0 = Single inlet → PV; 1 = Single outlet → HV
  const int mode = vtkSMPropertyHelper(filter, "FlowBoundaryMode").GetAsInt();
  return mode == 0 ? QStringLiteral("PV") : QStringLiteral("HV");
}

vtkSMProxy* pqSHYXBoundaryAssignmentInfoWidget::topLevelProducer(vtkSMProxy* proxy)
{
  vtkSMProxy* cur = proxy;
  // Bound the walk in case of unusual cycles / multi-input graphs.
  for (int guard = 0; cur && guard < 64; ++guard)
  {
    vtkSMProperty* inputProp = cur->GetProperty("Input");
    if (!inputProp)
    {
      break;
    }
    vtkSMProxy* upstream = vtkSMPropertyHelper(inputProp).GetAsProxy(0);
    if (!upstream)
    {
      break;
    }
    cur = upstream;
  }
  return cur;
}

QString pqSHYXBoundaryAssignmentInfoWidget::filePathFromProxy(vtkSMProxy* proxy)
{
  if (!proxy)
  {
    return QString();
  }

  // Single-file readers (most common for Open File).
  if (vtkSMProperty* fn = proxy->GetProperty("FileName"))
  {
    const char* path = vtkSMPropertyHelper(fn).GetAsString();
    if (path && path[0] != '\0')
    {
      return QString::fromUtf8(path);
    }
  }

  // Multi-file readers: use the first listed path.
  if (vtkSMProperty* fns = proxy->GetProperty("FileNames"))
  {
    vtkSMPropertyHelper helper(fns);
    if (helper.GetNumberOfElements() > 0)
    {
      const char* path = helper.GetAsString(0);
      if (path && path[0] != '\0')
      {
        return QString::fromUtf8(path);
      }
    }
  }

  return QString();
}

QString pqSHYXBoundaryAssignmentInfoWidget::upstreamFilePath() const
{
  return filePathFromProxy(topLevelProducer(this->proxy()));
}

QString pqSHYXBoundaryAssignmentInfoWidget::caseIdFromFilePath(const QString& filePath)
{
  if (filePath.isEmpty())
  {
    return QString();
  }
  QString stem = QFileInfo(filePath).completeBaseName().trimmed();
  if (stem.isEmpty())
  {
    return QString();
  }
  // K2-1_plaque / K2-1_aorta → K2-1
  const QString lower = stem.toLower();
  if (lower.endsWith(QStringLiteral("_plaque")))
  {
    stem.chop(7);
  }
  else if (lower.endsWith(QStringLiteral("_aorta")))
  {
    stem.chop(6);
  }
  return stem.trimmed();
}

int pqSHYXBoundaryAssignmentInfoWidget::inferFlowModeFromFileName(const QString& filePath)
{
  if (filePath.isEmpty())
  {
    return -1;
  }
  // Match against the basename only (not the directory path).
  const QString name = QFileInfo(filePath).fileName().toLower();
  const bool aorta = name.contains(QStringLiteral("aorta"));
  const bool plaque = name.contains(QStringLiteral("plaque"));
  if (plaque && !aorta)
  {
    return 0; // Single inlet → PV
  }
  if (aorta && !plaque)
  {
    return 1; // Single outlet → HV
  }
  return -1; // none or ambiguous
}

QString pqSHYXBoundaryAssignmentInfoWidget::resolveCaseId() const
{
  const QString id = caseIdFromFilePath(this->upstreamFilePath());
  return id.isEmpty() ? QStringLiteral("0") : id;
}

QString pqSHYXBoundaryAssignmentInfoWidget::resolveExportKey() const
{
  return this->currentModeTag() + QLatin1Char('|') + this->resolveCaseId();
}

//-----------------------------------------------------------------------------
pqSHYXBoundaryAssignmentInfoWidget::pqSHYXBoundaryAssignmentInfoWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  this->AssignmentProp = vtkSMStringVectorProperty::SafeDownCast(
    propertyFromGroup(smgroup, smproxy, "AssignmentText", "BoundaryAssignmentText"));
  this->InletOptProp = vtkSMStringVectorProperty::SafeDownCast(
    propertyFromGroup(smgroup, smproxy, "InletOptText", "InletOptText"));

  auto* namesHost = new QWidget(this);
  auto* namesForm = new QFormLayout(namesHost);
  namesForm->setContentsMargins(0, 0, 0, 0);
  namesForm->setSpacing(4);
  this->ExoNameEdit = new QLineEdit(namesHost);
  this->OptNameEdit = new QLineEdit(namesHost);
  this->BcNameEdit = new QLineEdit(namesHost);
  this->ExoNameEdit->setPlaceholderText(QStringLiteral("PV_K2-1.exo / HV_K2-1.exo"));
  this->OptNameEdit->setPlaceholderText(QStringLiteral("options_PV_K2-1 / options_HV_K2-1"));
  this->BcNameEdit->setPlaceholderText(QStringLiteral("Nodeset_PV_K2-1 / Nodeset_HV_K2-1"));
  this->ExoNameEdit->setToolTip(tr(
    "Exodus output basename. Default PV_<case>.exo or HV_<case>.exo from the top-level "
    "source (e.g. K2-1_plaque.stl → PV_K2-1.exo). Falls back to PV_0.exo / HV_0.exo."));
  this->OptNameEdit->setToolTip(tr(
    "Options file basename (no extension). Default options_PV_<case> / options_HV_<case>."));
  this->BcNameEdit->setToolTip(tr(
    "Boundary assignment / nodeset basename (no extension). "
    "Default Nodeset_PV_<case> / Nodeset_HV_<case>."));
  namesForm->addRow(tr("Exodus (.exo)"), this->ExoNameEdit);
  namesForm->addRow(tr("Options (no ext)"), this->OptNameEdit);
  namesForm->addRow(tr("Nodeset (no ext)"), this->BcNameEdit);
  vbox->addWidget(namesHost);

  // Filename hint before wiring mode Modified → avoids a redundant name sync mid-setup.
  this->syncFlowModeFromUpstreamFile();
  {
    QString tag;
    QString caseId;
    this->LastAutoKey = this->resolveExportKey();
    splitExportKey(this->LastAutoKey, tag, caseId);
    this->ExoNameEdit->setText(defaultExoName(tag, caseId));
    this->OptNameEdit->setText(defaultOptName(tag, caseId));
    this->BcNameEdit->setText(defaultNodesetName(tag, caseId));
  }

  if (vtkSMProperty* modeProp = smproxy->GetProperty("FlowBoundaryMode"))
  {
    pqCoreUtilities::connect(
      modeProp, vtkCommand::ModifiedEvent, this, SLOT(syncExportNameDefaults()));
  }
  if (vtkSMProperty* inputProp = smproxy->GetProperty("Input"))
  {
    // Re-resolve mode + names when the upstream connection changes.
    pqCoreUtilities::connect(
      inputProp, vtkCommand::ModifiedEvent, this, SLOT(syncFlowModeFromUpstreamFile()));
    pqCoreUtilities::connect(
      inputProp, vtkCommand::ModifiedEvent, this, SLOT(syncExportNameDefaults()));
  }

  auto* exportBtn =
    new QPushButton(tr("Export port 0 (.exo) + options + Nodeset"), this);
  exportBtn->setToolTip(tr(
    "Choose a folder via the .exo save dialog (suggested name from the box above). "
    "Writes the Exodus file plus extensionless options and Nodeset files beside it."));
  vbox->addWidget(exportBtn);
  QObject::connect(exportBtn, &QPushButton::clicked, this,
    &pqSHYXBoundaryAssignmentInfoWidget::onExportClicked);

  auto* assignLabel = new QLabel(tr("Boundary assignment (id/nodeset/sideset)"), this);
  vbox->addWidget(assignLabel);
  this->AssignmentEdit = makeReadonlyMultiline(this, 14);
  vbox->addWidget(this->AssignmentEdit);

  auto* optLabel = new QLabel(tr("Options file (OPT)"), this);
  vbox->addWidget(optLabel);
  this->InletOptEdit = makeReadonlyMultiline(this, 22);
  vbox->addWidget(this->InletOptEdit);

  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    this->PipelineSource = smm->findItem<pqPipelineSource*>(smproxy);
    if (this->PipelineSource)
    {
      QObject::connect(this->PipelineSource.data(),
        static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(&pqPipelineSource::dataUpdated),
        this, [this](pqPipelineSource*) {
          this->refreshTexts();
          this->configureDebugPointLabels();
          // Representation may appear one tick after Apply; force OccludeLabels off again.
          QTimer::singleShot(0, this, [this]() { this->configureDebugPointLabels(); });
        });
    }
  }

  this->refreshTexts();
}

//-----------------------------------------------------------------------------
pqSHYXBoundaryAssignmentInfoWidget::~pqSHYXBoundaryAssignmentInfoWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::syncFlowModeFromUpstreamFile()
{
  if (this->ApplyingAutoFlowMode)
  {
    return;
  }

  const QString path = this->upstreamFilePath();
  if (path == this->LastHintFilePath)
  {
    return;
  }
  this->LastHintFilePath = path;

  const int inferred = inferFlowModeFromFileName(path);
  if (inferred < 0)
  {
    return;
  }

  vtkSMProxy* filter = this->proxy();
  vtkSMProperty* modeProp = filter ? filter->GetProperty("FlowBoundaryMode") : nullptr;
  if (!modeProp)
  {
    return;
  }

  const int current = vtkSMPropertyHelper(modeProp).GetAsInt();
  if (current == inferred)
  {
    return;
  }

  this->ApplyingAutoFlowMode = true;
  vtkSMPropertyHelper(modeProp).Set(inferred);
  filter->UpdateVTKObjects();
  this->ApplyingAutoFlowMode = false;
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::syncExportNameDefaults()
{
  if (this->ApplyingAutoFlowMode)
  {
    // Mode auto-set will be followed by an explicit name sync from Input / ctor / refresh.
    return;
  }

  const QString key = this->resolveExportKey();
  if (key == this->LastAutoKey)
  {
    return;
  }

  QString oldTag;
  QString oldCaseId;
  QString newTag;
  QString newCaseId;
  splitExportKey(this->LastAutoKey, oldTag, oldCaseId);
  splitExportKey(key, newTag, newCaseId);

  const QString oldExo = defaultExoName(oldTag, oldCaseId);
  const QString oldOpt = defaultOptName(oldTag, oldCaseId);
  const QString oldBc = defaultNodesetName(oldTag, oldCaseId);
  const QString newExo = defaultExoName(newTag, newCaseId);
  const QString newOpt = defaultOptName(newTag, newCaseId);
  const QString newBc = defaultNodesetName(newTag, newCaseId);

  // Preserve manual edits: only rewrite boxes that still hold the previous auto default.
  if (this->ExoNameEdit && this->ExoNameEdit->text().trimmed() == oldExo)
  {
    this->ExoNameEdit->setText(newExo);
  }
  if (this->OptNameEdit && this->OptNameEdit->text().trimmed() == oldOpt)
  {
    this->OptNameEdit->setText(newOpt);
  }
  if (this->BcNameEdit && this->BcNameEdit->text().trimmed() == oldBc)
  {
    this->BcNameEdit->setText(newBc);
  }
  this->LastAutoKey = key;
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::setTextFromProperty(
  QTextEdit* edit, vtkSMStringVectorProperty* prop)
{
  if (!edit)
  {
    return;
  }
  if (!prop || prop->GetNumberOfElements() < 1)
  {
    edit->setPlainText(QString());
    return;
  }
  const char* value = prop->GetElement(0);
  edit->setPlainText(QString::fromUtf8(value ? value : ""));
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::refreshTexts()
{
  vtkSMProxy* filter = this->proxy();
  if (!filter)
  {
    return;
  }

  this->syncFlowModeFromUpstreamFile();
  this->syncExportNameDefaults();

  auto* source = vtkSMSourceProxy::SafeDownCast(filter);
  if (this->AssignmentProp)
  {
    if (source)
    {
      source->UpdatePropertyInformation(this->AssignmentProp);
    }
    else
    {
      filter->UpdatePropertyInformation(this->AssignmentProp);
    }
  }
  if (this->InletOptProp)
  {
    if (source)
    {
      source->UpdatePropertyInformation(this->InletOptProp);
    }
    else
    {
      filter->UpdatePropertyInformation(this->InletOptProp);
    }
  }

  this->setTextFromProperty(this->AssignmentEdit, this->AssignmentProp);
  this->setTextFromProperty(this->InletOptEdit, this->InletOptProp);
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::configureDebugPointLabels()
{
  if (!this->PipelineSource)
  {
    return;
  }
  pqOutputPort* port1 = this->PipelineSource->getOutputPort(1);
  if (!port1)
  {
    return;
  }

  auto applyToProxy = [](vtkSMProxy* proxy) {
    if (!proxy)
    {
      return;
    }
    auto setInt = [proxy](const char* name, int value) {
      if (vtkSMProperty* p = proxy->GetProperty(name))
      {
        vtkSMPropertyHelper(p).Set(value);
      }
    };
    auto setStr = [proxy](const char* name, const char* value) {
      if (vtkSMProperty* p = proxy->GetProperty(name))
      {
        vtkSMPropertyHelper(p).Set(value);
      }
    };

    // Display proxy exposes Point Label settings as PL_* (GetSubProxy is protected).
    setStr("Representation", "Point Label");
    setInt("PL_ShowPointLabels", 1);
    setInt("PL_OccludeLabels", 0); // port-1 only: always-on-top 2D overlay
    setInt("PL_VertexOnly", 1);
    setStr("PL_PointLabelArray", "Label");
    proxy->UpdateVTKObjects();
  };

  const QList<pqDataRepresentation*> reps = port1->getRepresentations(nullptr);
  for (pqDataRepresentation* rep : reps)
  {
    if (!rep)
    {
      continue;
    }
    applyToProxy(rep->getProxy());
    if (pqView* view = rep->getView())
    {
      view->render();
    }
  }
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::onExportClicked()
{
  this->refreshTexts();

  auto* source = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!source)
  {
    QMessageBox::warning(this, tr("Export"), tr("Filter proxy is missing."));
    return;
  }

  QString tag;
  QString caseId;
  splitExportKey(this->resolveExportKey(), tag, caseId);

  QString exoName = ensureExtension(
    this->ExoNameEdit ? this->ExoNameEdit->text() : QString(), QStringLiteral(".exo"));
  // Options / Nodeset intentionally have no extension.
  QString optName = this->OptNameEdit ? this->OptNameEdit->text().trimmed() : QString();
  QString bcName = this->BcNameEdit ? this->BcNameEdit->text().trimmed() : QString();
  if (exoName.isEmpty())
  {
    exoName = defaultExoName(tag, caseId);
  }
  if (optName.isEmpty())
  {
    optName = defaultOptName(tag, caseId);
  }
  if (bcName.isEmpty())
  {
    bcName = defaultNodesetName(tag, caseId);
  }

  const QString exoPath = QFileDialog::getSaveFileName(pqCoreUtilities::mainWidget(),
    tr("Export Exodus + options + Nodeset"), exoName, tr("Exodus (*.exo);;All files (*)"));
  if (exoPath.isEmpty())
  {
    return;
  }

  QString exoFile = ensureExtension(exoPath, QStringLiteral(".exo"));
  const QString outDir = QFileInfo(exoFile).absolutePath();
  const QString optPath = outDir + QLatin1Char('/') + QFileInfo(optName).fileName();
  const QString bcPath = outDir + QLatin1Char('/') + QFileInfo(bcName).fileName();

  // Keep the exo line edit in sync with the path the user actually chose.
  if (this->ExoNameEdit)
  {
    this->ExoNameEdit->setText(QFileInfo(exoFile).fileName());
  }

  source->UpdatePipeline();
  source->UpdatePipelineInformation();

  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(source->GetClientSideObject());
  if (!alg)
  {
    QMessageBox::warning(this, tr("Export"),
      tr("Cannot access client-side filter (remote servers are not supported for this export)."));
    return;
  }

  auto* pdc = vtkPartitionedDataSetCollection::SafeDownCast(alg->GetOutputDataObject(0));
  QString error;
  if (!writeExodusIossDefault(pdc, exoFile, &error))
  {
    QMessageBox::critical(this, tr("Export"), error);
    return;
  }

  const QString assignText =
    this->AssignmentEdit ? this->AssignmentEdit->toPlainText() : QString();
  const QString optText = this->InletOptEdit ? this->InletOptEdit->toPlainText() : QString();
  if (!writeTextFile(bcPath, assignText, &error) || !writeTextFile(optPath, optText, &error))
  {
    QMessageBox::critical(this, tr("Export"), error);
    return;
  }

  QMessageBox::information(this, tr("Export"),
    tr("Wrote:\n%1\n%2\n%3").arg(exoFile, optPath, bcPath));
}
