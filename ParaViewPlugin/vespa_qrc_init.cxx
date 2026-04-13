#include "vespa_qrc_init.h"

#include <QtCore/QFile>
#include <QtCore/QtGlobal>

void vespa_qrc_init()
{
  // RCC may already register at DLL load; only init if resources are missing.
  if (QFile::exists(QStringLiteral(":/VESPA/VESPA_Delaunay_2D.png")))
  {
    return;
  }
  Q_INIT_RESOURCE(VESPAIcons);
}
