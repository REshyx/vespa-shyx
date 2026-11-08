#include "pqSHYXCheckpointAutoStart.h"

#include "pqSHYXStatusNotifier.h"

#include "pqApplicationCore.h"
#include "pqObjectBuilder.h"
#include "pqPipelineSource.h"
#include "pqProxy.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

namespace
{
constexpr auto kSettingsGroup = "SHYX/Checkpoint";
constexpr auto kEnabledKey = "enabled";
constexpr auto kCheckpointFileName = "shyx_checkpoint.pvsm";
// Let the load reaction finish rebuilding views before snapshotting. The
// previous checkpoint stays valid until then.
constexpr int kDeferredWriteDelayMs = 500;

const QString& checkpointName()
{
  static const QString name = QString::fromLatin1(kCheckpointFileName);
  return name;
}
} // namespace

//-----------------------------------------------------------------------------
pqSHYXCheckpointAutoStart::pqSHYXCheckpointAutoStart(QObject* parent)
  : Superclass(parent)
{
  this->SaveTimer = new QTimer(this);
  this->SaveTimer->setSingleShot(true);
  this->SaveTimer->setInterval(kDeferredWriteDelayMs);
  QObject::connect(
    this->SaveTimer, &QTimer::timeout, this, &pqSHYXCheckpointAutoStart::writeCheckpoint);
}

//-----------------------------------------------------------------------------
pqSHYXCheckpointAutoStart::~pqSHYXCheckpointAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
QString pqSHYXCheckpointAutoStart::checkpointFileName()
{
  return checkpointName();
}

//-----------------------------------------------------------------------------
bool pqSHYXCheckpointAutoStart::autoSaveEnabled()
{
  QSettings settings;
  settings.beginGroup(QString::fromLatin1(kSettingsGroup));
  return settings.value(QString::fromLatin1(kEnabledKey), true).toBool();
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::setAutoSaveEnabled(bool enabled)
{
  QSettings settings;
  settings.beginGroup(QString::fromLatin1(kSettingsGroup));
  settings.setValue(QString::fromLatin1(kEnabledKey), enabled);
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::onStartup()
{
  if (pqApplicationCore* core = pqApplicationCore::instance())
  {
    // After the main window and the Server Manager model exist.
    QObject::connect(core, &pqApplicationCore::clientEnvironmentDone, this,
      &pqSHYXCheckpointAutoStart::install, Qt::UniqueConnection);
    QObject::connect(core, &pqApplicationCore::stateLoaded, this,
      &pqSHYXCheckpointAutoStart::onStateLoaded, Qt::UniqueConnection);
  }
  this->install();
  // The plugin can also be loaded from Tools -> Manage Plugins while the UI is
  // already up.
  QTimer::singleShot(0, this, &pqSHYXCheckpointAutoStart::install);
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::onShutdown()
{
  if (this->SaveTimer)
  {
    this->SaveTimer->stop();
  }
  this->Writing = false;
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::install()
{
  pqApplicationCore* core = pqApplicationCore::instance();
  if (!core)
  {
    return;
  }

  if (pqObjectBuilder* builder = core->getObjectBuilder())
  {
    QObject::connect(builder, &pqObjectBuilder::sourceCreated, this,
      &pqSHYXCheckpointAutoStart::onCreated, Qt::UniqueConnection);
    QObject::connect(builder, &pqObjectBuilder::filterCreated, this,
      &pqSHYXCheckpointAutoStart::onCreated, Qt::UniqueConnection);
  }

  if (pqServerManagerModel* model = core->getServerManagerModel())
  {
    QObject::connect(model, &pqServerManagerModel::modifiedStateChanged, this,
      &pqSHYXCheckpointAutoStart::onModifiedStateChanged, Qt::UniqueConnection);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::onModifiedStateChanged(pqServerManagerModelItem* item)
{
  const auto* source = qobject_cast<pqPipelineSource*>(item);
  if (!source)
  {
    return;
  }

  if (source->modifiedState() == pqProxy::UNMODIFIED)
  {
    // Apply finished (pqApplyBehavior sets UNMODIFIED), including the first
    // apply of a newly created filter. Deferred so the new representations
    // created by the apply behavior are part of the snapshot.
    this->scheduleCheckpoint();
    return;
  }

  if (source->modifiedState() != pqProxy::MODIFIED)
  {
    return;
  }

  // Synchronous: this runs from pqPropertiesPanel::sourcePropertyChanged()
  // before changeFinished() starts the Apply (manual or auto). The edited
  // values still live in the unchecked SM properties here.
  this->writeCheckpoint();
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::onCreated()
{
  // Before the auto-apply timer (or the user's Apply) gets a chance to run.
  this->writeCheckpoint();
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::onStateLoaded()
{
  this->scheduleCheckpoint();
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::scheduleCheckpoint()
{
  if (!autoSaveEnabled())
  {
    return;
  }
  this->SaveTimer->start();
}

//-----------------------------------------------------------------------------
void pqSHYXCheckpointAutoStart::writeCheckpoint()
{
  if (this->Writing || !autoSaveEnabled())
  {
    return;
  }

  pqApplicationCore* core = pqApplicationCore::instance();
  if (!core || core->isLoadingState() || !core->getActiveServer())
  {
    return;
  }

  const QString path = QDir::tempPath() + QLatin1Char('/') + checkpointName();
  const QString tmpPath = path + QStringLiteral(".tmp");
  QFile::remove(tmpPath);

  this->Writing = true;
  const bool saved = core->saveState(tmpPath);
  this->Writing = false;

  if (!saved || !QFileInfo(tmpPath).isFile() || QFileInfo(tmpPath).size() <= 0)
  {
    QFile::remove(tmpPath);
    pqSHYXStatusNotifier::warning(
      tr("SHYX checkpoint: could not write %1.").arg(QDir::toNativeSeparators(path)));
    return;
  }

  // Replace, not merge: one checkpoint file only. The temporary file keeps the
  // previous checkpoint intact if ParaView dies while saving.
  QFile::remove(path);
  if (!QFile::rename(tmpPath, path))
  {
    QFile::remove(tmpPath);
    pqSHYXStatusNotifier::warning(
      tr("SHYX checkpoint: could not replace %1.").arg(QDir::toNativeSeparators(path)));
    return;
  }

  pqSHYXStatusNotifier::info(tr("checkpoint saved"));
}
