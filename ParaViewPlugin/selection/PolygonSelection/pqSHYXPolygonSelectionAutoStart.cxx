#include "pqSHYXPolygonSelectionAutoStart.h"

#include "pqSHYXPolygonSelectionViewFrameActions.h"

#include "pqApplicationCore.h"
#include "pqInterfaceTracker.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include <QTimer>

//-----------------------------------------------------------------------------
pqSHYXPolygonSelectionAutoStart::pqSHYXPolygonSelectionAutoStart(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXPolygonSelectionAutoStart::~pqSHYXPolygonSelectionAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionAutoStart::onStartup()
{
  if (this->Interface)
  {
    return;
  }

  pqApplicationCore* core = pqApplicationCore::instance();
  if (!core || !core->interfaceTracker())
  {
    return;
  }

  this->Interface = new pqSHYXPolygonSelectionViewFrameActions(core->interfaceTracker());
  core->interfaceTracker()->addInterface(this->Interface);

  // Default RenderView frames are often created before plugins finish loading.
  // frameConnected only runs for *new* frames, so patch any that already exist
  // (otherwise the button only appears after loading a .pvsm that rebuilds views).
  this->Interface->installOnExistingViews();

  // Layout parenting may not be finished in the same tick as plugin startup.
  QTimer::singleShot(0, this, [this]() {
    if (this->Interface)
    {
      this->Interface->installOnExistingViews();
    }
  });
  QTimer::singleShot(100, this, [this]() {
    if (this->Interface)
    {
      this->Interface->installOnExistingViews();
    }
  });

  // Views assigned to pre-created empty frames do not re-invoke frameConnected.
  if (pqServerManagerModel* sm = core->getServerManagerModel())
  {
    QObject::connect(sm, &pqServerManagerModel::viewAdded, this, [this](pqView*) {
      QTimer::singleShot(0, this, [this]() {
        if (this->Interface)
        {
          this->Interface->installOnExistingViews();
        }
      });
    });
  }
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionAutoStart::onShutdown()
{
  if (!this->Interface)
  {
    return;
  }

  pqApplicationCore* core = pqApplicationCore::instance();
  if (core && core->interfaceTracker())
  {
    core->interfaceTracker()->removeInterface(this->Interface);
  }
  delete this->Interface;
  this->Interface = nullptr;
}
