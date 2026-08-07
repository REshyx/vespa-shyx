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
QString pqSHYXBoundaryAssignmentInfoWidget::defaultExoName(const QString& tag)
{
  return QStringLiteral("%1_0.exo").arg(tag);
}

QString pqSHYXBoundaryAssignmentInfoWidget::defaultOptName(const QString& tag)
{
  return QStringLiteral("options_%1_0.opt").arg(tag);
}

QString pqSHYXBoundaryAssignmentInfoWidget::defaultBcName(const QString& tag)
{
  return QStringLiteral("options_%1_0.bc").arg(tag);
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
  this->ExoNameEdit->setPlaceholderText(QStringLiteral("PV_0.exo / HV_0.exo"));
  this->OptNameEdit->setPlaceholderText(QStringLiteral("options_PV_0.opt / options_HV_0.opt"));
  this->BcNameEdit->setPlaceholderText(QStringLiteral("options_PV_0.bc / options_HV_0.bc"));
  this->ExoNameEdit->setToolTip(
    tr("Exodus output basename. Default PV_0.exo (Single inlet) or HV_0.exo (Single outlet)."));
  this->OptNameEdit->setToolTip(tr(
    "Options file (.opt) basename for the full solver options text. "
    "Default options_PV_0.opt or options_HV_0.opt."));
  this->BcNameEdit->setToolTip(tr(
    "Boundary assignment (.bc) basename. Default options_PV_0.bc or options_HV_0.bc."));
  namesForm->addRow(tr("Exodus (.exo)"), this->ExoNameEdit);
  namesForm->addRow(tr("Options (.opt)"), this->OptNameEdit);
  namesForm->addRow(tr("Boundary assignment (.bc)"), this->BcNameEdit);
  vbox->addWidget(namesHost);

  this->LastAutoTag = this->currentModeTag();
  this->ExoNameEdit->setText(defaultExoName(this->LastAutoTag));
  this->OptNameEdit->setText(defaultOptName(this->LastAutoTag));
  this->BcNameEdit->setText(defaultBcName(this->LastAutoTag));

  if (vtkSMProperty* modeProp = smproxy->GetProperty("FlowBoundaryMode"))
  {
    pqCoreUtilities::connect(
      modeProp, vtkCommand::ModifiedEvent, this, SLOT(syncExportNameDefaults()));
  }

  auto* exportBtn =
    new QPushButton(tr("Export port 0 (.exo) + options (.opt) + assignment (.bc)"), this);
  exportBtn->setToolTip(tr(
    "Choose a folder via the .exo save dialog (suggested name from the box above). "
    "Writes the Exodus file plus the .opt and .bc files beside it using the names above."));
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
void pqSHYXBoundaryAssignmentInfoWidget::syncExportNameDefaults()
{
  const QString tag = this->currentModeTag();
  if (tag == this->LastAutoTag)
  {
    return;
  }

  const QString oldExo = defaultExoName(this->LastAutoTag);
  const QString oldOpt = defaultOptName(this->LastAutoTag);
  const QString oldBc = defaultBcName(this->LastAutoTag);
  const QString newExo = defaultExoName(tag);
  const QString newOpt = defaultOptName(tag);
  const QString newBc = defaultBcName(tag);

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
  this->LastAutoTag = tag;
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

  const QString tag = this->currentModeTag();
  QString exoName = ensureExtension(
    this->ExoNameEdit ? this->ExoNameEdit->text() : QString(), QStringLiteral(".exo"));
  QString optName = ensureExtension(
    this->OptNameEdit ? this->OptNameEdit->text() : QString(), QStringLiteral(".opt"));
  QString bcName = ensureExtension(
    this->BcNameEdit ? this->BcNameEdit->text() : QString(), QStringLiteral(".bc"));
  if (exoName.isEmpty())
  {
    exoName = defaultExoName(tag);
  }
  if (optName.isEmpty())
  {
    optName = defaultOptName(tag);
  }
  if (bcName.isEmpty())
  {
    bcName = defaultBcName(tag);
  }

  const QString exoPath = QFileDialog::getSaveFileName(pqCoreUtilities::mainWidget(),
    tr("Export Exodus + options (.opt) + assignment (.bc)"), exoName,
    tr("Exodus (*.exo);;All files (*)"));
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
