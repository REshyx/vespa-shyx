#ifndef pqSHYXAIAgentTools_h
#define pqSHYXAIAgentTools_h

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/**
 * OpenAI-compatible tool schemas and live ParaView lookups for SHYX AI Assistant
 * agent mode. Live state is inspect_pipeline / inspect_selection / inspect_view
 * (plus get_output_window). Read tools live here; set_code_script / run_code_script /
 * get_code_script are executed by the panel because they touch the code box and host
 * Python. capture_screenshot is handled by the panel so it can attach a JPEG.
 * applyScreenshotCamera / annotateScreenshotTargets run first: the call passes
 * camera={focal_point, position, view_angle}, targets, and label.
 * Omit capture_screenshot from schema() when the user has not enabled auto RenderView capture.
 */
namespace pqSHYXAIAgentTools
{
QJsonArray schema(bool allowCaptureScreenshot);
QString run(const QString& name, const QJsonObject& args);

/** Apply camera from a capture_screenshot call. Empty when no camera was passed.
 *  Failures start with "Error:" and leave the view unchanged. */
QString applyScreenshotCamera(const QJsonObject& args);

/** Project targets onto image and, when label is true (the default), draw their names.
 *  JPEG pixel notes use origin=top_left. Empty when targets was omitted. */
QString annotateScreenshotTargets(QImage& image, const QJsonObject& args);
}

#endif
