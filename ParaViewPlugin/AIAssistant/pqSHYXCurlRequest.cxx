#include "pqSHYXCurlRequest.h"

#include <QMetaObject>
#include <QThread>
#include <QtGlobal>

#include <curl/curl.h>

#include <mutex>

namespace
{
size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* self = static_cast<pqSHYXCurlRequest*>(userdata);
  if (!self || self->isAborted())
  {
    return 0;
  }
  const size_t n = size * nmemb;
  if (n == 0)
  {
    return 0;
  }
  const QByteArray chunk(ptr, static_cast<int>(n));
  QMetaObject::invokeMethod(
    self, [self, chunk]() { self->handleChunk(chunk); }, Qt::QueuedConnection);
  return n;
}

int xferCb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
  auto* self = static_cast<pqSHYXCurlRequest*>(clientp);
  return (self && self->isAborted()) ? 1 : 0;
}
}

class pqSHYXCurlRequest::WorkerThread : public QThread
{
public:
  explicit WorkerThread(pqSHYXCurlRequest* owner)
    : QThread(owner)
    , Owner(owner)
  {
  }

  void run() override
  {
    pqSHYXCurlRequest::globalInit();
    CURL* curl = curl_easy_init();
    if (!curl)
    {
      pqSHYXCurlRequest* owner = this->Owner;
      QMetaObject::invokeMethod(
        owner,
        [owner]() { owner->handleDone(-1, 0, QStringLiteral("curl_easy_init failed"), false); },
        Qt::QueuedConnection);
      return;
    }

    const QByteArray url = this->Owner->Url.toUtf8();
    const QByteArray post = this->Owner->PostBody;
    const QList<QByteArray> headers = this->Owner->Headers;
    const bool postMethod = this->Owner->HttpMethod == pqSHYXCurlRequest::Method::Post;
    const int timeoutMs = this->Owner->TimeoutMs;

    curl_easy_setopt(curl, CURLOPT_URL, url.constData());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this->Owner);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &xferCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this->Owner);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    if (timeoutMs > 0)
    {
      curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    }
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "VESPA-SHYX-AI/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* hdr = nullptr;
    for (const QByteArray& line : headers)
    {
      hdr = curl_slist_append(hdr, line.constData());
    }
    if (hdr)
    {
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    }
    if (postMethod)
    {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.constData());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post.size()));
    }

    const CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    QString err;
    if (rc != CURLE_OK)
    {
      err = QString::fromUtf8(curl_easy_strerror(rc));
    }
    const bool remote = rc == CURLE_RECV_ERROR || rc == CURLE_PARTIAL_FILE ||
      rc == CURLE_GOT_NOTHING || rc == CURLE_SEND_ERROR;
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);

    pqSHYXCurlRequest* owner = this->Owner;
    QMetaObject::invokeMethod(
      owner,
      [owner, rc, http, err, remote]() {
        owner->handleDone(static_cast<int>(rc), static_cast<int>(http), err, remote);
      },
      Qt::QueuedConnection);
  }

private:
  pqSHYXCurlRequest* Owner = nullptr;
};

pqSHYXCurlRequest::pqSHYXCurlRequest(QObject* parent)
  : QObject(parent)
{
}

pqSHYXCurlRequest::~pqSHYXCurlRequest()
{
  this->blockSignals(true);
  this->abort();
}

void pqSHYXCurlRequest::globalInit()
{
  static std::once_flag once;
  std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

QString pqSHYXCurlRequest::diagnostic()
{
  globalInit();
  const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
  const char* ver = (info && info->version) ? info->version : "?";
  const char* ssl = (info && info->ssl_version) ? info->ssl_version : "none";
  return QStringLiteral("libcurl %1 ssl=%2").arg(QLatin1String(ver), QLatin1String(ssl));
}

void pqSHYXCurlRequest::setUrl(const QString& url)
{
  this->Url = url;
}

void pqSHYXCurlRequest::setMethod(Method method)
{
  this->HttpMethod = method;
}

void pqSHYXCurlRequest::addHeader(const QByteArray& line)
{
  if (!line.isEmpty())
  {
    this->Headers.append(line);
  }
}

void pqSHYXCurlRequest::setBody(const QByteArray& body)
{
  this->PostBody = body;
}

void pqSHYXCurlRequest::setTimeoutMs(int timeoutMs)
{
  this->TimeoutMs = timeoutMs;
}

void pqSHYXCurlRequest::start()
{
  if (this->Started)
  {
    return;
  }
  this->Started = true;
  this->AbortFlag.store(false);
  this->Canceled = false;
  this->Body.clear();
  this->ErrorString.clear();
  this->HttpStatus = 0;
  this->CurlCode = 0;
  this->RemoteClosed = false;
  globalInit();
  this->Thread = new WorkerThread(this);
  this->Thread->start();
}

void pqSHYXCurlRequest::abort()
{
  this->AbortFlag.store(true);
  if (this->Thread && this->Thread->isRunning())
  {
    const unsigned long waitMs =
      static_cast<unsigned long>(qMax(1000, this->TimeoutMs + 5000));
    this->Thread->wait(waitMs);
  }
}

bool pqSHYXCurlRequest::succeeded() const
{
  return !this->Canceled && this->CurlCode == 0 && this->HttpStatus > 0 && this->HttpStatus < 400;
}

void pqSHYXCurlRequest::handleChunk(const QByteArray& chunk)
{
  if (this->AbortFlag.load() || chunk.isEmpty())
  {
    return;
  }
  this->Body.append(chunk);
  Q_EMIT this->readyRead(chunk);
}

void pqSHYXCurlRequest::handleDone(
  int curlCode, int httpStatus, const QString& error, bool remoteClosed)
{
  this->CurlCode = curlCode;
  this->HttpStatus = httpStatus;
  this->RemoteClosed = remoteClosed;
  this->Canceled = this->AbortFlag.load() || curlCode == CURLE_ABORTED_BY_CALLBACK;
  if (this->Canceled)
  {
    this->ErrorString = QStringLiteral("canceled");
  }
  else if (curlCode == 0 && httpStatus >= 400)
  {
    this->ErrorString = QStringLiteral("HTTP %1").arg(httpStatus);
  }
  else
  {
    this->ErrorString = error;
  }
  Q_EMIT this->finished();
}
