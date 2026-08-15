#include "pqSHYXAIOutputLog.h"

#include "vtkCallbackCommand.h"
#include "vtkCommand.h"
#include "vtkObject.h"
#include "vtkOutputWindow.h"

#include <QMutexLocker>
#include <QRecursiveMutex>

namespace
{
QRecursiveMutex& logMutex()
{
  static QRecursiveMutex mutex;
  return mutex;
}
}

pqSHYXAIOutputLog* pqSHYXAIOutputLog::instance()
{
  static pqSHYXAIOutputLog log;
  return &log;
}

pqSHYXAIOutputLog::pqSHYXAIOutputLog()
  : Superclass(nullptr)
{
}

pqSHYXAIOutputLog::~pqSHYXAIOutputLog()
{
  this->stop();
}

void pqSHYXAIOutputLog::start()
{
  if (this->Observer)
  {
    return;
  }
  this->Observer = vtkCallbackCommand::New();
  this->Observer->SetClientData(this);
  this->Observer->SetCallback(&pqSHYXAIOutputLog::OnOutputWindow);
  if (vtkOutputWindow* win = vtkOutputWindow::GetInstance())
  {
    win->AddObserver(vtkCommand::ErrorEvent, this->Observer);
    win->AddObserver(vtkCommand::WarningEvent, this->Observer);
    win->AddObserver(vtkCommand::TextEvent, this->Observer);
  }
}

void pqSHYXAIOutputLog::stop()
{
  if (!this->Observer)
  {
    return;
  }
  if (vtkOutputWindow* win = vtkOutputWindow::GetInstance())
  {
    win->RemoveObserver(this->Observer);
  }
  this->Observer->Delete();
  this->Observer = nullptr;
}

void pqSHYXAIOutputLog::OnOutputWindow(
  vtkObject*, unsigned long event, void* clientdata, void* calldata)
{
  auto* self = static_cast<pqSHYXAIOutputLog*>(clientdata);
  const char* text = static_cast<const char*>(calldata);
  if (!self || !text || !text[0])
  {
    return;
  }
  const char* tag = "MSG";
  if (event == vtkCommand::ErrorEvent)
  {
    tag = "ERROR";
  }
  else if (event == vtkCommand::WarningEvent)
  {
    tag = "WARN";
  }
  self->appendLine(QString::fromUtf8(tag), QString::fromUtf8(text));
}

void pqSHYXAIOutputLog::appendLine(const QString& tag, const QString& text)
{
  QMutexLocker locker(&::logMutex());
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty())
  {
    return;
  }
  this->Lines.append(QStringLiteral("[%1] %2").arg(tag, trimmed));
  while (this->Lines.size() > MaxLines)
  {
    this->Lines.removeFirst();
  }
}

QString pqSHYXAIOutputLog::recentText(int maxChars) const
{
  QMutexLocker locker(&::logMutex());
  QString joined = this->Lines.join(QLatin1Char('\n'));
  if (maxChars > 0 && joined.size() > maxChars)
  {
    joined = joined.right(maxChars);
  }
  return joined;
}

QString pqSHYXAIOutputLog::recentErrors(int maxChars) const
{
  QMutexLocker locker(&::logMutex());
  QStringList errors;
  for (const QString& line : this->Lines)
  {
    if (line.startsWith(QLatin1String("[ERROR]")) || line.startsWith(QLatin1String("[WARN]")))
    {
      errors.append(line);
    }
  }
  QString joined = errors.join(QLatin1Char('\n'));
  if (maxChars > 0 && joined.size() > maxChars)
  {
    joined = joined.right(maxChars);
  }
  return joined;
}

int pqSHYXAIOutputLog::lineCount() const
{
  QMutexLocker locker(&::logMutex());
  return this->Lines.size();
}

QString pqSHYXAIOutputLog::linesFrom(int startIndex, int maxChars) const
{
  QMutexLocker locker(&::logMutex());
  if (startIndex < 0)
  {
    startIndex = 0;
  }
  if (startIndex >= this->Lines.size())
  {
    return {};
  }
  QString joined = this->Lines.mid(startIndex).join(QLatin1Char('\n'));
  if (maxChars > 0 && joined.size() > maxChars)
  {
    joined = joined.right(maxChars);
  }
  return joined;
}
