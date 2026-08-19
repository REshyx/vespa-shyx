#ifndef pqSHYXAIOutputLog_h
#define pqSHYXAIOutputLog_h

#include <QObject>
#include <QString>
#include <QStringList>

class vtkCallbackCommand;
class vtkObject;

/**
 * Client-side ring buffer of vtkOutputWindow text (errors/warnings/messages).
 * Started from plugin auto_start so messages are captured before the panel exists.
 */
class pqSHYXAIOutputLog : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  static pqSHYXAIOutputLog* instance();

  void start();
  void stop();
  QString recentText(int maxChars = 16000) const;
  QString recentErrors(int maxChars = 16000) const;
  int lineCount() const;
  QString linesFrom(int startIndex, int maxChars = 16000) const;

private:
  pqSHYXAIOutputLog();
  ~pqSHYXAIOutputLog() override;

  Q_DISABLE_COPY(pqSHYXAIOutputLog)

  static void OnOutputWindow(vtkObject* caller, unsigned long event, void* clientdata, void* calldata);
  void appendLine(const QString& tag, const QString& text);

  vtkCallbackCommand* Observer = nullptr;
  QStringList Lines;
  static constexpr int MaxLines = 200;
};

#endif
