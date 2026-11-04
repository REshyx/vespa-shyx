#include "vtkSHYXRemeshProgressState.h"

#include "vtkObject.h"

#ifdef VESPA_HAVE_PARAVIEW_SM
#include "vtkIndent.h"
#include "vtkPVXMLElement.h"
#include "vtkSMProxy.h"
#include "vtkSMProxyIterator.h"
#include "vtkSMProxyManager.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSmartPointer.h"

#include <vtksys/SystemTools.hxx>

#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
/** The proxy whose client-side VTK object is @a object (nullptr when unknown). */
vtkSMProxy* findProxyForClientObject(vtkSMSessionProxyManager* pxm, vtkObject* object)
{
  if (!pxm || !object)
  {
    return nullptr;
  }

  // SHYXAdaptiveIsotropicRemesher -> vtkSHYXAdaptiveIsotropicRemesher
  std::string xmlName = object->GetClassName();
  if (xmlName.compare(0, 3, "vtk") == 0)
  {
    xmlName.erase(0, 3);
  }

  vtkSMProxyIterator* iter = vtkSMProxyIterator::New();
  iter->SetSessionProxyManager(pxm);
  iter->SetModeToAll();
  vtkSMProxy* candidate = nullptr;
  int candidateCount = 0;
  for (iter->Begin(); !iter->IsAtEnd(); iter->Next())
  {
    vtkSMProxy* proxy = iter->GetProxy();
    const char* proxyXmlName = proxy ? proxy->GetXMLName() : nullptr;
    if (!proxyXmlName || xmlName != proxyXmlName)
    {
      continue;
    }
    ++candidateCount;
    candidate = proxy;
    // GetClientSideObject() can instantiate the proxy's VTK object, so only ask
    // proxies of the same type as the filter currently being applied.
    if (proxy->GetClientSideObject() == object)
    {
      iter->Delete();
      return proxy;
    }
  }
  iter->Delete();
  // Server-side sessions have no client-side VTK object; with a single filter of
  // this type there is no ambiguity.
  return candidateCount == 1 ? candidate : nullptr;
}

/** True when an "Input" property of @a proxyElement points at a removed proxy. */
bool referencesRemovedInput(vtkPVXMLElement* proxyElement, const std::set<std::string>& removedIds)
{
  for (unsigned int i = 0; i < proxyElement->GetNumberOfNestedElements(); ++i)
  {
    vtkPVXMLElement* property = proxyElement->GetNestedElement(i);
    const char* elementName = property->GetName();
    const char* propertyName = property->GetAttribute("name");
    if (!elementName || strcmp(elementName, "Property") != 0 || !propertyName ||
      strncmp(propertyName, "Input", 5) != 0)
    {
      continue;
    }
    for (unsigned int j = 0; j < property->GetNumberOfNestedElements(); ++j)
    {
      vtkPVXMLElement* reference = property->GetNestedElement(j);
      const char* referenceName = reference->GetName();
      // Proxy references inside a property are serialized as <Proxy value="gid">
      // (older states may use id).
      const char* referenceValue = reference->GetAttribute("value");
      const char* referenceId = reference->GetAttribute("id");
      if (referenceName && strcmp(referenceName, "Proxy") == 0 &&
        ((referenceValue && removedIds.find(referenceValue) != removedIds.end()) ||
          (referenceId && removedIds.find(referenceId) != removedIds.end())))
      {
        return true;
      }
    }
  }
  return false;
}

/**
 * Add the ids of every proxy downstream of the removed ones, following Input
 * connections. Representations and views that only display a removed proxy are
 * pulled in too, so the remaining state has no dangling references.
 */
void collectRemovedProxyIds(vtkPVXMLElement* serverManagerState, std::set<std::string>& removedIds)
{
  std::vector<vtkPVXMLElement*> proxies;
  for (unsigned int i = 0; i < serverManagerState->GetNumberOfNestedElements(); ++i)
  {
    vtkPVXMLElement* child = serverManagerState->GetNestedElement(i);
    const char* name = child->GetName();
    if (name && strcmp(name, "Proxy") == 0 && child->GetAttribute("id"))
    {
      proxies.push_back(child);
    }
  }

  bool changed = true;
  while (changed)
  {
    changed = false;
    for (vtkPVXMLElement* proxy : proxies)
    {
      const char* id = proxy->GetAttribute("id");
      if (!id || removedIds.find(id) != removedIds.end())
      {
        continue;
      }
      if (referencesRemovedInput(proxy, removedIds))
      {
        removedIds.insert(id);
        changed = true;
      }
    }
  }
}

/** Drop proxy definitions, proxy references, and collection items for removed ids. */
void removeProxyState(vtkPVXMLElement* element, const std::set<std::string>& removedIds)
{
  for (int i = static_cast<int>(element->GetNumberOfNestedElements()) - 1; i >= 0; --i)
  {
    vtkPVXMLElement* child = element->GetNestedElement(static_cast<unsigned int>(i));
    const char* name = child->GetName();
    const bool isProxy = name && strcmp(name, "Proxy") == 0;
    const bool isItem = name && strcmp(name, "Item") == 0;
    // Definitions and collection items carry the global id in "id"; proxy
    // references inside properties carry it in "value".
    const char* id = child->GetAttribute("id");
    const char* value = child->GetAttribute("value");
    if (((isProxy && value && removedIds.find(value) != removedIds.end()) ||
          ((isProxy || isItem) && id && removedIds.find(id) != removedIds.end())))
    {
      element->RemoveNestedElement(child);
      continue;
    }
    removeProxyState(child, removedIds);
  }
}
} // namespace
#endif

void vtkSHYXSaveRemeshProgressState(vtkObject* self)
{
#ifdef VESPA_HAVE_PARAVIEW_SM
  vtkSMProxyManager* pm = vtkSMProxyManager::GetProxyManager();
  vtkSMSessionProxyManager* pxm = pm ? pm->GetActiveSessionProxyManager() : nullptr;
  if (!pxm)
  {
    return;
  }

  std::string tempDir;
#ifdef _WIN32
  if (!vtksys::SystemTools::GetEnv("TEMP", tempDir) || tempDir.empty())
  {
    vtksys::SystemTools::GetEnv("TMP", tempDir);
  }
#else
  if (!vtksys::SystemTools::GetEnv("TMPDIR", tempDir) || tempDir.empty())
  {
    tempDir = "/tmp";
  }
#endif
  if (tempDir.empty())
  {
    vtkWarningWithObjectMacro(self, "Cannot save remesh progress state: no temp directory.");
    return;
  }

  const std::string path =
    vtksys::SystemTools::ConvertToOutputPath(tempDir + "/shyx_remesh_pre_apply.pvsm");

  // Save the pipeline as it was before this Apply: the proxy that is currently
  // running CGAL (and anything downstream of it) is left out, so loading the
  // recovery state can never start this remesh again.
  vtkSMProxy* applyingProxy = findProxyForClientObject(pxm, self);
  vtkPVXMLElement* rootElement = pxm->SaveXMLState();
  if (!rootElement)
  {
    vtkWarningWithObjectMacro(self, "Failed to save remesh progress state: " << path);
    return;
  }
  vtkSmartPointer<vtkPVXMLElement> root;
  root.TakeReference(rootElement);

  vtkPVXMLElement* serverManagerState = root->FindNestedElementByName("ServerManagerState");
  if (applyingProxy && serverManagerState)
  {
    std::set<std::string> removedIds;
    removedIds.insert(applyingProxy->GetGlobalIDAsString());
    collectRemovedProxyIds(serverManagerState, removedIds);
    removeProxyState(root, removedIds);
  }

  std::ostringstream xmlStream;
  root->PrintXML(xmlStream, vtkIndent());
  if (!pxm->SaveString(xmlStream.str().c_str(), path.c_str(), 0x10 /*vtkPVSession::CLIENT*/))
  {
    vtkWarningWithObjectMacro(self, "Failed to save remesh progress state: " << path);
  }
#else
  (void)self;
#endif
}
