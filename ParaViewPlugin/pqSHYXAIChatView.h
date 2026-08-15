#ifndef pqSHYXAIChatView_h
#define pqSHYXAIChatView_h

#include <QImage>
#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

/**
 * Chat-style transcript for SHYX AI Assistant: user/assistant bubbles, optional
 * attached images (click to enlarge). plainText stays a User:/Assistant: log for
 * the model prompt.
 */
class pqSHYXAIChatView : public QWidget
{
  Q_OBJECT
  Q_PROPERTY(QString plainText READ plainText WRITE setPlainText NOTIFY textChanged)

public:
  explicit pqSHYXAIChatView(QWidget* parent = nullptr);

  QString plainText() const;
  void setPlainText(const QString& text);

  struct TranscriptTurn
  {
    bool user = false;
    QString text;
  };

  /// Last completed Dialog bubbles, oldest first. Empty / in-progress stream skipped.
  QList<TranscriptTurn> lastTurns(int maxCount) const;

  struct Attachment
  {
    QString title;
    QString body;
    QImage image;
  };

  void appendUser(const QString& text, const QList<Attachment>& attachments = {});
  void appendAssistant(const QString& text, const QList<QImage>& images = {},
    const QList<Attachment>& attachments = {});
  void beginAssistantStream();
  void appendAssistantDelta(const QString& chunk);
  void appendAssistantThinkingDelta(const QString& chunk);
  void appendAssistantToolCall(const QString& name, const QString& result);
  void finishAssistantStream(const QList<QImage>& images = {},
    const QList<Attachment>& attachments = {});
  bool isStreaming() const { return this->Streaming; }
  QString streamingText() const { return this->StreamText; }
  QString streamingThinking() const { return this->StreamThinking; }
  void clear();

Q_SIGNALS:
  void textChanged();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  struct ToolCall
  {
    QString name;
    QString result;
    int atChar = 0;
  };

  struct Message
  {
    bool user = false;
    QString text;
    QString thinking;
    QList<QImage> images;
    QList<Attachment> attachments;
    QList<ToolCall> toolCalls;
  };

  void appendMessage(const Message& msg, bool emitChanged);
  void rebuildFromTranscript(const QString& text);
  void addBubbleWidget(const Message& msg);
  void addToolFold(QWidget* frame, QVBoxLayout* layout, const QString& name, const QString& result,
    const QString& fg);
  void ensureStreamTextLabel();
  void clearBubbles();
  void removeLastMessage();
  void updateEmptyState();
  void updateBubbleWidths();
  void scrollToBottom();
  int bubbleMaxWidth() const;

  QScrollArea* Scroll = nullptr;
  QWidget* Inner = nullptr;
  QVBoxLayout* InnerLayout = nullptr;
  QLabel* EmptyLabel = nullptr;
  QList<Message> Messages;
  QList<QWidget*> BubbleFrames;
  QPointer<QLabel> StreamLabel;
  QPointer<QLabel> StreamThinkingLabel;
  QPointer<QWidget> StreamThinkingFold;
  QPointer<QToolButton> StreamThinkingToggle;
  QPointer<QWidget> StreamBubbleFrame;
  QPointer<QVBoxLayout> StreamBubbleLayout;
  QString StreamFg;
  QString StreamText;
  QString StreamThinking;
  QList<ToolCall> StreamTools;
  int StreamSegmentStart = 0;
  bool Streaming = false;
};

#endif
