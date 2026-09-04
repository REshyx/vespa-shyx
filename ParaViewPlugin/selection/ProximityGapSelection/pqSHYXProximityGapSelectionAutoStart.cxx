#include "pqSHYXProximityGapSelectionAutoStart.h"

#include "pqSHYXProximityGapSelectionViewFrameActions.h"

#include "pqApplicationCore.h"
#include "pqInterfaceTracker.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include <QTimer>

//-----------------------------------------------------------------------------
pqSHYXProximityGapSelectionAutoStart::pqSHYXProximityGapSelectionAutoStart(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXProximityGapSelectionAutoStart::~pqSHYXProximityGapSelectionAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionAutoStart::onStartup()
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

  this->Interface = new pqSHYXProximityGapSelectionViewFrameActions(core->interfaceTracker());
  core->interfaceTracker()->addInterface(this->Interface);

  this->Interface->installOnExistingViews();

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
void pqSHYXProximityGapSelectionAutoStart::onShutdown()
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
