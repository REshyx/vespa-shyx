#include "pqSHYXAIAssistantPanel.h"

#include "pqSHYXAIAgentTools.h"
#include "pqSHYXAIChatView.h"
#include "pqSHYXAIImageAnnotator.h"
#include "pqSHYXAIOutputLog.h"
#include "pqSHYXPythonSyntaxHighlighter.h"

#include "pqActiveObjects.h"
#include "pqPopOutWidget.h"
#include "pqPVApplicationCore.h"
#include "pqView.h"

#include "vtkImageData.h"
#include "vtkSMViewProxy.h"
#include "vtkSmartPointer.h"

#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QLibraryInfo>
#include <QList>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPluginLoader>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QSlider>
#include <QSslSocket>
#include <QStringList>
#include <QStyle>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QVector>
#include <QWidget>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstring>
#include <functional>

namespace
{
constexpr int kHistoryMaxMessages = 10;
constexpr int kHistoryKeepChars = 2000;
constexpr int kJpegMaxEdge = 1280;
constexpr auto kSettingsGroup = "VESPA/SHYXAIAssistant";
constexpr auto kDefaultCode =
  "# ParaView Python. Use Run script to execute.\nfrom paraview.simple import *\n";

QString thisPluginDir()
{
#ifdef _WIN32
  HMODULE module = nullptr;
  if (GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&thisPluginDir), &module) &&
    module)
  {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(module, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
    {
      return QFileInfo(QString::fromWCharArray(buf, static_cast<int>(n))).absolutePath();
    }
  }
#endif
  return QCoreApplication::applicationDirPath();
}

void tryLoadTlsPluginFile(const QString& dllPath)
{
  if (dllPath.isEmpty() || !QFileInfo::exists(dllPath))
  {
    return;
  }
  QPluginLoader loader(dllPath);
  loader.load();
}

void ensureHttpsTls()
{
  static bool attempted = false;
  if (attempted)
  {
    return;
  }
  attempted = true;

  QStringList roots;
#ifdef SHYX_QT_PLUGINS_DIR
  roots << QString::fromUtf8(SHYX_QT_PLUGINS_DIR);
#endif
  const QByteArray envPath = qgetenv("QT_PLUGIN_PATH");
  if (!envPath.isEmpty())
  {
    roots << QString::fromLocal8Bit(envPath).split(QDir::listSeparator(), Qt::SkipEmptyParts);
  }
  roots << thisPluginDir();
  const QString appDir = QCoreApplication::applicationDirPath();
  if (!appDir.isEmpty())
  {
    roots << appDir << (appDir + QStringLiteral("/plugins"));
  }
  const QString qtPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
  if (!qtPlugins.isEmpty())
  {
    roots << qtPlugins;
  }

  roots.removeDuplicates();
  for (const QString& root : roots)
  {
    if (root.isEmpty())
    {
      continue;
    }
    QCoreApplication::addLibraryPath(root);
#ifdef Q_OS_WIN
    tryLoadTlsPluginFile(QDir(root).filePath(QStringLiteral("tls/qschannelbackend.dll")));
#else
    tryLoadTlsPluginFile(QDir(root).filePath(QStringLiteral("tls/qopensslbackend.dll")));
#endif
  }

  const QStringList backends = QSslSocket::availableBackends();
#ifdef Q_OS_WIN
  if (backends.contains(QStringLiteral("schannel")))
  {
    QSslSocket::setActiveBackend(QStringLiteral("schannel"));
  }
  else
#endif
    if (backends.contains(QStringLiteral("openssl")))
  {
    QSslSocket::setActiveBackend(QStringLiteral("openssl"));
  }
}

QString tlsDiagnostic()
{
  const QStringList backends = QSslSocket::availableBackends();
  return QStringLiteral("supportsSsl=%1; backends=[%2]; active=%3")
    .arg(QSslSocket::supportsSsl() ? QStringLiteral("true") : QStringLiteral("false"))
    .arg(backends.join(QLatin1Char(',')))
    .arg(QSslSocket::activeBackend());
}

/** Host ParaView's Python manager (nullptr if this process was built without pqPython). */
QObject* hostPythonManager()
{
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  if (!core)
  {
    return nullptr;
  }
  return reinterpret_cast<QObject*>(core->pythonManager());
}

bool executeOnHostPython(const QByteArray& code)
{
  QObject* py = hostPythonManager();
  if (!py)
  {
    return false;
  }
  QByteArray payload = code;
  QVector<QByteArray> pre;
  QVector<QByteArray> post;
  return QMetaObject::invokeMethod(py, "executeCode", Qt::DirectConnection,
    Q_ARG(QByteArray, payload), Q_ARG(QVector<QByteArray>, pre), Q_ARG(QVector<QByteArray>, post));
}

constexpr auto kSystemPrompt = R"SYS(You are an assistant inside ParaView (VESPA / SHYX plugin).
You answer questions about visualization, meshing, and this pipeline, and you write ParaView Python.

Rules:
- Default script language is ParaView Python using `from paraview.simple import *`.
- If the user is only asking a question, reply in the dialog. Do not emit a fenced code block.
- If the user asks you to write or edit a script and you do NOT have run_code_script, put the complete script in one ```python fenced block. The plugin copies that block into the code box. Keep a short explanation outside the fence.
- When a current code box is provided, treat it as the file to edit. Return the full updated script, not a partial patch, unless the user asks for a snippet.
- The Run script button (and run_code_script) execute the code box. Send to AI does not, unless you call run_code_script.
- Prefer existing pipeline objects (FindSource, GetActiveSource) over recreating readers.
- Never create a second Clip/Slice/Threshold/Calculator/etc. on the same input. On retries, FindSource the existing filter (use get_pipeline_tree for names) and set its properties (ClipType.Origin, ClipType.Normal, ...). If you must replace it, Delete(FindSource('Clip1')) first. Pass registrationName= when creating so later turns can find it.
- The assistant is a View-menu dock, not a pipeline filter. Do not create a SHYXAIAssistant source.
- Screenshots are expensive; only call capture_screenshot when the user enabled render-view screenshots and you truly need pixels.
- Output Window errors are not attached automatically. If you need them, call get_output_window.
- If tools are provided, call them when you lack live ParaView context. Do not guess FindSource names or SHYX XML/python names.
- Use get_source_data(name, port) for non-active nodes and extra output ports. get_source_properties includes nested proxies (ClipType).
- Use list_filters / describe_proxy / lookup_shyx_docs before creating SHYX or VESPA filters. lookup_shyx_docs knows the Vascular pipeline order.
- Use get_selection_ids before selection-based SHYX filters; get_blocks before ExtractBlock / PDC tools.
- Use get_color_map for LUT range/log; get_time for timesteps.
- pick_world_point: prefer a single pick. Multiple calls are allowed when you need several distinct locations (several brush marks, clip origin plus a second point) or one miss needs a single corrected click. Pass image_width/height from capture_screenshot (origin=top_left). Do not grid-sample the screenshot, and do not retry the same click with origin/normalized/pixel variants.
- When set_code_script and run_code_script are available: write the full script with set_code_script, then run_code_script. Read the Output Window and data from the tool result. Use a screenshot only if one was actually attached. If there is a traceback, ERROR, empty/wrong data, or a bad view, fix the script and run again. Repeat until it works or you are stuck, then explain in the dialog.
- Reply in the same language the user uses.)SYS";

QPlainTextEdit* makeEditor(QWidget* parent, bool mono, int minHeight)
{
  auto* edit = new QPlainTextEdit(parent);
  edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  edit->setMinimumHeight(minHeight);
  edit->setTabChangesFocus(false);
  if (mono)
  {
    QFont font = edit->font();
    font.setFamily(QStringLiteral("Consolas"));
    font.setStyleHint(QFont::Monospace);
    edit->setFont(font);
    edit->setTabStopDistance(4 * edit->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
  }
  return edit;
}

class ClickThumb : public QLabel
{
public:
  explicit ClickThumb(QWidget* parent)
    : QLabel(parent)
  {
    this->setCursor(Qt::PointingHandCursor);
    this->setToolTip(QObject::tr("Click to mark with a brush"));
  }

  std::function<void()> OnClick;

protected:
  void mouseReleaseEvent(QMouseEvent* event) override
  {
    if (event->button() == Qt::LeftButton && this->OnClick)
    {
      this->OnClick();
    }
    QLabel::mouseReleaseEvent(event);
  }
};

QByteArray jpegFromImage(const QImage& img)
{
  if (img.isNull())
  {
    return {};
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (!img.save(&buffer, "JPEG", 75))
  {
    return {};
  }
  return bytes;
}

QString truncateTail(const QString& text, int maxChars)
{
  if (maxChars <= 0 || text.size() <= maxChars)
  {
    return text;
  }
  return text.right(maxChars);
}

QImage imageFromVtk(vtkImageData* img)
{
  if (!img)
  {
    return {};
  }
  int dims[3] = { 0, 0, 0 };
  img->GetDimensions(dims);
  if (dims[0] < 1 || dims[1] < 1)
  {
    return {};
  }
  if (img->GetScalarType() != VTK_UNSIGNED_CHAR)
  {
    return {};
  }
  const int comps = img->GetNumberOfScalarComponents();
  if (comps < 3)
  {
    return {};
  }
  QImage qimg(dims[0], dims[1], QImage::Format_RGB888);
  for (int y = 0; y < dims[1]; ++y)
  {
    auto* src = static_cast<unsigned char*>(img->GetScalarPointer(0, dims[1] - 1 - y, 0));
    unsigned char* dst = qimg.scanLine(y);
    if (!src || !dst)
    {
      return {};
    }
    if (comps == 3)
    {
      std::memcpy(dst, src, static_cast<size_t>(dims[0]) * 3u);
    }
    else
    {
      for (int x = 0; x < dims[0]; ++x)
      {
        dst[x * 3 + 0] = src[x * comps + 0];
        dst[x * 3 + 1] = src[x * comps + 1];
        dst[x * 3 + 2] = src[x * comps + 2];
      }
    }
  }
  return qimg;
}

QUrl completionsUrl(QString endpoint)
{
  endpoint = endpoint.trimmed();
  while (endpoint.endsWith(QLatin1Char('/')))
  {
    endpoint.chop(1);
  }
  if (endpoint.contains(QLatin1String("/chat/completions"), Qt::CaseInsensitive))
  {
    return QUrl(endpoint);
  }
  return QUrl(endpoint + QStringLiteral("/chat/completions"));
}

QImage imageFromDataUrl(const QString& url)
{
  const QString trimmed = url.trimmed();
  if (!trimmed.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive))
  {
    return {};
  }
  const int comma = trimmed.indexOf(QLatin1Char(','));
  if (comma < 0)
  {
    return {};
  }
  QImage img;
  img.loadFromData(QByteArray::fromBase64(trimmed.mid(comma + 1).toLatin1()));
  return img;
}

void takeMarkdownImages(QString& text, QList<QImage>& images)
{
  static const QRegularExpression md(
    QStringLiteral(R"(!\[[^\]]*\]\((data:image\/[a-zA-Z0-9+.-]+;base64,[A-Za-z0-9+/=\s]+)\))"));
  QRegularExpressionMatchIterator it = md.globalMatch(text);
  QString rebuilt;
  int pos = 0;
  while (it.hasNext())
  {
    const QRegularExpressionMatch m = it.next();
    rebuilt += text.mid(pos, m.capturedStart() - pos);
    const QImage img = imageFromDataUrl(m.captured(1));
    if (!img.isNull())
    {
      images.append(img);
    }
    pos = m.capturedEnd();
  }
  rebuilt += text.mid(pos);
  text = rebuilt;
}

void parseAssistantContent(const QJsonValue& content, QString& text, QList<QImage>& images)
{
  text.clear();
  images.clear();
  if (content.isString())
  {
    text = content.toString();
  }
  else if (content.isArray())
  {
    const QJsonArray parts = content.toArray();
    for (const QJsonValue& part : parts)
    {
      if (part.isString())
      {
        text += part.toString();
        continue;
      }
      if (!part.isObject())
      {
        continue;
      }
      const QJsonObject obj = part.toObject();
      const QString type = obj.value(QLatin1String("type")).toString();
      if (type == QLatin1String("text"))
      {
        text += obj.value(QLatin1String("text")).toString();
      }
      else if (type == QLatin1String("image_url"))
      {
        QString url;
        const QJsonValue imageVal = obj.value(QLatin1String("image_url"));
        if (imageVal.isString())
        {
          url = imageVal.toString();
        }
        else if (imageVal.isObject())
        {
          url = imageVal.toObject().value(QLatin1String("url")).toString();
        }
        const QImage img = imageFromDataUrl(url);
        if (!img.isNull())
        {
          images.append(img);
        }
      }
    }
  }
  takeMarkdownImages(text, images);
}

bool extractPythonFence(const QString& content, QString& codeOut)
{
  static const QRegularExpression fence(
    QStringLiteral("```(?:python|py|paraview)\\r?\\n([\\s\\S]*?)```"),
    QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator it = fence.globalMatch(content);
  QStringList blocks;
  while (it.hasNext())
  {
    blocks << it.next().captured(1).trimmed();
  }
  if (blocks.isEmpty())
  {
    static const QRegularExpression generic(
      QStringLiteral("```\\r?\\n([\\s\\S]*?)```"));
    QRegularExpressionMatch m = generic.match(content);
    if (m.hasMatch())
    {
      const QString block = m.captured(1).trimmed();
      if (block.contains(QLatin1String("paraview.simple")) ||
        block.contains(QLatin1String("import paraview")))
      {
        codeOut = block;
        return true;
      }
    }
    return false;
  }
  codeOut = blocks.join(QLatin1String("\n\n"));
  return true;
}

QString jsonErrorMessage(const QJsonObject& obj, const QString& fallback)
{
  auto fromErrorValue = [&](const QJsonValue& ev) -> QString {
    if (ev.isObject())
    {
      const QJsonObject eo = ev.toObject();
      QString msg = eo.value(QLatin1String("message")).toString();
      const QString code = eo.value(QLatin1String("code")).toVariant().toString();
      if (msg.isEmpty())
      {
        msg = fallback;
      }
      if (!code.isEmpty() && !msg.contains(code))
      {
        msg = QStringLiteral("%1 (%2)").arg(msg, code);
      }
      return msg;
    }
    if (ev.isString())
    {
      return ev.toString();
    }
    return QString();
  };

  if (obj.contains(QLatin1String("error")))
  {
    const QString nested = fromErrorValue(obj.value(QLatin1String("error")));
    if (!nested.isEmpty())
    {
      return nested;
    }
  }
  QString msg = obj.value(QLatin1String("message")).toString();
  const QString code = obj.value(QLatin1String("code")).toVariant().toString();
  if (msg.isEmpty() && code.isEmpty())
  {
    return fallback;
  }
  if (msg.isEmpty())
  {
    msg = fallback;
  }
  if (!code.isEmpty() && !msg.contains(code))
  {
    msg = QStringLiteral("%1 (%2)").arg(msg, code);
  }
  return msg;
}

QJsonObject sanitizeAssistantToolMessage(QJsonObject msg)
{
  msg.remove(QStringLiteral("reasoning_content"));
  msg.remove(QStringLiteral("reasoning"));
  msg.insert(QStringLiteral("role"), QStringLiteral("assistant"));
  const QJsonValue content = msg.value(QLatin1String("content"));
  if (content.isNull() || content.isUndefined() ||
    (content.isString() && content.toString().isEmpty()))
  {
    msg.insert(QStringLiteral("content"), QString());
  }

  QJsonArray inCalls = msg.value(QLatin1String("tool_calls")).toArray();
  QJsonArray outCalls;
  for (int i = 0; i < inCalls.size(); ++i)
  {
    QJsonObject call = inCalls.at(i).toObject();
    QString id = call.value(QLatin1String("id")).toString();
    if (id.trimmed().isEmpty())
    {
      id = QStringLiteral("call_%1").arg(i);
    }
    QJsonObject fn = call.value(QLatin1String("function")).toObject();
    QString name = fn.value(QLatin1String("name")).toString();
    if (name.trimmed().isEmpty())
    {
      name = call.value(QLatin1String("name")).toString();
    }
    QString arguments;
    const QJsonValue argsVal = fn.contains(QLatin1String("arguments"))
      ? fn.value(QLatin1String("arguments"))
      : call.value(QLatin1String("arguments"));
    if (argsVal.isArray())
    {
      arguments = QString::fromUtf8(QJsonDocument(argsVal.toArray()).toJson(QJsonDocument::Compact));
    }
    else if (argsVal.isObject())
    {
      arguments = QString::fromUtf8(QJsonDocument(argsVal.toObject()).toJson(QJsonDocument::Compact));
    }
    else
    {
      arguments = argsVal.toString();
    }
    if (arguments.trimmed().isEmpty())
    {
      arguments = QStringLiteral("{}");
    }
    QJsonObject outFn;
    outFn.insert(QStringLiteral("name"), name);
    outFn.insert(QStringLiteral("arguments"), arguments);
    QJsonObject outCall;
    outCall.insert(QStringLiteral("id"), id);
    outCall.insert(QStringLiteral("type"), QStringLiteral("function"));
    outCall.insert(QStringLiteral("function"), outFn);
    outCalls.append(outCall);
  }
  if (!outCalls.isEmpty())
  {
    msg.insert(QStringLiteral("tool_calls"), outCalls);
  }
  return msg;
}

QString replyErrorDetail(QNetworkReply* reply, const QByteArray& body)
{
  QString err = reply ? reply->errorString() : QStringLiteral("network error");
  const int http =
    reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
  QString extra;
  if (http > 0)
  {
    extra = QStringLiteral("HTTP %1").arg(http);
  }
  QByteArray trimmed = body.trimmed();
  if (trimmed.startsWith("data:"))
  {
    QByteArray data = trimmed.mid(5);
    if (data.startsWith(' '))
    {
      data = data.mid(1);
    }
    const int nl = data.indexOf('\n');
    if (nl >= 0)
    {
      data = data.left(nl);
    }
    trimmed = data.trimmed();
  }
  const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
  if (doc.isObject())
  {
    const QString api = jsonErrorMessage(doc.object(), QString());
    if (!api.isEmpty())
    {
      extra = extra.isEmpty() ? api : extra + QStringLiteral(": ") + api;
    }
    else
    {
      const QString compact =
        QString::fromUtf8(QJsonDocument(doc.object()).toJson(QJsonDocument::Compact));
      extra = extra.isEmpty() ? compact : extra + QStringLiteral(": ") + compact;
    }
  }
  else if (!trimmed.isEmpty())
  {
    QString text = QString::fromUtf8(trimmed.left(800));
    extra = extra.isEmpty() ? text : extra + QStringLiteral(": ") + text;
  }
  if (!extra.isEmpty() && !err.contains(extra))
  {
    if (err.trimmed().endsWith(QLatin1Char(':')))
    {
      err += QLatin1Char(' ') + extra;
    }
    else
    {
      err += QStringLiteral(" — %1").arg(extra);
    }
  }
  return err;
}

void applyToolRequestFields(QJsonObject& root)
{
  root.insert(QStringLiteral("enable_thinking"), false);
}
}

pqSHYXAIAssistantPanel::pqSHYXAIAssistantPanel(const QString& title, QWidget* parent)
  : Superclass(title, parent)
{
  this->constructor();
}

pqSHYXAIAssistantPanel::pqSHYXAIAssistantPanel(QWidget* parent)
  : Superclass(parent)
{
  this->constructor();
}

void pqSHYXAIAssistantPanel::constructor()
{
  pqSHYXAIOutputLog::instance()->start();
  this->setWindowTitle(tr("SHYX AI Assistant"));
  this->setObjectName(QStringLiteral("pqSHYXAIAssistantPanel"));
  this->setAllowedAreas(Qt::AllDockWidgetAreas);
  this->setMinimumWidth(320);

  auto* root = new QWidget(this);
  this->setWidget(root);

  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(4);

  auto* hint = new QLabel(root);
  hint->setWordWrap(true);
  if (hostPythonManager())
  {
    hint->setText(tr(
      "Run script executes the code box in this ParaView's Python. Send to AI does not run it."));
  }
  else
  {
    hint->setText(tr(
      "This ParaView process has no Python interpreter. Run script will not execute the code box. "
      "Load this plugin in a ParaView build with Python (official installer is fine)."));
  }
  layout->addWidget(hint);

  layout->addWidget(new QLabel(tr("Question"), root));
  auto* questionBox = new QFrame(root);
  questionBox->setFrameShape(QFrame::StyledPanel);
  auto* questionLay = new QVBoxLayout(questionBox);
  questionLay->setContentsMargins(4, 4, 4, 4);
  questionLay->setSpacing(4);
  this->QuestionEdit = makeEditor(questionBox, false, 56);
  this->QuestionEdit->setMaximumHeight(72);
  this->QuestionEdit->setFrameStyle(QFrame::NoFrame);
  this->QuestionEdit->setPlaceholderText(
    tr("Ask a question, or tell the assistant to write/edit the script... "
       "(Enter to send, Ctrl+Enter for a new line)"));
  this->QuestionEdit->installEventFilter(this);
  questionLay->addWidget(this->QuestionEdit);

  auto* shotRow = new QHBoxLayout();
  shotRow->setContentsMargins(0, 0, 0, 0);
  shotRow->setSpacing(6);
  this->QuestionThumbsLayout = shotRow;
  shotRow->addStretch(1);
  questionLay->addLayout(shotRow);
  layout->addWidget(questionBox);

  auto* sendRow = new QHBoxLayout();
  auto* captureBtn = new QPushButton(tr("Capture screenshot"), root);
  sendRow->addWidget(captureBtn);
  this->SendButton = new QPushButton(tr("Send to AI"), root);
  this->SendButton->setDefault(false);
  sendRow->addWidget(this->SendButton);
  auto* runBtn = new QPushButton(tr("Run script"), root);
  sendRow->addWidget(runBtn);
  sendRow->addStretch(1);
  layout->addLayout(sendRow);

  this->RenderViewCheck = new QCheckBox(tr("Access Auto Render Review"), root);
  this->RenderViewCheck->setToolTip(tr(
    "When on, the agent may capture and send RenderView JPEGs (costly). "
    "When off, use Capture screenshot on the question box to attach images yourself."));
  this->AgentModeCheck = new QCheckBox(
    tr("Agent mode (look up context, run script, and fix errors)"), root);
  this->RenderViewCheck->setChecked(false);
  this->AgentModeCheck->setChecked(true);
  layout->addWidget(this->RenderViewCheck);
  layout->addWidget(this->AgentModeCheck);

  layout->addWidget(new QLabel(tr("Code (ParaView Python)"), root));
  this->CodeEdit = makeEditor(root, true, 120);
  new pqSHYXPythonSyntaxHighlighter(this->CodeEdit);
  layout->addWidget(this->CodeEdit);

  auto* dialogHeader = new QWidget(root);
  auto* dialogHeaderLay = new QHBoxLayout(dialogHeader);
  dialogHeaderLay->setContentsMargins(0, 0, 0, 0);
  dialogHeaderLay->setSpacing(4);
  dialogHeaderLay->setAlignment(Qt::AlignVCenter);
  auto* dialogLabel = new QLabel(tr("Dialog"), dialogHeader);
  dialogHeaderLay->addWidget(dialogLabel, 0, Qt::AlignVCenter);
  dialogHeaderLay->addSpacing(8);
  auto* historyLabel = new QLabel(tr("History"), dialogHeader);
  historyLabel->setToolTip(tr(
    "Previous Dialog messages sent as context. 0 = no memory; 10 = last 10 bubbles."));
  dialogHeaderLay->addWidget(historyLabel, 0, Qt::AlignVCenter);
  this->HistorySlider = new QSlider(Qt::Horizontal, dialogHeader);
  this->HistorySlider->setRange(0, kHistoryMaxMessages);
  this->HistorySlider->setValue(0);
  this->HistorySlider->setSingleStep(1);
  this->HistorySlider->setPageStep(1);
  this->HistorySlider->setMinimumWidth(100);
  this->HistorySlider->setMaximumWidth(140);
  {
    int sliderH = this->HistorySlider->style()->pixelMetric(
      QStyle::PM_SliderThickness, nullptr, this->HistorySlider);
    sliderH = qMax(sliderH, dialogLabel->sizeHint().height());
    this->HistorySlider->setFixedHeight(sliderH);
  }
  this->HistorySlider->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  this->HistorySlider->setToolTip(historyLabel->toolTip());
  this->HistoryCountLabel = new QLabel(QStringLiteral("0"), dialogHeader);
  this->HistoryCountLabel->setMinimumWidth(16);
  this->HistoryCountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  this->HistoryCountLabel->setToolTip(historyLabel->toolTip());
  dialogHeaderLay->addWidget(this->HistorySlider, 0, Qt::AlignVCenter);
  dialogHeaderLay->addWidget(this->HistoryCountLabel, 0, Qt::AlignVCenter);
  dialogHeaderLay->addStretch(1);
  auto* dialogClearBtn = new QPushButton(tr("Clear"), dialogHeader);
  dialogClearBtn->setToolTip(tr("Clear the conversation"));
  auto* dialogPopBtn = new QPushButton(dialogHeader);
  dialogPopBtn->setIcon(this->style()->standardIcon(QStyle::SP_TitleBarMaxButton));
  dialogPopBtn->setToolTip(tr("Open Dialog in a separate window"));
  dialogPopBtn->setFlat(true);
  dialogPopBtn->setFixedSize(dialogPopBtn->sizeHint().height(), dialogPopBtn->sizeHint().height());
  dialogHeaderLay->addWidget(dialogClearBtn, 0, Qt::AlignVCenter);
  dialogHeaderLay->addWidget(dialogPopBtn, 0, Qt::AlignVCenter);
  layout->addWidget(dialogHeader);

  this->ChatView = new pqSHYXAIChatView(root);
  auto* dialogPopOut =
    new pqPopOutWidget(this->ChatView, tr("SHYX AI Assistant — Dialog"), root);
  dialogPopOut->setPopOutButton(dialogPopBtn);
  dialogPopOut->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
  layout->addWidget(dialogPopOut, 1);

  auto* apiBox = new QGroupBox(tr("OpenAI-compatible API"), root);
  auto* apiForm = new QFormLayout(apiBox);
  apiForm->setContentsMargins(4, 4, 4, 4);
  this->EndpointEdit = new QLineEdit(apiBox);
  this->EndpointEdit->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
  this->EndpointEdit->setText(QStringLiteral("https://api.openai.com/v1"));
  this->ModelEdit = new QLineEdit(apiBox);
  this->ModelEdit->setPlaceholderText(QStringLiteral("gpt-4o-mini"));
  this->ModelEdit->setText(QStringLiteral("gpt-4o-mini"));
  this->ApiKeyEdit = new QLineEdit(apiBox);
  this->ApiKeyEdit->setEchoMode(QLineEdit::Password);
  this->ApiKeyEdit->setPlaceholderText(tr("API key (saved locally, not in state files)"));
  apiForm->addRow(tr("Base URL"), this->EndpointEdit);
  apiForm->addRow(tr("Model"), this->ModelEdit);
  apiForm->addRow(tr("API key"), this->ApiKeyEdit);
  layout->addWidget(apiBox);

  this->StatusLabel = new QLabel(root);
  this->StatusLabel->setWordWrap(true);
  layout->addWidget(this->StatusLabel);

  this->Network = new QNetworkAccessManager(this);
  ensureHttpsTls();
  connect(this->HistorySlider, &QSlider::valueChanged, this, [this](int v) {
    if (this->HistoryCountLabel)
    {
      this->HistoryCountLabel->setText(QString::number(v));
    }
    this->saveClientSettings();
  });
  this->loadClientSettings();
  if (this->CodeEdit->toPlainText().trimmed().isEmpty())
  {
    this->CodeEdit->setPlainText(QString::fromUtf8(kDefaultCode));
  }

  connect(this->SendButton, &QPushButton::clicked, this, &pqSHYXAIAssistantPanel::onSendClicked);
  connect(captureBtn, &QPushButton::clicked, this, &pqSHYXAIAssistantPanel::onCaptureScreenshot);
  connect(runBtn, &QPushButton::clicked, this, &pqSHYXAIAssistantPanel::runCodeScript);
  connect(dialogClearBtn, &QPushButton::clicked, this, &pqSHYXAIAssistantPanel::onClearDialogClicked);
  connect(this->Network, &QNetworkAccessManager::finished, this, &pqSHYXAIAssistantPanel::onReplyFinished);
  connect(this->ApiKeyEdit, &QLineEdit::editingFinished, this, [this]() { this->saveClientSettings(); });
  connect(this->EndpointEdit, &QLineEdit::editingFinished, this, [this]() { this->saveClientSettings(); });
  connect(this->ModelEdit, &QLineEdit::editingFinished, this, [this]() { this->saveClientSettings(); });
}

pqSHYXAIAssistantPanel::~pqSHYXAIAssistantPanel()
{
  this->saveClientSettings();
  if (this->ActiveReply)
  {
    this->ActiveReply->abort();
  }
}

bool pqSHYXAIAssistantPanel::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == this->QuestionEdit && event)
  {
    if (event->type() == QEvent::InputMethod)
    {
      auto* ime = static_cast<QInputMethodEvent*>(event);
      this->ImePreedit = ime->preeditString();
      return Superclass::eventFilter(watched, event);
    }
    if (event->type() == QEvent::KeyPress)
    {
      auto* ke = static_cast<QKeyEvent*>(event);
      if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !ke->isAutoRepeat())
      {
        if (ke->modifiers() & Qt::ControlModifier)
        {
          this->QuestionEdit->insertPlainText(QStringLiteral("\n"));
          return true;
        }
        if (ke->modifiers() & (Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))
        {
          return Superclass::eventFilter(watched, event);
        }
        if (!this->ImePreedit.isEmpty())
        {
          return Superclass::eventFilter(watched, event);
        }
        this->onSendClicked();
        return true;
      }
    }
  }
  return Superclass::eventFilter(watched, event);
}

void pqSHYXAIAssistantPanel::onClearDialogClicked()
{
  this->ChatView->clear();
}

void pqSHYXAIAssistantPanel::onSendClicked()
{
  if (this->SendBusy)
  {
    this->stopChatRequest();
    return;
  }
  this->saveClientSettings();
  this->sendChatRequest();
}

void pqSHYXAIAssistantPanel::stopChatRequest()
{
  this->UserStopped = true;
  if (this->ActiveReply)
  {
    this->ActiveReply->abort();
    return;
  }
  this->AgentMessages = QJsonArray();
  this->AgentRound = 0;
  this->AgentFollowupJpegs.clear();
  if (this->ChatView->isStreaming())
  {
    const QString soFar = this->ChatView->streamingText().trimmed();
    if ((soFar.isEmpty() || soFar == QLatin1String("…")) &&
      this->ChatView->streamingThinking().trimmed().isEmpty())
    {
      this->ChatView->appendAssistantDelta(tr("Stopped."));
    }
    this->ChatView->finishAssistantStream();
  }
  this->setStatus(tr("Stopped."));
  this->setSendBusy(false);
}

void pqSHYXAIAssistantPanel::setSendBusy(bool busy)
{
  this->SendBusy = busy;
  if (!this->SendButton)
  {
    return;
  }
  this->SendButton->setEnabled(true);
  this->SendButton->setText(busy ? tr("Stop") : tr("Send to AI"));
}

void pqSHYXAIAssistantPanel::setStatus(const QString& text)
{
  this->StatusLabel->setText(text);
}

void pqSHYXAIAssistantPanel::loadClientSettings()
{
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  this->ApiKeyEdit->setText(settings.value(QStringLiteral("apiKey")).toString());
  if (settings.contains(QStringLiteral("endpointUrl")))
  {
    const QString endpoint = settings.value(QStringLiteral("endpointUrl")).toString().trimmed();
    if (!endpoint.isEmpty())
    {
      this->EndpointEdit->setText(endpoint);
    }
  }
  if (settings.contains(QStringLiteral("modelName")))
  {
    const QString model = settings.value(QStringLiteral("modelName")).toString().trimmed();
    if (!model.isEmpty())
    {
      this->ModelEdit->setText(model);
    }
  }
  if (this->HistorySlider)
  {
    this->HistorySlider->setValue(
      qBound(0, settings.value(QStringLiteral("historyCount"), 0).toInt(), kHistoryMaxMessages));
  }
}

void pqSHYXAIAssistantPanel::saveClientSettings() const
{
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QStringLiteral("apiKey"), this->ApiKeyEdit->text());
  settings.setValue(QStringLiteral("endpointUrl"), this->EndpointEdit->text().trimmed());
  settings.setValue(QStringLiteral("modelName"), this->ModelEdit->text().trimmed());
  if (this->HistorySlider)
  {
    settings.setValue(QStringLiteral("historyCount"), this->HistorySlider->value());
  }
}

void pqSHYXAIAssistantPanel::runCodeScript()
{
  if (this->RunningScript)
  {
    return;
  }
  const QString code = this->CodeEdit->toPlainText();
  if (code.trimmed().isEmpty())
  {
    this->setStatus(tr("Code box is empty; nothing to run."));
    return;
  }

  this->RunningScript = true;
  this->setStatus(tr("Running ParaView Python..."));
  const bool ran = executeOnHostPython(code.toUtf8());
  this->RunningScript = false;
  if (ran)
  {
    this->setStatus(tr("Script finished. Check Output Window for errors."));
    return;
  }
  const QString msg = tr(
    "Could not run the code box: this ParaView process has no Python manager, "
    "or executeCode could not be invoked. Use a Python-enabled ParaView "
    "(for example C:\\Program Files\\ParaView 6.0.1\\bin\\paraview.exe).");
  this->setStatus(msg);
  QMessageBox::warning(this, tr("SHYX AI Assistant"), msg);
}

QImage pqSHYXAIAssistantPanel::captureActiveViewImage() const
{
  pqView* view = pqActiveObjects::instance().activeView();
  if (!view || !view->getViewProxy())
  {
    return {};
  }
  vtkSmartPointer<vtkImageData> img;
  img.TakeReference(view->getViewProxy()->CaptureWindow(1));
  QImage qimg = imageFromVtk(img);
  if (qimg.isNull())
  {
    return {};
  }
  if (qimg.width() > kJpegMaxEdge || qimg.height() > kJpegMaxEdge)
  {
    qimg = qimg.scaled(kJpegMaxEdge, kJpegMaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  return qimg;
}

bool pqSHYXAIAssistantPanel::attachRenderView() const
{
  return this->RenderViewCheck && this->RenderViewCheck->isChecked();
}

void pqSHYXAIAssistantPanel::rebuildQuestionThumbs()
{
  if (!this->QuestionThumbsLayout)
  {
    return;
  }
  while (QLayoutItem* item = this->QuestionThumbsLayout->takeAt(0))
  {
    if (QWidget* w = item->widget())
    {
      w->deleteLater();
    }
    delete item;
  }
  QWidget* parent = this->QuestionThumbsLayout->parentWidget();
  const int cap = 96;
  for (int i = 0; i < this->QuestionImages.size(); ++i)
  {
    auto* cell = new QWidget(parent);
    auto* cellLay = new QHBoxLayout(cell);
    cellLay->setContentsMargins(0, 0, 0, 0);
    cellLay->setSpacing(0);
    auto* thumb = new ClickThumb(cell);
    QImage preview = this->QuestionImages[i];
    if (preview.width() > cap || preview.height() > cap)
    {
      preview = preview.scaled(cap, cap, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    thumb->setPixmap(QPixmap::fromImage(preview));
    const int idx = i;
    thumb->OnClick = [this, idx]() {
      if (idx < 0 || idx >= this->QuestionImages.size())
      {
        return;
      }
      const QImage marked =
        pqSHYXAIImageAnnotator::annotate(this, this->QuestionImages[idx]);
      if (!marked.isNull())
      {
        this->QuestionImages[idx] = marked.convertToFormat(QImage::Format_RGB32);
        this->rebuildQuestionThumbs();
      }
    };
    auto* rm = new QToolButton(cell);
    rm->setText(QStringLiteral("x"));
    rm->setAutoRaise(true);
    rm->setToolTip(tr("Remove screenshot"));
    QObject::connect(rm, &QToolButton::clicked, this, [this, idx]() {
      if (idx >= 0 && idx < this->QuestionImages.size())
      {
        this->QuestionImages.removeAt(idx);
        this->rebuildQuestionThumbs();
      }
    });
    cellLay->addWidget(thumb);
    cellLay->addWidget(rm, 0, Qt::AlignTop);
    this->QuestionThumbsLayout->addWidget(cell);
  }
  this->QuestionThumbsLayout->addStretch(1);
}

void pqSHYXAIAssistantPanel::clearQuestionImages()
{
  this->QuestionImages.clear();
  this->rebuildQuestionThumbs();
}

void pqSHYXAIAssistantPanel::onCaptureScreenshot()
{
  const QImage shot = this->captureActiveViewImage();
  if (shot.isNull())
  {
    this->setStatus(tr("Could not capture the active view."));
    return;
  }
  this->QuestionImages.append(shot.convertToFormat(QImage::Format_RGB32));
  this->rebuildQuestionThumbs();
  this->setStatus(tr("Screenshot added. Click a thumbnail to mark with a brush."));
}

QString pqSHYXAIAssistantPanel::buildUserText(const QString& question) const
{
  QString user;
  user += QStringLiteral("## User question\n%1\n\n").arg(question);
  user += QStringLiteral("## Current code box\n```python\n%1\n```\n\n")
            .arg(this->CodeEdit->toPlainText());
  return user;
}

QByteArray pqSHYXAIAssistantPanel::buildRequestJson(
  const QString& question, const QList<QByteArray>& jpegs) const
{
  QString sys = QString::fromUtf8(kSystemPrompt);
  sys += this->attachRenderView()
    ? QStringLiteral("\n- Render-view screenshots: ENABLED for this request.")
    : QStringLiteral(
        "\n- Render-view screenshots: DISABLED for this request (user chose this to save cost). "
        "Do not call capture_screenshot. Do not set capture=true on run_code_script. "
        "Use get_display, get_color_map, get_camera, or get_source_data instead.");
  QJsonArray messages;
  messages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("system") },
    { QStringLiteral("content"), sys } });

  const int historyN = this->HistorySlider ? this->HistorySlider->value() : 0;
  if (this->ChatView && historyN > 0)
  {
    for (const pqSHYXAIChatView::TranscriptTurn& turn : this->ChatView->lastTurns(historyN))
    {
      const QString text = truncateTail(turn.text, kHistoryKeepChars).trimmed();
      if (text.isEmpty())
      {
        continue;
      }
      messages.append(QJsonObject{
        { QStringLiteral("role"),
          turn.user ? QStringLiteral("user") : QStringLiteral("assistant") },
        { QStringLiteral("content"), text } });
    }
  }

  QJsonObject userMsg;
  userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
  const QString userText = this->buildUserText(question);
  if (jpegs.isEmpty())
  {
    userMsg.insert(QStringLiteral("content"), userText);
  }
  else
  {
    QJsonArray parts;
    parts.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") },
      { QStringLiteral("text"), userText } });
    for (const QByteArray& jpeg : jpegs)
    {
      if (jpeg.isEmpty())
      {
        continue;
      }
      const QString dataUrl =
        QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
      parts.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("image_url") },
        { QStringLiteral("image_url"),
          QJsonObject{ { QStringLiteral("url"), dataUrl } } } });
    }
    userMsg.insert(QStringLiteral("content"), parts);
  }
  messages.append(userMsg);

  QJsonObject root;
  root.insert(QStringLiteral("model"), this->ModelEdit->text().trimmed());
  root.insert(QStringLiteral("messages"), messages);
  root.insert(QStringLiteral("temperature"), 0.2);
  root.insert(QStringLiteral("stream"), true);
  if (this->AgentModeCheck && this->AgentModeCheck->isChecked())
  {
    root.insert(QStringLiteral("tools"), pqSHYXAIAgentTools::schema());
    applyToolRequestFields(root);
  }
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray pqSHYXAIAssistantPanel::buildAgentRequestJson() const
{
  QJsonObject root;
  root.insert(QStringLiteral("model"), this->ModelEdit->text().trimmed());
  root.insert(QStringLiteral("messages"), this->AgentMessages);
  root.insert(QStringLiteral("temperature"), 0.2);
  root.insert(QStringLiteral("stream"), true);
  root.insert(QStringLiteral("tools"), pqSHYXAIAgentTools::schema());
  applyToolRequestFields(root);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void pqSHYXAIAssistantPanel::postJson(const QByteArray& payload)
{
  const QString endpoint = this->EndpointEdit->text().trimmed();
  QNetworkRequest req(::completionsUrl(endpoint));
  if (req.url().scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0)
  {
    ensureHttpsTls();
    if (!QSslSocket::supportsSsl())
    {
      this->failRequest(tr("HTTPS is unavailable in this ParaView/Qt (%1). "
                           "Need plugins/tls (qschannelbackend.dll on Windows) next to VESPAPlugin.")
                          .arg(tlsDiagnostic()));
      return;
    }
  }
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  req.setRawHeader("Accept", "text/event-stream, application/json");
  const QString key = this->ApiKeyEdit->text().trimmed();
  if (!key.isEmpty())
  {
    req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
  }
  req.setTransferTimeout(120000);
  this->resetStreamState();
  QNetworkReply* reply = this->Network->post(req, payload);
  this->ActiveReply = reply;
  connect(reply, &QNetworkReply::readyRead, this, &pqSHYXAIAssistantPanel::onStreamReadyRead);
}

void pqSHYXAIAssistantPanel::sendChatRequest()
{
  QString question = this->QuestionEdit->toPlainText().trimmed();
  if (question.isEmpty() && this->QuestionImages.isEmpty())
  {
    this->setStatus(tr("Question box is empty."));
    return;
  }
  if (question.isEmpty())
  {
    question = tr("(see screenshot)");
  }
  const QString endpoint = this->EndpointEdit->text().trimmed();
  const QString model = this->ModelEdit->text().trimmed();
  if (endpoint.isEmpty() || model.isEmpty())
  {
    this->setStatus(tr("Set Base URL and Model first."));
    return;
  }

  QList<QByteArray> jpegs;
  for (const QImage& img : this->QuestionImages)
  {
    const QByteArray jpeg = jpegFromImage(img);
    if (!jpeg.isEmpty())
    {
      jpegs.append(jpeg);
    }
  }

  this->AgentMessages = QJsonArray();
  this->AgentFollowupJpegs.clear();
  this->AgentRound = 0;
  this->UserStopped = false;

  this->setSendBusy(true);
  if (jpegs.isEmpty())
  {
    this->setStatus(this->AgentModeCheck->isChecked() ? tr("Agent: sending...")
                                                      : tr("Sending to AI..."));
  }
  else
  {
    this->setStatus(tr("Sending to AI (with %1 screenshot(s))...").arg(jpegs.size()));
  }

  const QByteArray payload = this->buildRequestJson(question, jpegs);
  const QJsonDocument sent = QJsonDocument::fromJson(payload);
  if (sent.isObject())
  {
    this->AgentMessages = sent.object().value(QLatin1String("messages")).toArray();
  }

  QList<pqSHYXAIChatView::Attachment> atts;
  for (int i = 0; i < this->QuestionImages.size(); ++i)
  {
    pqSHYXAIChatView::Attachment shot;
    shot.title = (this->QuestionImages.size() == 1)
      ? tr("Screenshot")
      : tr("Screenshot %1").arg(i + 1);
    shot.image = this->QuestionImages[i];
    atts.append(shot);
  }
  this->ChatView->appendUser(question, atts);
  this->ChatView->beginAssistantStream();

  this->postJson(payload);
}

void pqSHYXAIAssistantPanel::resetStreamState()
{
  this->StreamBuf.clear();
  this->StreamContent.clear();
  this->StreamToolCalls.clear();
  this->StreamFinishReason.clear();
  this->StreamError.clear();
  this->StreamIsSse = false;
}

void pqSHYXAIAssistantPanel::onStreamReadyRead()
{
  auto* reply = qobject_cast<QNetworkReply*>(this->sender());
  if (!reply || reply != this->ActiveReply)
  {
    return;
  }
  this->ingestStreamBytes(reply->readAll());
}

void pqSHYXAIAssistantPanel::ingestStreamBytes(const QByteArray& bytes)
{
  if (bytes.isEmpty())
  {
    return;
  }
  this->StreamBuf += bytes;
  if (!this->StreamIsSse)
  {
    const QByteArray trimmed = this->StreamBuf.trimmed();
    if (trimmed.startsWith("data:") || this->StreamBuf.contains("\ndata:") ||
      this->StreamBuf.contains("\r\ndata:"))
    {
      this->StreamIsSse = true;
    }
    else if (trimmed.startsWith('{') || trimmed.startsWith('['))
    {
      return;
    }
  }
  if (!this->StreamIsSse)
  {
    return;
  }

  while (true)
  {
    const int nl = this->StreamBuf.indexOf('\n');
    if (nl < 0)
    {
      break;
    }
    QByteArray line = this->StreamBuf.left(nl);
    this->StreamBuf.remove(0, nl + 1);
    if (line.endsWith('\r'))
    {
      line.chop(1);
    }
    if (line.isEmpty() || line.startsWith(':'))
    {
      continue;
    }
    if (!line.startsWith("data:"))
    {
      continue;
    }
    QByteArray data = line.mid(5);
    if (data.startsWith(' '))
    {
      data = data.mid(1);
    }
    const QByteArray done = data.trimmed();
    if (done == "[DONE]")
    {
      continue;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject())
    {
      this->handleStreamEvent(doc.object());
    }
  }
}

void pqSHYXAIAssistantPanel::handleStreamEvent(const QJsonObject& obj)
{
  if (obj.contains(QLatin1String("error")))
  {
    this->StreamError = jsonErrorMessage(obj, this->StreamError.isEmpty()
        ? tr("AI request failed.")
        : this->StreamError);
    return;
  }
  const QJsonArray choices = obj.value(QLatin1String("choices")).toArray();
  if (choices.isEmpty())
  {
    return;
  }
  const QJsonObject choice = choices.at(0).toObject();
  const QJsonValue finish = choice.value(QLatin1String("finish_reason"));
  if (finish.isString() && !finish.toString().isEmpty())
  {
    this->StreamFinishReason = finish.toString();
  }

  QJsonObject delta = choice.value(QLatin1String("delta")).toObject();
  if (delta.isEmpty() && choice.contains(QLatin1String("message")))
  {
    delta = choice.value(QLatin1String("message")).toObject();
  }

  auto appendVisible = [this](const QString& piece) {
    if (piece.isEmpty())
    {
      return;
    }
    const QString soFar = this->ChatView->streamingText().trimmed();
    if (soFar.isEmpty() || soFar == QLatin1String("…"))
    {
      this->setStatus(tr("Receiving..."));
    }
    this->ChatView->appendAssistantDelta(piece);
  };

  const QJsonValue reasoning = delta.contains(QLatin1String("reasoning_content"))
    ? delta.value(QLatin1String("reasoning_content"))
    : delta.value(QLatin1String("reasoning"));
  if (reasoning.isString() && !reasoning.toString().isEmpty())
  {
    const QString soFar = this->ChatView->streamingText().trimmed();
    if (soFar.isEmpty() || soFar == QLatin1String("…"))
    {
      this->setStatus(tr("Thinking..."));
    }
    this->ChatView->appendAssistantThinkingDelta(reasoning.toString());
  }

  const QJsonValue content = delta.value(QLatin1String("content"));
  if (content.isString())
  {
    const QString piece = content.toString();
    this->StreamContent += piece;
    appendVisible(piece);
  }

  const QJsonArray toolDeltas = delta.value(QLatin1String("tool_calls")).toArray();
  for (const QJsonValue& v : toolDeltas)
  {
    if (!v.isObject())
    {
      continue;
    }
    const QJsonObject item = v.toObject();
    const int idx = item.value(QLatin1String("index")).toInt(0);
    QJsonObject acc = this->StreamToolCalls.value(idx);
    if (item.contains(QLatin1String("id")))
    {
      acc.insert(QStringLiteral("id"), item.value(QLatin1String("id")));
    }
    if (item.contains(QLatin1String("type")))
    {
      acc.insert(QStringLiteral("type"), item.value(QLatin1String("type")));
    }
    else if (!acc.contains(QLatin1String("type")))
    {
      acc.insert(QStringLiteral("type"), QStringLiteral("function"));
    }
    QJsonObject fn = acc.value(QLatin1String("function")).toObject();
    const QJsonObject dfn = item.value(QLatin1String("function")).toObject();
    const QString name = dfn.value(QLatin1String("name")).toString();
    if (!name.isEmpty())
    {
      fn.insert(QStringLiteral("name"), name);
    }
    if (dfn.contains(QLatin1String("arguments")))
    {
      fn.insert(QStringLiteral("arguments"),
        fn.value(QLatin1String("arguments")).toString() +
          dfn.value(QLatin1String("arguments")).toString());
    }
    acc.insert(QStringLiteral("function"), fn);
    this->StreamToolCalls.insert(idx, acc);
  }
}

QJsonObject pqSHYXAIAssistantPanel::assembledAssistantMessage() const
{
  QJsonObject msg;
  msg.insert(QStringLiteral("role"), QStringLiteral("assistant"));
  msg.insert(QStringLiteral("content"), this->StreamContent);
  if (!this->StreamToolCalls.isEmpty())
  {
    QJsonArray arr;
    for (auto it = this->StreamToolCalls.constBegin(); it != this->StreamToolCalls.constEnd(); ++it)
    {
      arr.append(it.value());
    }
    msg.insert(QStringLiteral("tool_calls"), arr);
  }
  return sanitizeAssistantToolMessage(msg);
}

void pqSHYXAIAssistantPanel::failRequest(const QString& err)
{
  QString shown = err;
  if (shown.contains(QLatin1String("TLS"), Qt::CaseInsensitive) ||
    shown.contains(QLatin1String("SSL"), Qt::CaseInsensitive))
  {
    shown = tr("%1 (%2)").arg(err, tlsDiagnostic());
  }
  this->setStatus(tr("AI request failed: %1").arg(shown));
  const QString line = tr("[request failed] %1").arg(shown);
  if (this->ChatView->isStreaming())
  {
    const QString soFar = this->ChatView->streamingText().trimmed();
    if (soFar.isEmpty() || soFar == QLatin1String("…"))
    {
      this->ChatView->appendAssistantDelta(line);
    }
    else
    {
      this->ChatView->appendAssistantDelta(QStringLiteral("\n\n") + line);
    }
    this->ChatView->finishAssistantStream();
  }
  else
  {
    this->ChatView->appendAssistant(line);
  }
  this->setSendBusy(false);
}

void pqSHYXAIAssistantPanel::completeStreamReply()
{
  if (!this->StreamError.isEmpty())
  {
    this->failRequest(this->StreamError);
    return;
  }
  const QJsonObject message = this->assembledAssistantMessage();
  if (this->AgentModeCheck && this->AgentModeCheck->isChecked() &&
    this->continueAgentIfNeeded(message))
  {
    return;
  }
  QString content;
  QList<QImage> images;
  parseAssistantContent(message.value(QLatin1String("content")), content, images);
  const QString visible = this->ChatView->streamingText().trimmed();
  if (content.trimmed().isEmpty() && images.isEmpty() &&
    (visible.isEmpty() || visible == QLatin1String("…")) &&
    this->ChatView->streamingThinking().trimmed().isEmpty())
  {
    this->failRequest(tr("AI returned an empty message."));
    return;
  }
  this->applyAssistantReply(content, images);
}

void pqSHYXAIAssistantPanel::onReplyFinished(QNetworkReply* reply)
{
  if (!reply)
  {
    return;
  }
  reply->deleteLater();
  if (this->ActiveReply == reply)
  {
    this->ActiveReply.clear();
  }
  else if (this->ActiveReply)
  {
    return;
  }

  this->ingestStreamBytes(reply->readAll());
  const QByteArray errorBody = this->StreamBuf;
  if (this->StreamIsSse)
  {
    const QByteArray rest = this->StreamBuf.trimmed();
    this->StreamBuf.clear();
    if (rest.startsWith("data:"))
    {
      QByteArray data = rest.mid(5);
      if (data.startsWith(' '))
      {
        data = data.mid(1);
      }
      if (data.trimmed() != "[DONE]")
      {
        const QJsonDocument last = QJsonDocument::fromJson(data);
        if (last.isObject())
        {
          this->handleStreamEvent(last.object());
        }
      }
    }
  }

  if (reply->error() != QNetworkReply::NoError)
  {
    const bool stopped = this->UserStopped ||
      reply->error() == QNetworkReply::OperationCanceledError;
    this->UserStopped = false;
    if (stopped)
    {
      this->AgentMessages = QJsonArray();
      this->AgentRound = 0;
      this->AgentFollowupJpegs.clear();
      if (this->ChatView->isStreaming())
      {
        const QString soFar = this->ChatView->streamingText().trimmed();
        const bool empty = soFar.isEmpty() || soFar == QLatin1String("…");
        if (empty && this->ChatView->streamingThinking().trimmed().isEmpty())
        {
          this->ChatView->appendAssistantDelta(tr("Stopped."));
        }
        this->ChatView->finishAssistantStream();
      }
      this->setStatus(tr("Stopped."));
      this->setSendBusy(false);
      return;
    }
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool httpFailed = http >= 400;
    const bool gotUsefulStream = this->StreamIsSse && this->StreamError.isEmpty() && !httpFailed &&
      (!this->StreamContent.isEmpty() || !this->StreamToolCalls.isEmpty());
    const bool closedAfterStream =
      reply->error() == QNetworkReply::RemoteHostClosedError ||
      reply->error() == QNetworkReply::UnknownNetworkError;
    if (gotUsefulStream && closedAfterStream)
    {
      this->completeStreamReply();
      return;
    }
    QString err = replyErrorDetail(reply, errorBody);
    if (!this->StreamError.isEmpty() && !err.contains(this->StreamError))
    {
      err = this->StreamError + QStringLiteral(" — ") + err;
    }
    this->failRequest(err);
    return;
  }

  if (!this->StreamIsSse)
  {
    const QJsonDocument doc = QJsonDocument::fromJson(this->StreamBuf);
    if (!doc.isObject())
    {
      this->failRequest(tr("AI response was not JSON."));
      return;
    }
    const QJsonObject obj = doc.object();
    if (obj.contains(QLatin1String("error")))
    {
      this->failRequest(jsonErrorMessage(obj, tr("AI request failed.")));
      return;
    }
    const QJsonArray choices = obj.value(QLatin1String("choices")).toArray();
    if (choices.isEmpty())
    {
      this->failRequest(tr("AI response had no choices."));
      return;
    }
    const QJsonObject message =
      choices.at(0).toObject().value(QLatin1String("message")).toObject();
    QString content;
    QList<QImage> images;
    parseAssistantContent(message.value(QLatin1String("content")), content, images);
    this->StreamContent = content;
    const QJsonArray calls = message.value(QLatin1String("tool_calls")).toArray();
    for (int i = 0; i < calls.size(); ++i)
    {
      this->StreamToolCalls.insert(i, calls.at(i).toObject());
    }
    QString think = message.value(QLatin1String("reasoning_content")).toString();
    if (think.isEmpty())
    {
      think = message.value(QLatin1String("reasoning")).toString();
    }
    if (!think.isEmpty())
    {
      this->ChatView->appendAssistantThinkingDelta(think);
    }
    if (!content.isEmpty())
    {
      const QString soFar = this->ChatView->streamingText().trimmed();
      if (soFar.isEmpty() || soFar == QLatin1String("…"))
      {
        this->ChatView->appendAssistantDelta(content);
      }
    }
    this->completeStreamReply();
    return;
  }

  this->completeStreamReply();
}

void pqSHYXAIAssistantPanel::applyAssistantReply(const QString& content, const QList<QImage>& images)
{
  this->ChatView->finishAssistantStream(images, {});
  this->setSendBusy(false);
  this->AgentMessages = QJsonArray();
  this->AgentRound = 0;
  this->QuestionEdit->clear();
  this->clearQuestionImages();
  QString code;
  if (extractPythonFence(content, code) && !code.isEmpty())
  {
    this->CodeEdit->setPlainText(code);
    this->setStatus(tr("AI updated the code box. Click Run script to execute it."));
  }
  else
  {
    this->setStatus(tr("AI replied in the dialog. Click Run script only if you want to execute the current code."));
  }
}

QString pqSHYXAIAssistantPanel::runAgentTool(const QString& name, const QJsonObject& args)
{
  if (name == QLatin1String("get_code_script"))
  {
    const QString code = this->CodeEdit ? this->CodeEdit->toPlainText() : QString();
    return code.trimmed().isEmpty() ? QStringLiteral("(code box is empty)") : code;
  }
  if (name == QLatin1String("set_code_script"))
  {
    QString code = args.value(QLatin1String("code")).toString();
    if (code.isEmpty())
    {
      code = args.value(QLatin1String("script")).toString();
    }
    if (code.trimmed().isEmpty())
    {
      return QStringLiteral("set_code_script requires a non-empty 'code' string.");
    }
    QString fenced;
    if (extractPythonFence(code, fenced) && !fenced.isEmpty())
    {
      code = fenced;
    }
    this->CodeEdit->setPlainText(code);
    return QStringLiteral("Code box updated (%1 chars, %2 lines). Call run_code_script to execute.")
      .arg(code.size())
      .arg(code.count(QLatin1Char('\n')) + 1);
  }
  if (name == QLatin1String("run_code_script"))
  {
    bool capture = this->attachRenderView();
    if (capture && args.contains(QLatin1String("capture")))
    {
      capture = args.value(QLatin1String("capture")).toBool(true);
    }
    return this->executeCodeBoxForAgent(capture);
  }
  if (name == QLatin1String("capture_screenshot"))
  {
    if (!this->attachRenderView())
    {
      return QStringLiteral(
        "Render-view screenshots are disabled. Enable 'Access Auto Render Review' "
        "in the panel if you need pixels. Use get_display, get_color_map, get_camera, "
        "or get_source_data instead.");
    }
    const QImage img = this->captureActiveViewImage();
    if (img.isNull())
    {
      return QStringLiteral("Could not capture the active view.");
    }
    const QByteArray jpeg = jpegFromImage(img);
    if (jpeg.isEmpty())
    {
      return QStringLiteral("Could not encode screenshot JPEG.");
    }
    this->AgentFollowupJpegs.append(jpeg);
    return QStringLiteral(
             "Captured %1x%2 JPEG; image attached as image_url on this turn. "
             "For pick_world_point use image_width=%3 image_height=%4 (origin=top_left).")
      .arg(img.width())
      .arg(img.height())
      .arg(img.width())
      .arg(img.height());
  }
  return pqSHYXAIAgentTools::run(name, args);
}

QString pqSHYXAIAssistantPanel::executeCodeBoxForAgent(bool captureScreenshot)
{
  if (this->RunningScript)
  {
    return QStringLiteral("A script is already running.");
  }
  const QString code = this->CodeEdit ? this->CodeEdit->toPlainText() : QString();
  if (code.trimmed().isEmpty())
  {
    return QStringLiteral("Code box is empty; call set_code_script first.");
  }

  pqSHYXAIOutputLog* log = pqSHYXAIOutputLog::instance();
  const int mark = log->lineCount();
  this->RunningScript = true;
  this->setStatus(tr("Agent: running script..."));
  const bool ran = executeOnHostPython(code.toUtf8());
  QCoreApplication::processEvents();
  if (pqView* view = pqActiveObjects::instance().activeView())
  {
    view->render();
  }
  QCoreApplication::processEvents();
  this->RunningScript = false;

  if (!ran)
  {
    return QStringLiteral(
      "Could not run the code box: this ParaView process has no Python manager. "
      "Use a Python-enabled ParaView (for example official ParaView 6.0.1).");
  }

  QString out = QStringLiteral("Script finished.\n");
  const QString fresh = log->linesFrom(mark, 8000);
  if (fresh.trimmed().isEmpty())
  {
    out += QStringLiteral("No new Output Window messages.\n");
  }
  else
  {
    out += QStringLiteral("Output Window since this run:\n%1\n").arg(fresh);
  }
  out += QStringLiteral("\nActive data after run:\n");
  out += pqSHYXAIAgentTools::run(QStringLiteral("get_active_data"), QJsonObject());
  out += QStringLiteral("\n\nPipeline after run (reuse these names; do not create duplicates):\n");
  out += pqSHYXAIAgentTools::run(QStringLiteral("get_pipeline_tree"), QJsonObject());

  if (captureScreenshot)
  {
    const QImage img = this->captureActiveViewImage();
    const QByteArray jpeg = jpegFromImage(img);
    if (jpeg.isEmpty())
    {
      out += QStringLiteral("\nScreenshot: could not capture the active view.");
    }
    else
    {
      this->AgentFollowupJpegs.append(jpeg);
      out += QStringLiteral("\nScreenshot: captured %1x%2 JPEG; image attached on this turn.")
               .arg(img.width())
               .arg(img.height());
    }
  }

  out += QStringLiteral(
    "\nIf you see a Python traceback, ERROR, empty/wrong data, or a bad view, "
    "fix with set_code_script and run_code_script again.");
  return out;
}

bool pqSHYXAIAssistantPanel::continueAgentIfNeeded(const QJsonObject& message)
{
  const QJsonArray calls = message.value(QLatin1String("tool_calls")).toArray();
  if (calls.isEmpty())
  {
    return false;
  }
  constexpr int kMaxRounds = 16;
  if (this->AgentRound >= kMaxRounds)
  {
    this->setStatus(tr("Agent stopped after %1 tool rounds.").arg(kMaxRounds));
    return false;
  }

  this->AgentMessages.append(sanitizeAssistantToolMessage(message));
  QStringList names;
  const QJsonArray sanitizedCalls =
    this->AgentMessages.last().toObject().value(QLatin1String("tool_calls")).toArray();
  for (const QJsonValue& v : sanitizedCalls)
  {
    if (this->UserStopped)
    {
      break;
    }
    const QJsonObject call = v.toObject();
    const QString id = call.value(QLatin1String("id")).toString();
    const QJsonObject fn = call.value(QLatin1String("function")).toObject();
    const QString name = fn.value(QLatin1String("name")).toString();
    QJsonObject args;
    const QJsonDocument argDoc =
      QJsonDocument::fromJson(fn.value(QLatin1String("arguments")).toString().toUtf8());
    if (argDoc.isObject())
    {
      args = argDoc.object();
    }
    const QString result = this->runAgentTool(name, args);
    names << name;
    this->ChatView->appendAssistantToolCall(name, truncateTail(result, 4000));
    QJsonObject toolMsg;
    toolMsg.insert(QStringLiteral("role"), QStringLiteral("tool"));
    toolMsg.insert(QStringLiteral("tool_call_id"), id);
    if (!name.isEmpty())
    {
      toolMsg.insert(QStringLiteral("name"), name);
    }
    toolMsg.insert(QStringLiteral("content"), result);
    this->AgentMessages.append(toolMsg);
  }

  if (this->UserStopped)
  {
    return true;
  }

  if (this->attachRenderView() && !this->AgentFollowupJpegs.isEmpty())
  {
    QJsonArray parts;
    parts.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") },
      { QStringLiteral("text"), QStringLiteral("Screenshots from this agent turn:") } });
    for (const QByteArray& jpeg : this->AgentFollowupJpegs)
    {
      const QString dataUrl =
        QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
      parts.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("image_url") },
        { QStringLiteral("image_url"), QJsonObject{ { QStringLiteral("url"), dataUrl } } } });
    }
    this->AgentMessages.append(
      QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") },
        { QStringLiteral("content"), parts } });
  }
  this->AgentFollowupJpegs.clear();

  ++this->AgentRound;
  this->setStatus(tr("Agent: %1 (round %2/%3)").arg(names.join(QStringLiteral(", "))).arg(this->AgentRound).arg(kMaxRounds));
  this->postJson(this->buildAgentRequestJson());
  return true;
}
