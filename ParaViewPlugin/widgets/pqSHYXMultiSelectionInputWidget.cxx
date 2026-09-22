#include "pqSHYXMultiSelectionInputWidget.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqPropertiesPanel.h"

#include "vtkCommand.h"
#include "vtkNew.h"
#include "vtkObject.h"
#include "vtkSMBoundsDomain.h"
#include "vtkSMInputProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMPropertyIterator.h"
#include "vtkSMProxy.h"
#include "vtkSMProxyIterator.h"
#include "vtkSMSession.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSmartPointer.h"

#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <string>

namespace
{

vtkSmartPointer<vtkSMSourceProxy> CloneOrEmptySelection(
  vtkSMSourceProxy* src, vtkSMSessionProxyManager* pxm)
{
  if (!pxm)
  {
    return nullptr;
  }

  vtkSmartPointer<vtkSMSourceProxy> clone;
  if (src && src->GetXMLGroup() && src->GetXMLName())
  {
    clone.TakeReference(
      vtkSMSourceProxy::SafeDownCast(pxm->NewProxy(src->GetXMLGroup(), src->GetXMLName())));
    if (clone)
    {
      clone->Copy(src);
      clone->UpdateVTKObjects();
      return clone;
    }
  }

  clone.TakeReference(
    vtkSMSourceProxy::SafeDownCast(pxm->NewProxy("sources", "IDSelectionSource")));
  if (clone)
  {
    vtkSMPropertyHelper(clone, "FieldType").Set(0);
    clone->UpdateVTKObjects();
  }
  return clone;
}

void RegisterSelectionTree(vtkSMProxy* proxy)
{
  if (!proxy)
  {
    return;
  }
  vtkSMSessionProxyManager* pxm = proxy->GetSessionProxyManager();
  if (!pxm)
  {
    return;
  }
  if (!pxm->GetProxyName("selection_sources", proxy))
  {
    const std::string key = std::string("selection_filter.") + proxy->GetGlobalIDAsString();
    pxm->RegisterProxy("selection_sources", key.c_str(), proxy);
  }
  auto* inputs = vtkSMInputProperty::SafeDownCast(proxy->GetProperty("Input"));
  if (!inputs)
  {
    return;
  }
  for (unsigned int i = 0; i < inputs->GetNumberOfProxies(); ++i)
  {
    RegisterSelectionTree(inputs->GetProxy(i));
  }
}

bool PropertyHasValues(vtkSMProxy* proxy, const char* name)
{
  vtkSMProperty* prop = proxy ? proxy->GetProperty(name) : nullptr;
  return prop && vtkSMPropertyHelper(prop).GetNumberOfElements() > 0;
}

bool SelectionLooksPopulated(vtkSMProxy* proxy)
{
  if (!proxy)
  {
    return false;
  }

  if (auto* inputs = vtkSMInputProperty::SafeDownCast(proxy->GetProperty("Input")))
  {
    const unsigned int n = inputs->GetNumberOfProxies();
    if (n > 0)
    {
      for (unsigned int i = 0; i < n; ++i)
      {
        if (SelectionLooksPopulated(inputs->GetProxy(i)))
        {
          return true;
        }
      }
      return false;
    }
  }

  if (PropertyHasValues(proxy, "IDs") || PropertyHasValues(proxy, "Values") ||
    PropertyHasValues(proxy, "GlobalIDs") || PropertyHasValues(proxy, "Blocks") ||
    PropertyHasValues(proxy, "BlockSelectors") || PropertyHasValues(proxy, "Locations") ||
    PropertyHasValues(proxy, "Thresholds") || PropertyHasValues(proxy, "CompositeIndex"))
  {
    return true;
  }

  // FrustumSelectionSource always has a 32-tuple Frustum array.
  if (proxy->GetProperty("Frustum"))
  {
    return true;
  }
  return false;
}

unsigned int CountPopulatedSelections(vtkSMProperty* selProp)
{
  if (!selProp)
  {
    return 0;
  }
  vtkSMPropertyHelper helper(selProp);
  unsigned int n = 0;
  for (unsigned int i = 0; i < helper.GetNumberOfElements(); ++i)
  {
    if (SelectionLooksPopulated(helper.GetAsProxy(i)))
    {
      ++n;
    }
  }
  return n;
}

} // namespace

pqSHYXMultiSelectionInputWidget::pqSHYXMultiSelectionInputWidget(
  vtkSMProxy* smproxy, vtkSMProperty* smproperty, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
  , SelectionProperty(smproperty)
{
  this->setShowLabel(false);
  this->setChangeAvailableAsChangeFinished(true);

  auto* layout = new QVBoxLayout(this);
  const int margin = pqPropertiesPanel::suggestedMargin();
  layout->setContentsMargins(margin, margin, margin, margin);
  layout->setSpacing(pqPropertiesPanel::suggestedVerticalSpacing());

  auto* copyBtn = new QPushButton(tr("Copy Input Selections"), this);
  copyBtn->setObjectName("CopyInputSelections");
  copyBtn->setToolTip(tr(
    "Clone each Input connection's current view selection, in Input order. "
    "Also fills selection-bounded lengths (Alpha / Offset) from the new selection AABB. "
    "After opening a .pvsm the saved Selection on this filter is kept; click this "
    "only if you want to replace it from the current view selections."));
  layout->addWidget(copyBtn);

  this->StatusLabel = new QLabel(this);
  this->StatusLabel->setObjectName("CopyStatus");
  this->StatusLabel->setWordWrap(true);
  layout->addWidget(this->StatusLabel);

  QObject::connect(
    copyBtn, &QPushButton::clicked, this, &pqSHYXMultiSelectionInputWidget::copyFromInputs);

  if (auto* inputProp = vtkSMInputProperty::SafeDownCast(smproxy->GetProperty("Input")))
  {
    pqCoreUtilities::connect(
      inputProp, vtkCommand::ModifiedEvent, this, SLOT(onInputsChanged()));
  }
  if (smproperty)
  {
    pqCoreUtilities::connect(
      smproperty, vtkCommand::ModifiedEvent, this, SLOT(updateStatusFromProperty()));
  }

  this->onInputsChanged();
}

pqSHYXMultiSelectionInputWidget::~pqSHYXMultiSelectionInputWidget() = default;

vtkSMProperty* pqSHYXMultiSelectionInputWidget::selectionProperty() const
{
  return this->SelectionProperty ? this->SelectionProperty : this->property();
}

void pqSHYXMultiSelectionInputWidget::apply()
{
  this->registerCopiedProxies();
  this->Superclass::apply();
  this->unregisterOrphanSelectionSources();
}

void pqSHYXMultiSelectionInputWidget::reset()
{
  this->Superclass::reset();
  this->updateStatusFromProperty();
}

void pqSHYXMultiSelectionInputWidget::onInputsChanged()
{
  if (this->Copying)
  {
    return;
  }

  auto* core = pqApplicationCore::instance();
  if (core && core->isLoadingState())
  {
    this->updateStatusFromProperty();
    return;
  }

  vtkSMProperty* selProp = this->selectionProperty();
  if (!selProp)
  {
    return;
  }

  // New filter: nothing stored yet, copy live view selections.
  // Loaded .pvsm: Selection already has the saved clones; Inputs no longer hold
  // a view selection, so recopying would show "0 of N" and wipe the restore.
  if (vtkSMPropertyHelper(selProp).GetNumberOfElements() == 0)
  {
    this->copyFromInputs();
    return;
  }
  this->updateStatusFromProperty();
}

void pqSHYXMultiSelectionInputWidget::copyFromInputs()
{
  if (this->Copying)
  {
    return;
  }
  vtkSMProxy* filter = this->proxy();
  vtkSMProperty* selProp = this->selectionProperty();
  if (!filter || !selProp)
  {
    return;
  }

  auto* inputProp = vtkSMInputProperty::SafeDownCast(filter->GetProperty("Input"));
  vtkSMSessionProxyManager* pxm = filter->GetSessionProxyManager();
  if (!inputProp || !pxm)
  {
    return;
  }

  this->Copying = true;

  vtkSMPropertyHelper helper(selProp);
  helper.RemoveAllValues();

  const unsigned int nInputs = inputProp->GetNumberOfProxies();
  for (unsigned int i = 0; i < nInputs; ++i)
  {
    auto* src = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(i));
    const unsigned int port = inputProp->GetOutputPortForConnection(i);
    vtkSMSourceProxy* liveSel = src ? src->GetSelectionInput(port) : nullptr;
    vtkSmartPointer<vtkSMSourceProxy> clone = CloneOrEmptySelection(liveSel, pxm);
    if (clone)
    {
      RegisterSelectionTree(clone);
      helper.Add(clone);
    }
  }

  this->Copying = false;
  // Push Selection first so SHYXSelectionBoundsDomain can extract the new AABB.
  const bool resetBounds = this->resetSelectionBoundsProperties();
  this->updateStatusFromProperty();
  Q_EMIT this->changeAvailable();
  if (!resetBounds && CountPopulatedSelections(selProp) > 0)
  {
    // New-filter construction: GatherInformation may not be ready until the
    // rest of the panel exists. Try once more on the next event-loop turn.
    QTimer::singleShot(0, this, SLOT(resetSelectionBoundsPropertiesLater()));
  }
}

void pqSHYXMultiSelectionInputWidget::resetSelectionBoundsPropertiesLater()
{
  if (this->resetSelectionBoundsProperties())
  {
    Q_EMIT this->changeAvailable();
  }
}

bool pqSHYXMultiSelectionInputWidget::resetSelectionBoundsProperties()
{
  vtkSMProxy* filter = this->proxy();
  vtkSMProperty* selProp = this->selectionProperty();
  if (!filter || !selProp)
  {
    return false;
  }
  if (CountPopulatedSelections(selProp) == 0)
  {
    return false;
  }

  filter->UpdateVTKObjects();

  bool any = false;
  vtkSmartPointer<vtkSMPropertyIterator> iter;
  iter.TakeReference(filter->NewPropertyIterator());
  for (iter->Begin(); !iter->IsAtEnd(); iter->Next())
  {
    vtkSMProperty* prop = iter->GetProperty();
    if (!prop || prop->GetInformationOnly() || prop->GetIsInternal())
    {
      continue;
    }
    vtkSMBoundsDomain* bounds = prop->FindDomain<vtkSMBoundsDomain>();
    if (!bounds || !bounds->GetRequiredProperty("Selection"))
    {
      continue;
    }
    bounds->Update(prop);
    if (prop->ResetToDomainDefaults(/*use_unchecked_values*/ true))
    {
      any = true;
    }
  }
  return any;
}

void pqSHYXMultiSelectionInputWidget::registerCopiedProxies()
{
  vtkSMProperty* selProp = this->selectionProperty();
  if (!selProp)
  {
    return;
  }
  vtkSMPropertyHelper helper(selProp);
  for (unsigned int i = 0; i < helper.GetNumberOfElements(); ++i)
  {
    RegisterSelectionTree(helper.GetAsProxy(i));
  }
}

void pqSHYXMultiSelectionInputWidget::unregisterOrphanSelectionSources()
{
  vtkSMProxy* filter = this->proxy();
  vtkSMSessionProxyManager* pxm = filter ? filter->GetSessionProxyManager() : nullptr;
  if (!pxm)
  {
    return;
  }

  vtkNew<vtkSMProxyIterator> iter;
  iter->SetSession(filter->GetSession());
  for (iter->Begin("selection_sources"); !iter->IsAtEnd();)
  {
    vtkSMProxy* p = iter->GetProxy();
    if (p && p->GetNumberOfConsumers() == 0)
    {
      const std::string key = iter->GetKey();
      iter->Next();
      pxm->UnRegisterProxy("selection_sources", key.c_str(), p);
    }
    else
    {
      iter->Next();
    }
  }
}

void pqSHYXMultiSelectionInputWidget::updateStatusFromProperty()
{
  if (this->Copying || !this->StatusLabel)
  {
    return;
  }

  vtkSMProxy* filter = this->proxy();
  vtkSMProperty* selProp = this->selectionProperty();
  auto* inputProp =
    filter ? vtkSMInputProperty::SafeDownCast(filter->GetProperty("Input")) : nullptr;
  const unsigned int nInputs = inputProp ? inputProp->GetNumberOfProxies() : 0;
  if (nInputs == 0)
  {
    this->StatusLabel->setText(tr("No Input connections."));
    return;
  }

  const unsigned int nPopulated = CountPopulatedSelections(selProp);
  this->StatusLabel->setText(
    tr("%1 of %2 Input selection(s) stored on this filter.")
      .arg(static_cast<int>(nPopulated))
      .arg(static_cast<int>(nInputs)));
}
