#ifndef pqSHYXAIAssistantPanel_h
#define pqSHYXAIAssistantPanel_h

#include <QDockWidget>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointer>

class QCheckBox;
class QEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QWidget;
class pqSHYXAIChatView;

/**
 * View-menu dock for SHYX AI Assistant: question / code / dialog editors,
 * Send to AI, and OpenAI-compatible endpoint fields. The Run script button
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
  void onClearDialogClicked();
  void onReplyFinished(QNetworkReply* reply);
  void onStreamReadyRead();
  void onCaptureScreenshot();

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
  void applyAssistantReply(const QString& content, const QList<QImage>& images = {});
  bool continueAgentIfNeeded(const QJsonObject& message);
  QString runAgentTool(const QString& name, const QJsonObject& args);
  QString executeCodeBoxForAgent(bool captureScreenshot);
  bool attachRenderView() const;
  void postJson(const QByteArray& payload);
  void resetStreamState();
  void ingestStreamBytes(const QByteArray& bytes);
  void handleStreamEvent(const QJsonObject& obj);
  QJsonObject assembledAssistantMessage() const;
  void completeStreamReply();
  void failRequest(const QString& err);
  void stopChatRequest();
  void setSendBusy(bool busy);

  QPlainTextEdit* QuestionEdit = nullptr;
  QHBoxLayout* QuestionThumbsLayout = nullptr;
  QList<QImage> QuestionImages;
  QPlainTextEdit* CodeEdit = nullptr;
  pqSHYXAIChatView* ChatView = nullptr;
  QSlider* HistorySlider = nullptr;
  QLabel* HistoryCountLabel = nullptr;
  QLineEdit* EndpointEdit = nullptr;
  QLineEdit* ModelEdit = nullptr;
  QLineEdit* ApiKeyEdit = nullptr;
  QCheckBox* RenderViewCheck = nullptr;
  QCheckBox* AgentModeCheck = nullptr;
  QPushButton* SendButton = nullptr;
  QLabel* StatusLabel = nullptr;
  QNetworkAccessManager* Network = nullptr;
  QPointer<QNetworkReply> ActiveReply;
  QJsonArray AgentMessages;
  QList<QByteArray> AgentFollowupJpegs;
  int AgentRound = 0;
  QByteArray StreamBuf;
  QString StreamContent;
  QMap<int, QJsonObject> StreamToolCalls;
  QString StreamFinishReason;
  QString StreamError;
  bool StreamIsSse = false;
  bool RunningScript = false;
  bool UserStopped = false;
  bool SendBusy = false;
  QString ImePreedit;
};

#endif
