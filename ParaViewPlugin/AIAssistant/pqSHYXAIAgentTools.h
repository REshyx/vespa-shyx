#ifndef pqSHYXAIAgentTools_h
#define pqSHYXAIAgentTools_h

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/**
 * OpenAI-compatible tool schemas and live ParaView lookups for SHYX AI Assistant
 * agent mode. Live state is inspect_pipeline / inspect_selection / inspect_view
 * (plus get_output_window). Read tools live here; set_code_script / run_code_script /
 * get_code_script are executed by the panel because they touch the code box and host
 * Python. capture_screenshot is handled by the panel so it can attach a JPEG.
 * Omit it from schema() when the user has not enabled auto RenderView capture.
 */
namespace pqSHYXAIAgentTools
{
QJsonArray schema(bool allowCaptureScreenshot);
QString run(const QString& name, const QJsonObject& args);
}

#endif
