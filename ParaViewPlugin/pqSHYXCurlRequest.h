#ifndef pqSHYXCurlRequest_h
#define pqSHYXCurlRequest_h

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <atomic>

/**
 * One-shot HTTP GET/POST via statically linked libcurl. Runs on a worker
 * thread and delivers body chunks / completion on the object's thread.
 */
class pqSHYXCurlRequest : public QObject
{
  Q_OBJECT
public:
  enum class Method
  {
    Get,
    Post
  };

  explicit pqSHYXCurlRequest(QObject* parent = nullptr);
  ~pqSHYXCurlRequest() override;

  void setUrl(const QString& url);
  void setMethod(Method method);
  void addHeader(const QByteArray& line);
  void setBody(const QByteArray& body);
  void setTimeoutMs(int timeoutMs);
  void start();
  void abort();

  bool isAborted() const { return this->AbortFlag.load(); }
  bool canceled() const { return this->Canceled; }
  QByteArray body() const { return this->Body; }
  int httpStatus() const { return this->HttpStatus; }
  int curlCode() const { return this->CurlCode; }
  QString errorString() const { return this->ErrorString; }
  bool remoteClosed() const { return this->RemoteClosed; }
  bool succeeded() const;

  static QString diagnostic();
  static void globalInit();

  void handleChunk(const QByteArray& chunk);
  void handleDone(int curlCode, int httpStatus, const QString& error, bool remoteClosed);

Q_SIGNALS:
  void readyRead(const QByteArray& chunk);
  void finished();

private:
  class WorkerThread;
  friend class WorkerThread;

  QString Url;
  Method HttpMethod = Method::Get;
  QList<QByteArray> Headers;
  QByteArray PostBody;
  int TimeoutMs = 120000;
  QByteArray Body;
  int HttpStatus = 0;
  int CurlCode = 0;
  QString ErrorString;
  bool RemoteClosed = false;
  bool Canceled = false;
  bool Started = false;
  std::atomic<bool> AbortFlag{ false };
  WorkerThread* Thread = nullptr;
};

#endif
