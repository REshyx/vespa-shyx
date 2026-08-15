#ifndef pqSHYXAIAssistantAutoStart_h
#define pqSHYXAIAssistantAutoStart_h

#include <QObject>

/** Starts vtkOutputWindow capture so "Attach Output Window errors" can include
 *  messages from before the AI Assistant panel is opened. */
class pqSHYXAIAssistantAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXAIAssistantAutoStart(QObject* parent = nullptr);
  ~pqSHYXAIAssistantAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  Q_DISABLE_COPY(pqSHYXAIAssistantAutoStart)
};

#endif
