#ifndef pqSHYXAIAgentTools_h
#define pqSHYXAIAgentTools_h

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/**
 * OpenAI-compatible tool schemas and live ParaView lookups for SHYX AI Assistant
 * agent mode. Read tools live here; set_code_script / run_code_script / get_code_script
 * are executed by the panel because they touch the code box and host Python.
 * capture_screenshot is also handled by the panel so it can attach a JPEG.
 */
namespace pqSHYXAIAgentTools
{
QJsonArray schema();
QString run(const QString& name, const QJsonObject& args);
}

#endif
