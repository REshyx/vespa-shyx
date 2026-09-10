#include "pqSHYXSkeletonExtractionAutoStart.h"

#include "pqApplicationCore.h"
#include "pqDataRepresentation.h"
#include "pqObjectBuilder.h"
#include "pqOutputPort.h"
#include "pqPipelineFilter.h"
#include "pqPipelineSource.h"
#include "pqProxy.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMTrace.h"

#include <cstring>

namespace
{
constexpr const char* kFilterXmlName = "SHYXSkeletonExtraction";
constexpr double kParentOpacity = 0.5;

bool isSkeletonExtraction(pqPipelineSource* source)
{
  vtkSMProxy* proxy = source ? source->getProxy() : nullptr;
  const char* xmlName = proxy ? proxy->GetXMLName() : nullptr;
  return xmlName && std::strcmp(xmlName, kFilterXmlName) == 0;
}
}

//-----------------------------------------------------------------------------
pqSHYXSkeletonExtractionAutoStart::pqSHYXSkeletonExtractionAutoStart(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXSkeletonExtractionAutoStart::~pqSHYXSkeletonExtractionAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXSkeletonExtractionAutoStart::onStartup()
{
  pqApplicationCore* core = pqApplicationCore::instance();
  if (!core)
  {
    return;
  }

  if (pqObjectBuilder* builder = core->getObjectBuilder())
  {
    QObject::connect(builder, &pqObjectBuilder::filterCreated, this,
      &pqSHYXSkeletonExtractionAutoStart::watchFilter);
  }

  if (pqServerManagerModel* sm = core->getServerManagerModel())
  {
    Q_FOREACH (pqPipelineSource* source, sm->findItems<pqPipelineSource*>())
    {
      if (source->modifiedState() == pqProxy::UNINITIALIZED)
      {
        this->watchFilter(source);
      }
    }
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSkeletonExtractionAutoStart::onShutdown()
{
  this->Pending.clear();
}

//-----------------------------------------------------------------------------
void pqSHYXSkeletonExtractionAutoStart::watchFilter(pqPipelineSource* source)
{
  auto* filter = qobject_cast<pqPipelineFilter*>(source);
  if (!filter || !isSkeletonExtraction(filter) || this->Pending.contains(filter))
  {
    return;
  }

  this->Pending.insert(filter);
  QObject::connect(filter, &QObject::destroyed, this, [this, filter]() {
    this->Pending.remove(filter);
  });
  QObject::connect(filter, &pqProxy::modifiedStateChanged, this, [this, filter]() {
    if (!this->Pending.contains(filter) || filter->modifiedState() != pqProxy::UNMODIFIED)
    {
      return;
    }
    this->Pending.remove(filter);
    this->fadeInputRepresentations(filter);
  });
}

//-----------------------------------------------------------------------------
void pqSHYXSkeletonExtractionAutoStart::fadeInputRepresentations(pqPipelineFilter* filter)
{
  if (!filter)
  {
    return;
  }

  Q_FOREACH (pqOutputPort* input, filter->getAllInputs())
  {
    if (!input)
    {
      continue;
    }

    Q_FOREACH (pqDataRepresentation* inputRepr, input->getRepresentations(nullptr))
    {
      vtkSMProxy* reprProxy = inputRepr ? inputRepr->getProxy() : nullptr;
      if (!reprProxy || !reprProxy->GetProperty("Opacity"))
      {
        continue;
      }

      SM_SCOPED_TRACE(PropertiesModified).arg("proxy", reprProxy);
      vtkSMPropertyHelper(reprProxy, "Opacity").Set(kParentOpacity);
      reprProxy->UpdateVTKObjects();
    }
  }
}
