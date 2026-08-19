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
  this->PathEdit->setReadOnly(true);
  this->PathEdit->setPlaceholderText(tr("Apply to keep the OpenFOAM case folder"));
  row->addWidget(this->PathEdit, /*stretch=*/1);

  this->OpenButton = new QPushButton(QStringLiteral("📂"), this);
  this->OpenButton->setObjectName("SHYXSnappyCaseFolderOpen");
  this->OpenButton->setToolTip(tr("Open folder"));
  this->OpenButton->setFixedWidth(32);
  this->OpenButton->setEnabled(false);
  row->addWidget(this->OpenButton);

  if (smgroup)
  {
    this->PathProp = vtkSMStringVectorProperty::SafeDownCast(smgroup->GetProperty("CaseFoamPath"));
  }
  if (!this->PathProp && smproxy)
  {
    this->PathProp = vtkSMStringVectorProperty::SafeDownCast(smproxy->GetProperty("CaseFoamPath"));
  }

  QObject::connect(this->OpenButton, &QPushButton::clicked, this,
    &pqSHYXSnappyCaseFolderWidget::openFolder);

  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    this->PipelineSource = smm->findItem<pqPipelineSource*>(smproxy);
    if (this->PipelineSource)
    {
      QObject::connect(this->PipelineSource.data(),
        static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(&pqPipelineSource::dataUpdated),
        this, [this](pqPipelineSource*) { this->refreshPath(); });
    }
  }

  this->refreshPath();
}

//-----------------------------------------------------------------------------
pqSHYXSnappyCaseFolderWidget::~pqSHYXSnappyCaseFolderWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXSnappyCaseFolderWidget::setReadOnly(bool)
{
  this->PathEdit->setReadOnly(true);
}

//-----------------------------------------------------------------------------
QString pqSHYXSnappyCaseFolderWidget::folderPath() const
{
  return this->PathEdit ? this->PathEdit->text().trimmed() : QString();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyCaseFolderWidget::refreshPath()
{
  vtkSMProxy* filter = this->proxy();
  if (!this->PathEdit)
  {
    return;
  }
  if (this->PathProp && filter)
  {
    if (auto* source = vtkSMSourceProxy::SafeDownCast(filter))
    {
      source->UpdatePropertyInformation(this->PathProp);
    }
    else
    {
      filter->UpdatePropertyInformation(this->PathProp);
    }
    const char* value =
      (this->PathProp->GetNumberOfElements() > 0) ? this->PathProp->GetElement(0) : nullptr;
    this->PathEdit->setText(QString::fromUtf8(value ? value : ""));
  }
  const QString path = this->folderPath();
  this->OpenButton->setEnabled(!path.isEmpty() && QFileInfo(path).isDir());
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyCaseFolderWidget::openFolder()
{
  const QString path = this->folderPath();
  if (path.isEmpty() || !QFileInfo(path).isDir())
  {
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::toNativeSeparators(path)));
}
