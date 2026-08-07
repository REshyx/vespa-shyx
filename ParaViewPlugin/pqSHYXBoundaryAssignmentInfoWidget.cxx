#include "pqSHYXBoundaryAssignmentInfoWidget.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
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
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

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

} // namespace

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

  auto* exportBtn =
    new QPushButton(tr("Export port 0 (.exo) + assignment/options (.txt)"), this);
  exportBtn->setToolTip(tr(
    "Choose a .exo path. Writes the Exodus file (vtkIOSSWriter defaults) plus "
    "<name>_boundary_assignment.txt and <name>_options.txt beside it."));
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

  const QString exoPath = QFileDialog::getSaveFileName(pqCoreUtilities::mainWidget(),
    tr("Export Exodus + assignment/options texts"), QString(),
    tr("Exodus (*.exo);;All files (*)"));
  if (exoPath.isEmpty())
  {
    return;
  }

  QString exoFile = exoPath;
  if (!exoFile.endsWith(QStringLiteral(".exo"), Qt::CaseInsensitive))
  {
    exoFile += QStringLiteral(".exo");
  }
  const QFileInfo fi(exoFile);
  const QString stemPath = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
  const QString assignPath = stemPath + QStringLiteral("_boundary_assignment.txt");
  const QString optPath = stemPath + QStringLiteral("_options.txt");

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
  if (!writeTextFile(assignPath, assignText, &error) || !writeTextFile(optPath, optText, &error))
  {
    QMessageBox::critical(this, tr("Export"), error);
    return;
  }

  QMessageBox::information(this, tr("Export"),
    tr("Wrote:\n%1\n%2\n%3").arg(exoFile, assignPath, optPath));
}
