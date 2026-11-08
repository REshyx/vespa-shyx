#include "pqSHYXRecoveryAutoStart.h"

#include "pqSHYXCheckpointAutoStart.h"
#include "pqSHYXStatusNotifier.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqLoadStateReaction.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr auto kRecoveryMenuObjectName = "menuSHYXRecovery";
constexpr auto kRemeshRecoveryFileName = "shyx_remesh_pre_apply.pvsm";

QMainWindow* findMainWindow()
{
  if (auto* main = qobject_cast<QMainWindow*>(pqCoreUtilities::mainWidget()))
  {
    return main;
  }
  for (QWidget* top : QApplication::topLevelWidgets())
  {
    if (auto* main = qobject_cast<QMainWindow*>(top))
    {
      return main;
    }
  }
  return nullptr;
}

QMenu* findFileMenu(QMainWindow* mainWindow)
{
  if (!mainWindow)
  {
    return nullptr;
  }
  if (QMenu* menu = mainWindow->findChild<QMenu*>(QStringLiteral("menu_File")))
  {
    return menu;
  }
  // Custom main window UIs may rename the menu; fall back to its title.
  if (QMenuBar* bar = mainWindow->menuBar())
  {
    for (QAction* action : bar->actions())
    {
      QMenu* menu = action ? action->menu() : nullptr;
      if (!menu)
      {
        continue;
      }
      QString title = menu->title();
      title.remove(QLatin1Char('&'));
      if (title.trimmed().compare(QStringLiteral("File"), Qt::CaseInsensitive) == 0)
      {
        return menu;
      }
    }
  }
  return nullptr;
}

QString tempFolderDisplay()
{
  return QDir::toNativeSeparators(QDir::tempPath());
}
} // namespace

//-----------------------------------------------------------------------------
pqSHYXRecoveryAutoStart::pqSHYXRecoveryAutoStart(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXRecoveryAutoStart::~pqSHYXRecoveryAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXRecoveryAutoStart::onStartup()
{
  if (pqApplicationCore* core = pqApplicationCore::instance())
  {
    // After the main window and its File menu have been built.
    QObject::connect(core, &pqApplicationCore::clientEnvironmentDone, this,
      &pqSHYXRecoveryAutoStart::installMenu, Qt::UniqueConnection);
  }
  this->installMenu();
  // The plugin can also be loaded from Tools -> Manage Plugins while the UI is
  // already up; a queued pass covers a File menu that is populated in this tick.
  QTimer::singleShot(0, this, &pqSHYXRecoveryAutoStart::installMenu);
}

//-----------------------------------------------------------------------------
void pqSHYXRecoveryAutoStart::onShutdown()
{
  if (this->RecoveryMenu)
  {
    delete this->RecoveryMenu.data();
  }
  this->RecoveryMenu = nullptr;
}

//-----------------------------------------------------------------------------
void pqSHYXRecoveryAutoStart::installMenu()
{
  if (this->RecoveryMenu)
  {
    return;
  }

  QMenu* fileMenu = findFileMenu(findMainWindow());
  if (!fileMenu)
  {
    return;
  }

  auto* recovery = new QMenu(tr("&Recovery"), fileMenu);
  recovery->setObjectName(QString::fromLatin1(kRecoveryMenuObjectName));
  recovery->setToolTip(
    tr("Reload a .pvsm saved in the temp folder, e.g. the automatic checkpoint or the "
       "state written before a remesh."));

  // Keep it next to Load/Save State: after Save State, before whatever follows.
  QAction* anchor = fileMenu->findChild<QAction*>(QStringLiteral("actionFileSaveServerState"));
  if (!anchor && fileMenu->actions().size() <= 1)
  {
    // The File menu is still the bare UI stub (<Exit> only); wait for
    // pqParaViewMenuBuilders::buildFileMenu / clientEnvironmentDone.
    delete recovery;
    return;
  }
  QAction* next = nullptr;
  if (anchor)
  {
    const QList<QAction*> actions = fileMenu->actions();
    const int index = actions.indexOf(anchor);
    if (index >= 0 && index + 1 < actions.size())
    {
      next = actions.at(index + 1);
    }
  }
  if (next)
  {
    fileMenu->insertMenu(next, recovery);
  }
  else
  {
    fileMenu->addMenu(recovery);
  }

  this->RecoveryMenu = recovery;
  QObject::connect(recovery, &QMenu::aboutToShow, this, &pqSHYXRecoveryAutoStart::refreshMenu,
    Qt::UniqueConnection);
  this->refreshMenu();
}

//-----------------------------------------------------------------------------
void pqSHYXRecoveryAutoStart::refreshMenu()
{
  QMenu* menu = this->RecoveryMenu;
  if (!menu)
  {
    return;
  }
  menu->clear();

  QAction* autoSave = menu->addAction(tr("Auto-save checkpoint"));
  autoSave->setCheckable(true);
  autoSave->setChecked(pqSHYXCheckpointAutoStart::autoSaveEnabled());
  autoSave->setToolTip(tr("Keep %1 up to date after pipeline changes and state loads.")
                         .arg(pqSHYXCheckpointAutoStart::checkpointFileName()));
  QObject::connect(
    autoSave, &QAction::toggled, this, [](bool enabled) {
      pqSHYXCheckpointAutoStart::setAutoSaveEnabled(enabled);
    });
  menu->addSeparator();

  const QDir tempDir(QDir::tempPath());
  QFileInfoList files = tempDir.entryInfoList(QStringList() << QStringLiteral("*.pvsm"),
    QDir::Files | QDir::Readable, QDir::NoSort);
  std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
    if (a.lastModified() != b.lastModified())
    {
      return a.lastModified() > b.lastModified();
    }
    return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
  });

  if (files.isEmpty())
  {
    QAction* empty = menu->addAction(tr("No .pvsm files in the temp folder"));
    empty->setEnabled(false);
    empty->setToolTip(tempFolderDisplay());
  }
  else
  {
    const QString timeFormat = QStringLiteral("yyyy-MM-dd HH:mm:ss");
    for (const QFileInfo& info : files)
    {
      QString label = info.fileName();
      if (info.fileName() == pqSHYXCheckpointAutoStart::checkpointFileName())
      {
        label = tr("Latest checkpoint - %1").arg(info.fileName());
      }
      else if (info.fileName() == QLatin1String(kRemeshRecoveryFileName))
      {
        label = tr("Before remesh - %1").arg(info.fileName());
      }
      label += QStringLiteral("  (%1)").arg(info.lastModified().toString(timeFormat));

      QAction* action = menu->addAction(label);
      action->setToolTip(tr("%1\n%2 KB\nClick to load this state.")
                           .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
                           .arg(info.size() / 1024.0, 0, 'f', 1));

      const QString path = info.absoluteFilePath();
      QObject::connect(action, &QAction::triggered, this,
        [this, path]() { this->loadStateFile(path); });
    }
  }

  menu->addSeparator();
  QAction* openFolder = menu->addAction(tr("Open Temp Folder..."));
  openFolder->setToolTip(tr("Open %1 in the file manager.").arg(tempFolderDisplay()));
  QObject::connect(openFolder, &QAction::triggered, this, []() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::tempPath()));
  });

  QAction* refresh = menu->addAction(tr("Refresh"));
  refresh->setToolTip(tr("Rescan the temp folder for .pvsm files."));
  // Queued: refreshMenu() clears this action, and deleting the sender inside its
  // own triggered slot is unsafe.
  QObject::connect(refresh, &QAction::triggered, this,
    [this]() { QTimer::singleShot(0, this, &pqSHYXRecoveryAutoStart::refreshMenu); });
}

//-----------------------------------------------------------------------------
void pqSHYXRecoveryAutoStart::loadStateFile(const QString& path)
{
  const QFileInfo info(path);
  if (!info.isFile())
  {
    pqSHYXStatusNotifier::warning(
      tr("SHYX Recovery: %1 no longer exists.").arg(QDir::toNativeSeparators(path)));
    return;
  }

  pqSHYXStatusNotifier::info(tr("SHYX Recovery: loading %1 ...").arg(info.fileName()));

  // dialogBlocked=true: recover in one click. If data paths moved, the user can
  // still use File -> Load State and resolve them in the Load State Options dialog.
  pqLoadStateReaction::loadState(path, true);
}
