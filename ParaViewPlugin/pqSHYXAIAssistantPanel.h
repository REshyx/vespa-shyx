#ifndef pqSHYXAIAssistantPanel_h
#define pqSHYXAIAssistantPanel_h

#include "pqPropertyGroupWidget.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointer>

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QWidget;
class pqSHYXAIChatView;
class vtkSMPropertyGroup;

/**
 * Property-group panel for SHYX AI Assistant: question / code / dialog editors,
 * Send to AI, and OpenAI-compatible endpoint fields. Apply runs the code box.
 */
class pqSHYXAIAssistantPanel : public pqPropertyGroupWidget
{
  Q_OBJECT
  typedef pqPropertyGroupWidget Superclass;

public:
  pqSHYXAIAssistantPanel(vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXAIAssistantPanel() override;

  void apply() override;

private Q_SLOTS:
  void onSendClicked();
  void onClearDialogClicked();
  void onReplyFinished(QNetworkReply* reply);
  void onStreamReadyRead();
  void onCaptureScreenshot();

private:
  void setStatus(const QString& text);
  void loadClientSettings();
  void saveClientSettings() const;
  void runCodeScript();
  void sendChatRequest();
  QByteArray buildRequestJson(const QString& question, const QList<QByteArray>& jpegs) const;
  QByteArray buildAgentRequestJson() const;
  QString buildUserText(const QString& question) const;
  QString pipelineSummary() const;
  QImage captureActiveViewImage() const;
  void rebuildQuestionThumbs();
  void clearQuestionImages();
  void applyAssistantReply(const QString& content, const QList<QImage>& images = {});
  bool continueAgentIfNeeded(const QJsonObject& message);
  QString runAgentTool(const QString& name, const QJsonObject& args);
  QString executeCodeBoxForAgent(bool captureScreenshot);
  void postJson(const QByteArray& payload);
  void resetStreamState();
  void ingestStreamBytes(const QByteArray& bytes);
  void handleStreamEvent(const QJsonObject& obj);
  QJsonObject assembledAssistantMessage() const;
  void completeStreamReply();
  void failRequest(const QString& err);
  void linkStringProperty(QWidget* widget, const char* qproperty, const char* signal,
    const char* smName);

  QPlainTextEdit* QuestionEdit = nullptr;
  QHBoxLayout* QuestionThumbsLayout = nullptr;
  QList<QImage> QuestionImages;
  QPlainTextEdit* CodeEdit = nullptr;
  pqSHYXAIChatView* ChatView = nullptr;
  QLineEdit* EndpointEdit = nullptr;
  QLineEdit* ModelEdit = nullptr;
  QLineEdit* ApiKeyEdit = nullptr;
  QCheckBox* OutputLogCheck = nullptr;
  QCheckBox* PipelineCheck = nullptr;
  QCheckBox* AgentModeCheck = nullptr;
  QPushButton* SendButton = nullptr;
  QLabel* StatusLabel = nullptr;
  QNetworkAccessManager* Network = nullptr;
  QPointer<QNetworkReply> ActiveReply;
  QJsonArray AgentMessages;
  QString AgentToolLog;
  QList<QByteArray> AgentFollowupJpegs;
  int AgentRound = 0;
  QByteArray StreamBuf;
  QString StreamContent;
  QMap<int, QJsonObject> StreamToolCalls;
  QString StreamFinishReason;
  QString StreamError;
  bool StreamIsSse = false;
  bool RunningScript = false;
};

#endif
