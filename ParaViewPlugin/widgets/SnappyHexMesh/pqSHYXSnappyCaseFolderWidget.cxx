#include "pqSHYXSnappyCaseFolderWidget.h"

#include "pqApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"

#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>

//-----------------------------------------------------------------------------
pqSHYXSnappyCaseFolderWidget::pqSHYXSnappyCaseFolderWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(4);

  this->PathEdit = new QLineEdit(this);
  this->PathEdit->setObjectName("SHYXSnappyCaseFolderPath");
  this->PathEdit->setPlaceholderText(tr("Leave empty to write under %TEMP%"));
  this->PathEdit->setClearButtonEnabled(true);
  row->addWidget(this->PathEdit, /*stretch=*/1);

  this->BrowseButton = new QPushButton(QStringLiteral("..."), this);
  this->BrowseButton->setObjectName("SHYXSnappyCaseFolderBrowse");
  this->BrowseButton->setToolTip(tr("Browse"));
  this->BrowseButton->setFixedWidth(32);
  row->addWidget(this->BrowseButton);

  this->OpenButton = new QPushButton(QStringLiteral("📂"), this);
  this->OpenButton->setObjectName("SHYXSnappyCaseFolderOpen");
  this->OpenButton->setToolTip(tr("Open folder"));
  this->OpenButton->setFixedWidth(32);
  this->OpenButton->setEnabled(false);
  row->addWidget(this->OpenButton);

  if (smgroup)
  {
    this->DirProp = vtkSMStringVectorProperty::SafeDownCast(smgroup->GetProperty("CaseDirectory"));
    this->PathProp = vtkSMStringVectorProperty::SafeDownCast(smgroup->GetProperty("CaseFoamPath"));
  }
  if (smproxy)
  {
    if (!this->DirProp)
    {
      this->DirProp =
        vtkSMStringVectorProperty::SafeDownCast(smproxy->GetProperty("CaseDirectory"));
    }
    if (!this->PathProp)
    {
      this->PathProp =
        vtkSMStringVectorProperty::SafeDownCast(smproxy->GetProperty("CaseFoamPath"));
    }
  }

  if (this->DirProp)
  {
    this->addPropertyLink(this->PathEdit, "text", SIGNAL(textChanged(QString)), smproxy, this->DirProp);
    const char* cur =
      (this->DirProp->GetNumberOfElements() > 0) ? this->DirProp->GetElement(0) : nullptr;
    this->PathEdit->setText(QString::fromUtf8(cur ? cur : ""));
  }

  QObject::connect(this->BrowseButton, &QPushButton::clicked, this,
    &pqSHYXSnappyCaseFolderWidget::browseFolder);
  QObject::connect(this->OpenButton, &QPushButton::clicked, this,
    &pqSHYXSnappyCaseFolderWidget::openFolder);
  QObject::connect(this->PathEdit, &QLineEdit::textChanged, this,
    &pqSHYXSnappyCaseFolderWidget::refreshOpenButton);

  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    this->PipelineSource = smm->findItem<pqPipelineSource*>(smproxy);
    if (this->PipelineSource)
    {
      QObject::connect(this->PipelineSource.data(),
        static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(&pqPipelineSource::dataUpdated),
        this, [this](pqPipelineSource*) { this->refreshOpenButton(); });
    }
  }

  this->refreshOpenButton();
}

//-----------------------------------------------------------------------------
pqSHYXSnappyCaseFolderWidget::~pqSHYXSnappyCaseFolderWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXSnappyCaseFolderWidget::setReadOnly(bool readOnly)
{
  this->PathEdit->setReadOnly(readOnly);
  this->BrowseButton->setEnabled(!readOnly);
}

//-----------------------------------------------------------------------------
QString pqSHYXSnappyCaseFolderWidget::resolvedFolder() const
{
  auto pullInfo = [this]() -> QString {
    if (!this->PathProp || !this->proxy())
    {
      return {};
    }
    if (auto* source = vtkSMSourceProxy::SafeDownCast(this->proxy()))
    {
      source->UpdatePropertyInformation(this->PathProp);
    }
    else
    {
      this->proxy()->UpdatePropertyInformation(this->PathProp);
    }
    const char* value =
      (this->PathProp->GetNumberOfElements() > 0) ? this->PathProp->GetElement(0) : nullptr;
    return QString::fromUtf8(value ? value : "").trimmed();
  };

  const QString written = pullInfo();
  if (!written.isEmpty() && QFileInfo(written).isDir())
  {
    return written;
  }
  const QString typed = this->PathEdit ? this->PathEdit->text().trimmed() : QString();
  if (!typed.isEmpty() && QFileInfo(typed).isDir())
  {
    return typed;
  }
  return {};
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyCaseFolderWidget::browseFolder()
{
  const QString start = this->PathEdit->text().trimmed();
  const QString dir = QFileDialog::getExistingDirectory(
    this, tr("Case Directory"), start, QFileDialog::ShowDirsOnly);
  if (dir.isEmpty())
  {
    return;
  }
  this->PathEdit->setText(QDir::toNativeSeparators(dir));
  this->refreshOpenButton();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyCaseFolderWidget::refreshOpenButton()
{
  if (!this->OpenButton)
  {
    return;
  }
  const QString path = this->resolvedFolder();
  this->OpenButton->setEnabled(!path.isEmpty());
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyCaseFolderWidget::openFolder()
{
  const QString path = this->resolvedFolder();
  if (path.isEmpty() || !QFileInfo(path).isDir())
  {
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::toNativeSeparators(path)));
}
