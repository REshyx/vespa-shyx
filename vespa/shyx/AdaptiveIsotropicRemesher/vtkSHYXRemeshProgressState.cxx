#include "vtkSHYXRemeshProgressState.h"

#include "vtkObject.h"

#ifdef VESPA_HAVE_PARAVIEW_SM
#include "vtkSMProxyManager.h"
#include "vtkSMSessionProxyManager.h"

#include <vtksys/SystemTools.hxx>

#include <string>
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

  if (!pxm->SaveXMLState(path.c_str()))
  {
    vtkWarningWithObjectMacro(self, "Failed to save remesh progress state: " << path);
  }
#else
  (void)self;
#endif
}
