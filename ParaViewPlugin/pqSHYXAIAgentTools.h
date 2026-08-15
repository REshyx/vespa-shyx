#ifndef pqSHYXAIAgentTools_h
#define pqSHYXAIAgentTools_h

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class vtkSMProxy;

/**
 * OpenAI-compatible tool schemas and live ParaView lookups for SHYX AI Assistant
 * agent mode. Read tools live here; set_code_script / run_code_script are executed
 * by the panel because they touch the code box and host Python.
 */
namespace pqSHYXAIAgentTools
{
QJsonArray schema();
QString run(const QString& name, const QJsonObject& args, vtkSMProxy* selfProxy);
}

#endif
