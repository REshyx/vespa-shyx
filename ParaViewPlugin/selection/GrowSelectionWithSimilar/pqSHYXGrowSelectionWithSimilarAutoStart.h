#ifndef pqSHYXGrowSelectionWithSimilarAutoStart_h
#define pqSHYXGrowSelectionWithSimilarAutoStart_h

#include <QObject>
#include <QPointer>

class pqSHYXGrowSelectionWithSimilarViewFrameActions;

/**
 * Registers pqSHYXGrowSelectionWithSimilarViewFrameActions with the ParaView
 * interface tracker so each RenderView title bar gets the grow-similar button.
 */
class pqSHYXGrowSelectionWithSimilarAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXGrowSelectionWithSimilarAutoStart(QObject* parent = nullptr);
  ~pqSHYXGrowSelectionWithSimilarAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  Q_DISABLE_COPY(pqSHYXGrowSelectionWithSimilarAutoStart)

  QPointer<pqSHYXGrowSelectionWithSimilarViewFrameActions> Interface;
};

#endif
