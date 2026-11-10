#ifndef pqSHYXAIAssistantPanel_h
#define pqSHYXAIAssistantPanel_h

#include <QDockWidget>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QSet>

#include "pqSHYXCurlRequest.h"

class QCheckBox;
class QComboBox;
class QEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QWidget;
class pqSHYXAIChatView;

/**
 * View-menu dock for SHYX AI Assistant: question / code / dialog editors,
 * Send, and OpenAI-compatible endpoint fields. The Run script button
 * (and agent run_code_script) execute the code box.
 */
class pqSHYXAIAssistantPanel : public QDockWidget
{
  Q_OBJECT
  typedef QDockWidget Superclass;

public:
  pqSHYXAIAssistantPanel(const QString& title, QWidget* parent = nullptr);
  pqSHYXAIAssistantPanel(QWidget* parent = nullptr);
  ~pqSHYXAIAssistantPanel() override;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
  void onSendClicked();
  void onResetAllClicked();
  void onChatFinished();
  void onModelsFinished();
  void onStreamReadyRead(const QByteArray& bytes);
  void onCaptureScreenshot();
  void onRefreshModelsClicked();
  void onAddModelClicked();

private:
  void constructor();
  void setStatus(const QString& text);
  void loadClientSettings();
  void saveClientSettings() const;
  void runCodeScript();
  void sendChatRequest();
  QByteArray buildRequestJson(const QString& question, const QList<QByteArray>& jpegs) const;
  QByteArray buildAgentRequestJson() const;
  QString buildUserText(const QString& question) const;
  QImage captureActiveViewImage() const;
  void rebuildQuestionThumbs();
  void clearQuestionImages();
  void applyAssistantReply(const QList<QImage>& images = {});
  bool continueAgentIfNeeded(const QJsonObject& message);
  QString runAgentTool(const QString& name, const QJsonObject& args);
  QString executeCodeBoxForAgent(bool captureScreenshot);
  bool attachRenderView() const;
  void postJson(const QByteArray& payload);
  void resetStreamState();
  void resetAllSessionState();
  void ingestStreamBytes(const QByteArray& bytes);
  void handleStreamEvent(const QJsonObject& obj);
  QJsonObject assembledAssistantMessage() const;
  void completeStreamReply();
  void noteStreamingToolCall(int index, const QString& name, const QJsonObject& args);
  void failRequest(const QString& err);
  void dropActiveReply();
  void finishStoppedUi();
  void stopChatRequest();
  void setSendBusy(bool busy);
  QString currentModel() const;
  QString reasoningEffort() const;
  bool thinkingEnabled() const;
  void applyReasoningFields(QJsonObject& root) const;
  void ensureModelItem(const QString& name, bool makeCurrent);
  void applyModelsReply(pqSHYXCurlRequest* reply);
  void dispatchMessages(const QJsonArray& messages);
  void beginSummary(const QJsonArray& messages, int prefixEnd, int foldedTurns);
  void onSummaryFinished();
  void noteCompressed();
  bool retryIfContextOverflow(const QString& err);
  void abortSummaryReply();
  void failOrCompress(const QString& err);

  QPlainTextEdit* QuestionEdit = nullptr;
  QHBoxLayout* QuestionThumbsLayout = nullptr;
  QList<QImage> QuestionImages;
  QPlainTextEdit* CodeEdit = nullptr;
  pqSHYXAIChatView* ChatView = nullptr;
  QLineEdit* EndpointEdit = nullptr;
  QComboBox* ModelCombo = nullptr;
  QComboBox* ReasoningCombo = nullptr;
  QLineEdit* ApiKeyEdit = nullptr;
  QCheckBox* RenderViewCheck = nullptr;
  QCheckBox* AgentModeCheck = nullptr;
  QPushButton* SendButton = nullptr;
  QToolButton* RefreshModelsButton = nullptr;
  QLabel* StatusLabel = nullptr;
  QPointer<pqSHYXCurlRequest> ActiveReply;
  QPointer<pqSHYXCurlRequest> ModelsReply;
  QPointer<pqSHYXCurlRequest> SummaryReply;
  QJsonArray AgentMessages;
  QJsonArray PendingMessages;
  QString ContextSummary;
  int SummaryCoversTurns = 0;
  int HistoryTurnsAtSend = 0;
  int PendingPrefixEnd = 0;
  int PendingFoldedTurns = 0;
  bool SummaryUsedThisSend = false;
  bool ContextRetryUsed = false;
  QList<QByteArray> AgentFollowupJpegs;
  int AgentRound = 0;
  int AgentMaxRounds = 20;
  bool AgentOmitTools = false;
  QByteArray StreamBuf;
  QString StreamContent;
  QString StreamReasoning;
  QMap<int, QJsonObject> StreamToolCalls;
  QSet<int> StreamAnnouncedToolIndexes;
  QString StreamFinishReason;
  QString StreamError;
  bool StreamIsSse = false;
  bool RunningScript = false;
  bool UserStopped = false;
  bool SendBusy = false;
  QString ImePreedit;
};

#endif
