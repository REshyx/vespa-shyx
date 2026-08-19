#include "pqSHYXAIAssistantAutoStart.h"

#include "pqSHYXAIOutputLog.h"

pqSHYXAIAssistantAutoStart::pqSHYXAIAssistantAutoStart(QObject* parent)
  : Superclass(parent)
{
}

pqSHYXAIAssistantAutoStart::~pqSHYXAIAssistantAutoStart()
{
  this->onShutdown();
}

void pqSHYXAIAssistantAutoStart::onStartup()
{
  pqSHYXAIOutputLog::instance()->start();
}

void pqSHYXAIAssistantAutoStart::onShutdown()
{
  pqSHYXAIOutputLog::instance()->stop();
}
