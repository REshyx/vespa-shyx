#include "pqSHYXRecoveryAutoStart.h"

#include "pqSHYXStatusNotifier.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqLoadStateReaction.h"
#include "pqRepresentation.h"
#include "pqServerManagerModel.h"

#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace
{
constexpr auto kRecoveryMenuObjectName = "menuSHYXRecovery";
constexpr auto kRemeshRecoveryFileName = "shyx_remesh_pre_apply.pvsm";

bool isRepresentationGroup(const QString& group)
{
  // "representations", plus sub-groups such as "representations/annotation".
  return group.contains(QLatin1String("representation"), Qt::CaseInsensitive);
}

/** Filters whose RequestData runs CGAL remeshing (and rewrites the recovery state). */
bool isRemeshFilterType(const QString& type)
{
  return type == QLatin1String("SHYXAdaptiveIsotropicRemesher") ||
    type == QLatin1String("SHYXRemeshWithEndpoint");
}

/** Properties that make the CGAL remesh run; forced to 0 by Safely Open. */
bool isRemeshEnableProperty(const QString& name)
{
  return name == QLatin1String("EnableRemesh") || name == QLatin1String("EnableWallRemesh") ||
    name == QLatin1String("EnableCapRemesh");
}

/** Every remesh-enable property of a given SHYX filter type (empty for others). */
QStringList remeshEnablePropertyNames(const QString& type)
{
  if (type == QLatin1String("SHYXAdaptiveIsotropicRemesher"))
  {
    return { QStringLiteral("EnableRemesh") };
  }
  if (type == QLatin1String("SHYXRemeshWithEndpoint"))
  {
    return { QStringLiteral("EnableWallRemesh"), QStringLiteral("EnableCapRemesh") };
  }
  return {};
}

void writeZeroedAttributes(QXmlStreamWriter& writer, const QXmlStreamAttributes& attributes)
{
  for (const QXmlStreamAttribute& attribute : attributes)
  {
    const QString name = attribute.qualifiedName().toString();
    // A couple of state formats fold the value into the Property tag; force it
    // to 0 as well so the Element we add below cannot be overridden.
    const QString value = name.compare(QLatin1String("value"), Qt::CaseInsensitive) == 0
      ? QStringLiteral("0")
      : attribute.value().toString();
    writer.writeAttribute(name, value);
  }
}

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
    tr("Reload a .pvsm saved in the temp folder, e.g. the state written before a remesh."));

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

  QAction* safelyOpen = menu->addAction(tr("Safely Open (hide all representations)"));
  safelyOpen->setCheckable(true);
  safelyOpen->setChecked(this->SafelyOpen);
  safelyOpen->setToolTip(tr("Load the state with every representation's Visibility set to 0 and\n"
                            "every SHYX remesh filter switched off (Enable remesh / wall / cap\n"
                            "remesh), so nothing renders or remeshes while the state is rebuilt.\n"
                            "Turn the remesh switches back on and Apply to remesh again.\n"
                            "Turning this on also hides what is currently shown."));
  QObject::connect(safelyOpen, &QAction::toggled, this, [this](bool checked) {
    this->SafelyOpen = checked;
    if (checked)
    {
      this->hideAllRepresentations();
    }
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
      if (info.fileName() == QLatin1String(kRemeshRecoveryFileName))
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
void pqSHYXRecoveryAutoStart::hideAllRepresentations()
{
  pqApplicationCore* core = pqApplicationCore::instance();
  pqServerManagerModel* sm = core ? core->getServerManagerModel() : nullptr;
  if (!sm)
  {
    return;
  }

  int hiddenCount = 0;
  Q_FOREACH (pqRepresentation* repr, sm->findItems<pqRepresentation*>())
  {
    vtkSMProxy* proxy = repr ? repr->getProxy() : nullptr;
    if (!proxy || !proxy->GetProperty("Visibility"))
    {
      continue;
    }
    vtkSMPropertyHelper visibility(proxy, "Visibility");
    if (visibility.GetAsInt() == 0)
    {
      continue;
    }
    visibility.Set(0);
    proxy->UpdateVTKObjects();
    ++hiddenCount;
  }

  if (hiddenCount > 0)
  {
    pqSHYXStatusNotifier::info(
      tr("SHYX Recovery: hid %1 visible representation(s).").arg(hiddenCount));
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXRecoveryAutoStart::patchStateForSafeOpen(const QByteArray& stateXml,
  QByteArray* patchedXml, int* hiddenRepresentations, int* disabledRemeshFilters)
{
  if (!patchedXml)
  {
    return false;
  }

  QByteArray buffer;
  QXmlStreamReader reader(stateXml);
  QXmlStreamWriter writer(&buffer);
  writer.setAutoFormatting(false);

  struct ProxyContext
  {
    bool IsRepresentation = false;
    QString RemeshType;
    QString ProxyId;
    int Depth = -1;
    bool Neutralized = false;
    QStringList SeenEnableProperties;
  };
  QVector<ProxyContext> proxies;

  int depth = 0;
  int skippedPropertyDepth = -1;
  int hidden = 0;
  int disabled = 0;

  auto writeZeroProperty = [](QXmlStreamWriter& xml, const QString& name, const QString& id) {
    xml.writeStartElement(QStringLiteral("Property"));
    xml.writeAttribute(QStringLiteral("name"), name);
    if (!id.isEmpty())
    {
      xml.writeAttribute(QStringLiteral("id"), id + QLatin1Char('.') + name);
    }
    xml.writeAttribute(QStringLiteral("number_of_elements"), QStringLiteral("1"));
    xml.writeStartElement(QStringLiteral("Element"));
    xml.writeAttribute(QStringLiteral("index"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("value"), QStringLiteral("0"));
    xml.writeEndElement();
    xml.writeEndElement();
  };

  while (!reader.atEnd())
  {
    const QXmlStreamReader::TokenType token = reader.readNext();

    if (token == QXmlStreamReader::StartElement)
    {
      const QString element = reader.name().toString();
      if (skippedPropertyDepth >= 0)
      {
        // Belongs to a Property that was already rewritten to a single 0 Element.
      }
      else if (element == QLatin1String("Proxy"))
      {
        ProxyContext context;
        context.IsRepresentation = isRepresentationGroup(
          reader.attributes().value(QLatin1String("group")).toString());
        const QString type = reader.attributes().value(QLatin1String("type")).toString();
        if (isRemeshFilterType(type))
        {
          context.RemeshType = type;
        }
        context.ProxyId = reader.attributes().value(QLatin1String("id")).toString();
        context.Depth = depth;
        proxies.push_back(context);
        writer.writeCurrentToken(reader);
      }
      else if (element == QLatin1String("Property") && !proxies.isEmpty())
      {
        const ProxyContext& context = proxies.last();
        const QString name = reader.attributes().value(QLatin1String("name")).toString();
        const bool hideRepresentation =
          context.IsRepresentation && name == QLatin1String("Visibility");
        const bool stopRemesh = !context.RemeshType.isEmpty() && isRemeshEnableProperty(name);
        if (hideRepresentation || stopRemesh)
        {
          // Replace the whole property body so every element becomes 0 while the
          // property id and any domain hints survive.
          writer.writeStartElement(element);
          writeZeroedAttributes(writer, reader.attributes());
          writer.writeStartElement(QStringLiteral("Element"));
          writer.writeAttribute(QStringLiteral("index"), QStringLiteral("0"));
          writer.writeAttribute(QStringLiteral("value"), QStringLiteral("0"));
          writer.writeEndElement();
          writer.writeEndElement();
          skippedPropertyDepth = depth;
          if (hideRepresentation)
          {
            ++hidden;
          }
          else
          {
            proxies.last().Neutralized = true;
            proxies.last().SeenEnableProperties.append(name);
          }
        }
        else
        {
          writer.writeCurrentToken(reader);
        }
      }
      else
      {
        writer.writeCurrentToken(reader);
      }
      ++depth;
    }
    else if (token == QXmlStreamReader::EndElement)
    {
      --depth;
      if (skippedPropertyDepth >= 0)
      {
        if (depth == skippedPropertyDepth)
        {
          skippedPropertyDepth = -1;
        }
      }
      else
      {
        if (!proxies.isEmpty() && depth == proxies.last().Depth)
        {
          // States saved before an enable property existed have no element for it,
          // so the filter would fall back to its default (remesh ON). Inject the
          // missing ones before closing the proxy.
          const ProxyContext context = proxies.takeLast();
          bool injected = false;
          for (const QString& name : remeshEnablePropertyNames(context.RemeshType))
          {
            if (!context.SeenEnableProperties.contains(name))
            {
              writeZeroProperty(writer, name, context.ProxyId);
              injected = true;
            }
          }
          if (context.Neutralized || injected)
          {
            ++disabled;
          }
        }
        writer.writeCurrentToken(reader);
      }
    }
    else if (token == QXmlStreamReader::EndDocument)
    {
      writer.writeEndDocument();
      break;
    }
    else if (skippedPropertyDepth < 0)
    {
      writer.writeCurrentToken(reader);
    }
  }

  if (reader.hasError())
  {
    return false;
  }
  *patchedXml = buffer;
  if (hiddenRepresentations)
  {
    *hiddenRepresentations = hidden;
  }
  if (disabledRemeshFilters)
  {
    *disabledRemeshFilters = disabled;
  }
  return true;
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

  if (!this->SafelyOpen)
  {
    // dialogBlocked=true: recover in one click. If data paths moved, the user can
    // still use File -> Load State and resolve them in the Load State Options dialog.
    pqLoadStateReaction::loadState(path, true);
    return;
  }

  // Stop whatever is currently on screen from rendering while the state rebuilds.
  this->hideAllRepresentations();

  // Visibility is stored in the .pvsm, so the state has to be patched before it
  // is loaded; hiding representations afterwards would render once and can hang.
  QFile stateFile(path);
  if (!stateFile.open(QIODevice::ReadOnly))
  {
    pqLoadStateReaction::loadState(path, true);
    return;
  }
  const QByteArray originalXml = stateFile.readAll();
  stateFile.close();

  QByteArray patchedXml;
  int hiddenRepresentations = 0;
  int disabledRemeshFilters = 0;
  if (!pqSHYXRecoveryAutoStart::patchStateForSafeOpen(
        originalXml, &patchedXml, &hiddenRepresentations, &disabledRemeshFilters))
  {
    pqSHYXStatusNotifier::warning(tr("SHYX Recovery: could not rewrite %1 for Safely Open; "
                                     "loading it unchanged.")
                                    .arg(info.fileName()));
    pqLoadStateReaction::loadState(path, true);
    return;
  }

  // Patch the file in place: a separate working copy would show up in Recent
  // Files (pqLoadStateReaction records whatever path it loads).
  QSaveFile patched(path);
  if (!patched.open(QIODevice::WriteOnly) || patched.write(patchedXml) != patchedXml.size() ||
    !patched.commit())
  {
    patched.cancelWriting();
    pqSHYXStatusNotifier::warning(tr("SHYX Recovery: could not rewrite %1; loading it unchanged.")
                                    .arg(info.fileName()));
    pqLoadStateReaction::loadState(path, true);
    return;
  }

  pqLoadStateReaction::loadState(path, true);

  if (disabledRemeshFilters > 0)
  {
    pqSHYXStatusNotifier::info(
      tr("SHYX Recovery: Safely Open hid %1 representation(s) and switched off %2 remesh "
         "filter(s). Turn \"Enable remesh\" / wall / cap remesh back on before Apply.")
        .arg(hiddenRepresentations)
        .arg(disabledRemeshFilters));
  }
  else if (hiddenRepresentations == 0)
  {
    // Nothing matched: the state has no representation/remesh proxies (or an
    // unexpected format). Surface it so a silent fallback is easy to spot.
    pqSHYXStatusNotifier::warning(
      tr("SHYX Recovery: %1 had no representation or SHYX remesh proxies; Safely Open did "
         "not change anything.")
        .arg(info.fileName()));
  }

  // Put the recovery state back byte for byte so it can be loaded again.
  QSaveFile restore(path);
  if (!restore.open(QIODevice::WriteOnly) || restore.write(originalXml) != originalXml.size() ||
    !restore.commit())
  {
    restore.cancelWriting();
    pqSHYXStatusNotifier::warning(
      tr("SHYX Recovery: could not restore %1; its representations are left hidden.")
        .arg(info.fileName()));
    return;
  }

  // Patching and restoring must not reorder the temp-folder listing.
  const QDateTime lastModified = info.lastModified();
  if (lastModified.isValid())
  {
    QFile restored(path);
    if (restored.open(QIODevice::ReadWrite))
    {
      restored.setFileTime(lastModified, QFileDevice::FileModificationTime);
    }
  }

  // Representations whose state omitted Visibility default back to visible;
  // force them off so nothing starts rendering right after the load.
  this->hideAllRepresentations();
}
