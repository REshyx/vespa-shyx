#include "pqSHYXAIChatView.h"

#include <QCursor>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSizePolicy>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
QString htmlFromPlain(const QString& text)
{
  return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

QString fencedCodeHtml(const QString& code)
{
  return QStringLiteral(
    "<table width='100%' cellspacing='0' cellpadding='8' "
    "style='background-color:rgba(0,0,0,0.16);margin:6px 0;'>"
    "<tr><td style='font-family:Consolas,monospace;white-space:pre-wrap;'>%1</td></tr>"
    "</table>")
    .arg(code.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>")));
}

QString markdownToHtml(const QString& md)
{
  static const QRegularExpression fence(QStringLiteral("```[\\w.+-]*\\r?\\n([\\s\\S]*?)```"));
  QStringList codeBlocks;
  QString stripped;
  int pos = 0;
  QRegularExpressionMatchIterator it = fence.globalMatch(md);
  while (it.hasNext())
  {
    const QRegularExpressionMatch m = it.next();
    stripped += md.mid(pos, m.capturedStart() - pos);
    stripped += QStringLiteral("\n\nSHYXCODEBLOCK%1\n\n").arg(codeBlocks.size());
    codeBlocks.append(m.captured(1));
    pos = m.capturedEnd();
  }
  stripped += md.mid(pos);

  QTextDocument doc;
  doc.setMarkdown(stripped);
  QString html = doc.toHtml();
  const int bodyOpen = html.indexOf(QLatin1String("<body"));
  const int bodyGt = html.indexOf(QLatin1Char('>'), bodyOpen);
  const int bodyClose = html.lastIndexOf(QLatin1String("</body>"));
  if (bodyGt >= 0 && bodyClose > bodyGt)
  {
    html = html.mid(bodyGt + 1, bodyClose - bodyGt - 1).trimmed();
  }
  html.replace(QRegularExpression(QStringLiteral("\\s*color\\s*:[^;\"']+;?")), QString());
  html.replace(QRegularExpression(QStringLiteral("<h[1-6][^>]*>")),
    QStringLiteral("<p style='font-weight:600;margin:8px 0 4px 0;'>"));
  html.replace(QRegularExpression(QStringLiteral("</h[1-6]>")), QStringLiteral("</p>"));
  html.replace(QLatin1String("<blockquote>"),
    QStringLiteral("<blockquote style='margin:6px 0;padding:2px 0 2px 8px;"
                   "border-left:3px solid rgba(128,128,128,0.55);'>"));
  html.replace(QLatin1String("<code>"),
    QStringLiteral("<code style='font-family:Consolas,monospace;"
                   "background:rgba(0,0,0,0.16);padding:0 3px;border-radius:3px;'>"));

  for (int i = 0; i < codeBlocks.size(); ++i)
  {
    const QString token = QStringLiteral("SHYXCODEBLOCK%1").arg(i);
    const QString box = fencedCodeHtml(codeBlocks[i]);
    const QRegularExpression para(
      QStringLiteral("<p[^>]*>\\s*%1\\s*</p>").arg(QRegularExpression::escape(token)));
    if (html.contains(para))
    {
      html.replace(para, box);
    }
    else
    {
      html.replace(token, box);
    }
  }
  return html;
}

QLabel* makeBodyLabel(
  QWidget* parent, const QString& text, const QString& fg, bool mono, bool markdown)
{
  auto* lab = new QLabel(parent);
  lab->setTextFormat(Qt::RichText);
  lab->setWordWrap(true);
  lab->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lab->setFocusPolicy(Qt::ClickFocus);
  lab->setStyleSheet(QStringLiteral("color: %1;").arg(fg));
  if (mono)
  {
    QFont font = lab->font();
    font.setFamily(QStringLiteral("Consolas"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSizeF(qMax(8.0, font.pointSizeF() - 1.0));
    lab->setFont(font);
  }
  if (markdown)
  {
    lab->setText(QStringLiteral("<div style='color:%1;'>%2</div>").arg(fg, markdownToHtml(text)));
  }
  else
  {
    lab->setText(htmlFromPlain(text));
  }
  return lab;
}

void showFullImage(QWidget* parent, const QImage& img)
{
  if (img.isNull())
  {
    return;
  }
  auto* dlg = new QDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle(QObject::tr("Image"));
  QSize maxSize(1280, 800);
  if (parent && parent->screen())
  {
    const QSize avail = parent->screen()->availableGeometry().size();
    maxSize = QSize(avail.width() * 3 / 4, avail.height() * 3 / 4);
  }
  QImage shown = img;
  if (img.width() > maxSize.width() || img.height() > maxSize.height())
  {
    shown = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  auto* lab = new QLabel(dlg);
  lab->setPixmap(QPixmap::fromImage(shown));
  lab->setAlignment(Qt::AlignCenter);
  auto* lay = new QVBoxLayout(dlg);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->addWidget(lab);
  dlg->resize(shown.size());
  dlg->show();
}

class ClickableImageLabel : public QLabel
{
public:
  ClickableImageLabel(const QImage& img, int maxW, QWidget* parent)
    : QLabel(parent)
    , Full(img)
  {
    this->setCursor(Qt::PointingHandCursor);
    this->setToolTip(QObject::tr("Click to view full size"));
    this->setScaledContents(false);
    this->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    const int cap = qMax(16, (qMax(80, maxW - 16) * 20) / 100);
    QImage thumb = img;
    if (img.width() > cap)
    {
      thumb = img.scaled(cap, cap, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    this->setPixmap(QPixmap::fromImage(thumb));
  }

protected:
  void mouseReleaseEvent(QMouseEvent* event) override
  {
    if (event->button() == Qt::LeftButton)
    {
      showFullImage(this->window(), this->Full);
    }
    QLabel::mouseReleaseEvent(event);
  }

private:
  QImage Full;
};

QWidget* makeFoldSection(const QString& title, QWidget* body, QWidget* parent, const QString& fg,
  bool startOpen = false, QToolButton** toggleOut = nullptr)
{
  auto* wrap = new QWidget(parent);
  auto* lay = new QVBoxLayout(wrap);
  lay->setContentsMargins(0, 2, 0, 0);
  lay->setSpacing(4);

  auto* toggle = new QToolButton(wrap);
  toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toggle->setArrowType(startOpen ? Qt::DownArrow : Qt::RightArrow);
  toggle->setText(title);
  toggle->setCheckable(true);
  toggle->setChecked(startOpen);
  toggle->setAutoRaise(true);
  toggle->setCursor(Qt::PointingHandCursor);
  toggle->setStyleSheet(QStringLiteral("QToolButton { color: %1; font-size: 11px; border: none; }")
                          .arg(fg));

  body->setParent(wrap);
  body->setVisible(startOpen);
  QObject::connect(toggle, &QToolButton::toggled, wrap, [toggle, body](bool on) {
    body->setVisible(on);
    toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
  });

  lay->addWidget(toggle, 0, Qt::AlignLeft);
  lay->addWidget(body);
  if (toggleOut)
  {
    *toggleOut = toggle;
  }
  return wrap;
}
}

pqSHYXAIChatView::pqSHYXAIChatView(QWidget* parent)
  : QWidget(parent)
{
  this->setMinimumHeight(360);
  this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  this->Scroll = new QScrollArea(this);
  this->Scroll->setWidgetResizable(true);
  this->Scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  this->Scroll->setFrameShape(QFrame::StyledPanel);
  this->Scroll->setFocusPolicy(Qt::StrongFocus);

  this->Inner = new QWidget(this->Scroll);
  this->InnerLayout = new QVBoxLayout(this->Inner);
  this->InnerLayout->setContentsMargins(8, 8, 8, 8);
  this->InnerLayout->setSpacing(10);

  this->EmptyLabel = new QLabel(tr("Conversation will appear here."), this->Inner);
  this->EmptyLabel->setWordWrap(true);
  this->EmptyLabel->setAlignment(Qt::AlignCenter);
  QPalette pal = this->EmptyLabel->palette();
  pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
  this->EmptyLabel->setPalette(pal);
  this->InnerLayout->addWidget(this->EmptyLabel);
  this->InnerLayout->addStretch(1);

  this->Scroll->setWidget(this->Inner);
  layout->addWidget(this->Scroll);
}

QString pqSHYXAIChatView::plainText() const
{
  QString out;
  for (const Message& msg : this->Messages)
  {
    if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
    {
      out += QLatin1Char('\n');
    }
    out += msg.user ? QStringLiteral("User: ") : QStringLiteral("Assistant: ");
    out += msg.text.trimmed();
    out += QStringLiteral("\n\n");
  }
  return out;
}

void pqSHYXAIChatView::setPlainText(const QString& text)
{
  if (text == this->plainText())
  {
    return;
  }
  this->rebuildFromTranscript(text);
}

void pqSHYXAIChatView::appendUser(const QString& text, const QList<Attachment>& attachments)
{
  Message msg;
  msg.user = true;
  msg.text = text;
  msg.attachments = attachments;
  this->appendMessage(msg, true);
}

void pqSHYXAIChatView::appendAssistant(
  const QString& text, const QList<QImage>& images, const QList<Attachment>& attachments)
{
  if (this->Streaming)
  {
    this->StreamText = text;
    this->finishAssistantStream(images, attachments);
    return;
  }
  Message msg;
  msg.user = false;
  msg.text = text;
  msg.images = images;
  msg.attachments = attachments;
  this->appendMessage(msg, true);
}

void pqSHYXAIChatView::beginAssistantStream()
{
  if (this->Streaming)
  {
    return;
  }
  this->Streaming = true;
  this->StreamText.clear();
  this->StreamThinking.clear();
  this->StreamLabel.clear();
  this->StreamThinkingLabel.clear();
  this->StreamThinkingFold.clear();
  this->StreamThinkingToggle.clear();
  Message msg;
  msg.user = false;
  msg.text = QStringLiteral("…");
  this->appendMessage(msg, false);
}

void pqSHYXAIChatView::appendAssistantDelta(const QString& chunk)
{
  if (chunk.isEmpty())
  {
    return;
  }
  if (!this->Streaming)
  {
    this->beginAssistantStream();
  }
  const bool firstContent = this->StreamText.isEmpty();
  if (firstContent && this->Messages.last().text == QStringLiteral("…"))
  {
    this->Messages.last().text.clear();
  }
  this->StreamText += chunk;
  if (!this->Messages.isEmpty() && !this->Messages.last().user)
  {
    this->Messages.last().text = this->StreamText;
  }
  if (firstContent && this->StreamThinkingToggle)
  {
    this->StreamThinkingToggle->setChecked(false);
  }
  if (this->StreamLabel)
  {
    this->StreamLabel->setTextFormat(Qt::PlainText);
    this->StreamLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->StreamLabel->setFocusPolicy(Qt::ClickFocus);
    this->StreamLabel->setWordWrap(true);
    this->StreamLabel->setText(this->StreamText);
  }
  this->scrollToBottom();
}

void pqSHYXAIChatView::appendAssistantThinkingDelta(const QString& chunk)
{
  if (chunk.isEmpty())
  {
    return;
  }
  if (!this->Streaming)
  {
    this->beginAssistantStream();
  }
  this->StreamThinking += chunk;
  if (!this->Messages.isEmpty() && !this->Messages.last().user)
  {
    this->Messages.last().thinking = this->StreamThinking;
  }
  if (this->StreamThinkingFold)
  {
    this->StreamThinkingFold->setVisible(true);
  }
  if (this->StreamThinkingToggle && this->StreamText.isEmpty())
  {
    this->StreamThinkingToggle->setChecked(true);
  }
  if (this->StreamThinkingLabel)
  {
    this->StreamThinkingLabel->setTextFormat(Qt::PlainText);
    this->StreamThinkingLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->StreamThinkingLabel->setFocusPolicy(Qt::ClickFocus);
    this->StreamThinkingLabel->setWordWrap(true);
    this->StreamThinkingLabel->setText(this->StreamThinking);
  }
  this->scrollToBottom();
}

void pqSHYXAIChatView::finishAssistantStream(
  const QList<QImage>& images, const QList<Attachment>& attachments)
{
  if (!this->Streaming)
  {
    Message msg;
    msg.user = false;
    msg.text = this->StreamText;
    msg.thinking = this->StreamThinking.trimmed();
    msg.images = images;
    msg.attachments = attachments;
    if (!msg.text.trimmed().isEmpty() || !msg.thinking.isEmpty() || !images.isEmpty() ||
      !attachments.isEmpty())
    {
      this->appendMessage(msg, true);
    }
    this->StreamText.clear();
    this->StreamThinking.clear();
    return;
  }
  Message msg;
  msg.user = false;
  msg.text = this->StreamText.trimmed();
  if (msg.text == QStringLiteral("…"))
  {
    msg.text.clear();
  }
  msg.thinking = this->StreamThinking.trimmed();
  msg.images = images;
  msg.attachments = attachments;
  this->Streaming = false;
  this->StreamLabel.clear();
  this->StreamThinkingLabel.clear();
  this->StreamThinkingFold.clear();
  this->StreamThinkingToggle.clear();
  this->removeLastMessage();
  if (!msg.text.isEmpty() || !msg.thinking.isEmpty() || !images.isEmpty() || !attachments.isEmpty())
  {
    this->appendMessage(msg, true);
  }
  else
  {
    this->updateEmptyState();
    Q_EMIT this->textChanged();
  }
  this->StreamText.clear();
  this->StreamThinking.clear();
}

void pqSHYXAIChatView::removeLastMessage()
{
  if (this->Messages.isEmpty())
  {
    return;
  }
  this->Messages.removeLast();
  if (this->BubbleFrames.isEmpty())
  {
    return;
  }
  QWidget* frame = this->BubbleFrames.takeLast();
  QWidget* row = frame;
  while (row && row->parentWidget() && row->parentWidget() != this->Inner)
  {
    row = row->parentWidget();
  }
  if (row)
  {
    this->InnerLayout->removeWidget(row);
    row->deleteLater();
  }
}

void pqSHYXAIChatView::clear()
{
  this->Streaming = false;
  this->StreamText.clear();
  this->StreamThinking.clear();
  this->StreamLabel.clear();
  this->StreamThinkingLabel.clear();
  this->StreamThinkingFold.clear();
  this->StreamThinkingToggle.clear();
  if (this->Messages.isEmpty())
  {
    return;
  }
  this->clearBubbles();
  this->Messages.clear();
  this->updateEmptyState();
  Q_EMIT this->textChanged();
}

void pqSHYXAIChatView::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  this->updateBubbleWidths();
}

void pqSHYXAIChatView::appendMessage(const Message& msg, bool emitChanged)
{
  this->Messages.append(msg);
  this->addBubbleWidget(msg);
  this->updateEmptyState();
  this->scrollToBottom();
  if (emitChanged)
  {
    Q_EMIT this->textChanged();
  }
}

void pqSHYXAIChatView::rebuildFromTranscript(const QString& text)
{
  this->clearBubbles();
  this->Messages.clear();

  static const QRegularExpression re(QStringLiteral("(^|\\n)(User|Assistant):\\s"));
  QRegularExpressionMatchIterator it = re.globalMatch(text);
  struct Hit
  {
    int matchStart = 0;
    int contentStart = 0;
    bool user = false;
  };
  QList<Hit> hits;
  while (it.hasNext())
  {
    const QRegularExpressionMatch m = it.next();
    Hit h;
    h.matchStart = m.capturedStart();
    h.contentStart = m.capturedEnd();
    h.user = (m.captured(2) == QLatin1String("User"));
    hits.append(h);
  }

  if (hits.isEmpty())
  {
    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty())
    {
      Message msg;
      msg.user = false;
      msg.text = trimmed;
      this->Messages.append(msg);
      this->addBubbleWidget(msg);
    }
  }
  else
  {
    for (int i = 0; i < hits.size(); ++i)
    {
      const int to = (i + 1 < hits.size()) ? hits[i + 1].matchStart : text.size();
      Message msg;
      msg.user = hits[i].user;
      msg.text = text.mid(hits[i].contentStart, to - hits[i].contentStart).trimmed();
      if (msg.text.isEmpty())
      {
        continue;
      }
      this->Messages.append(msg);
      this->addBubbleWidget(msg);
    }
  }
  this->updateEmptyState();
  this->scrollToBottom();
}

void pqSHYXAIChatView::addBubbleWidget(const Message& msg)
{
  const bool dark = this->palette().color(QPalette::Base).lightness() < 128;
  const int maxW = this->bubbleMaxWidth();

  auto* row = new QWidget(this->Inner);
  auto* rowLay = new QHBoxLayout(row);
  rowLay->setContentsMargins(0, 0, 0, 0);
  rowLay->setSpacing(0);

  auto* col = new QWidget(row);
  auto* colLay = new QVBoxLayout(col);
  colLay->setContentsMargins(0, 0, 0, 0);
  colLay->setSpacing(2);

  auto* cap = new QLabel(msg.user ? tr("You") : tr("Assistant"), col);
  QFont capFont = cap->font();
  capFont.setPointSizeF(qMax(8.0, capFont.pointSizeF() - 1.0));
  cap->setFont(capFont);
  cap->setAlignment(msg.user ? Qt::AlignRight : Qt::AlignLeft);
  QPalette capPal = cap->palette();
  capPal.setColor(QPalette::WindowText, capPal.color(QPalette::PlaceholderText));
  cap->setPalette(capPal);

  auto* frame = new QFrame(col);
  frame->setObjectName(QStringLiteral("shyxChatBubble"));
  frame->setMaximumWidth(maxW);
  const QString bg = msg.user ? (dark ? QStringLiteral("#1d4f91") : QStringLiteral("#2b6cb0"))
                              : (dark ? QStringLiteral("#3a3f46") : QStringLiteral("#e8edf2"));
  const QString fg = msg.user ? QStringLiteral("#ffffff")
                              : (dark ? QStringLiteral("#e6e6e6") : QStringLiteral("#1a1a1a"));
  frame->setStyleSheet(QStringLiteral("QFrame#shyxChatBubble {"
                                      " background-color: %1; color: %2;"
                                      " border-radius: 10px; }")
                         .arg(bg, fg));

  auto* bubbleLay = new QVBoxLayout(frame);
  bubbleLay->setContentsMargins(10, 8, 10, 8);
  bubbleLay->setSpacing(6);

  const bool streamAssistant = this->Streaming && !msg.user;
  if (streamAssistant || !msg.thinking.trimmed().isEmpty())
  {
    auto* thinkBody = new QWidget(frame);
    auto* thinkLay = new QVBoxLayout(thinkBody);
    thinkLay->setContentsMargins(8, 0, 0, 4);
    thinkLay->setSpacing(2);
    const QString thinkFg = dark ? QStringLiteral("#b0b6bd") : QStringLiteral("#5c6570");
    auto* thinkLab = makeBodyLabel(thinkBody,
      msg.thinking.isEmpty() ? QStringLiteral("…") : msg.thinking, thinkFg, false, false);
    thinkLab->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(thinkFg));
    if (streamAssistant && this->StreamThinkingLabel.isNull())
    {
      this->StreamThinkingLabel = thinkLab;
    }
    thinkLay->addWidget(thinkLab);
    QToolButton* thinkToggle = nullptr;
    auto* thinkFold = makeFoldSection(
      tr("Thinking"), thinkBody, frame, fg, false, &thinkToggle);
    if (msg.thinking.trimmed().isEmpty())
    {
      thinkFold->setVisible(false);
    }
    if (streamAssistant)
    {
      this->StreamThinkingFold = thinkFold;
      this->StreamThinkingToggle = thinkToggle;
    }
    bubbleLay->addWidget(thinkFold);
  }

  if (!msg.text.trimmed().isEmpty())
  {
    auto* lab = makeBodyLabel(frame, msg.text, fg, false, true);
    if (this->Streaming && !msg.user && this->StreamLabel.isNull())
    {
      this->StreamLabel = lab;
    }
    bubbleLay->addWidget(lab);
  }

  for (const Attachment& att : msg.attachments)
  {
    const QString bodyText = att.body.trimmed();
    const bool emptyBody = bodyText.isEmpty() || bodyText == QLatin1String("(none)");
    if (att.image.isNull() && emptyBody)
    {
      continue;
    }
    auto* body = new QWidget(frame);
    auto* bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(8, 0, 0, 4);
    bodyLay->setSpacing(4);
    if (!att.image.isNull())
    {
      bodyLay->addWidget(new ClickableImageLabel(att.image, maxW, body));
    }
    if (!att.body.trimmed().isEmpty())
    {
      bodyLay->addWidget(makeBodyLabel(body, att.body, fg, true, false));
    }
    const QString title = att.title.isEmpty() ? tr("Attachment") : att.title;
    bubbleLay->addWidget(makeFoldSection(title, body, frame, fg));
  }

  for (const QImage& img : msg.images)
  {
    if (img.isNull())
    {
      continue;
    }
    bubbleLay->addWidget(new ClickableImageLabel(img, maxW, frame));
  }

  colLay->addWidget(cap);
  colLay->addWidget(frame, 0, msg.user ? Qt::AlignRight : Qt::AlignLeft);

  if (msg.user)
  {
    rowLay->addStretch(1);
    rowLay->addWidget(col, 0, Qt::AlignRight | Qt::AlignTop);
  }
  else
  {
    rowLay->addWidget(col, 0, Qt::AlignLeft | Qt::AlignTop);
    rowLay->addStretch(1);
  }

  this->InnerLayout->insertWidget(this->InnerLayout->count() - 1, row);
  this->BubbleFrames.append(frame);
}

void pqSHYXAIChatView::clearBubbles()
{
  for (int i = this->InnerLayout->count() - 1; i >= 0; --i)
  {
    QLayoutItem* item = this->InnerLayout->itemAt(i);
    QWidget* w = item ? item->widget() : nullptr;
    if (!w || w == this->EmptyLabel)
    {
      continue;
    }
    this->InnerLayout->removeWidget(w);
    w->deleteLater();
  }
  this->BubbleFrames.clear();
}

void pqSHYXAIChatView::updateEmptyState()
{
  this->EmptyLabel->setVisible(this->Messages.isEmpty());
}

void pqSHYXAIChatView::updateBubbleWidths()
{
  const int maxW = this->bubbleMaxWidth();
  for (QWidget* frame : this->BubbleFrames)
  {
    if (frame)
    {
      frame->setMaximumWidth(maxW);
    }
  }
}

void pqSHYXAIChatView::scrollToBottom()
{
  QTimer::singleShot(0, this, [this]() {
    if (this->Scroll && this->Scroll->verticalScrollBar())
    {
      this->Scroll->verticalScrollBar()->setValue(this->Scroll->verticalScrollBar()->maximum());
    }
  });
}

int pqSHYXAIChatView::bubbleMaxWidth() const
{
  const int w = this->Scroll ? this->Scroll->viewport()->width() : this->width();
  return qMax(140, (w * 82) / 100);
}
