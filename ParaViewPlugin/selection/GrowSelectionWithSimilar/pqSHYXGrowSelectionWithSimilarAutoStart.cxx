#include "pqSHYXGrowSelectionWithSimilarAutoStart.h"

#include "pqSHYXGrowSelectionWithSimilarViewFrameActions.h"

#include "pqApplicationCore.h"
#include "pqInterfaceTracker.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include <QTimer>

//-----------------------------------------------------------------------------
pqSHYXGrowSelectionWithSimilarAutoStart::pqSHYXGrowSelectionWithSimilarAutoStart(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXGrowSelectionWithSimilarAutoStart::~pqSHYXGrowSelectionWithSimilarAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarAutoStart::onStartup()
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

  this->Interface = new pqSHYXGrowSelectionWithSimilarViewFrameActions(core->interfaceTracker());
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
void pqSHYXGrowSelectionWithSimilarAutoStart::onShutdown()
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
