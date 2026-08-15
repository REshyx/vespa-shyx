#include "pqSHYXPythonSyntaxHighlighter.h"

#include <QEvent>
#include <QKeyEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTextCursor>

namespace
{
constexpr const char* kFourSpaces = "    ";

const char* kKeywords[] = { "False", "None", "True", "and", "as", "assert", "async", "await",
  "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from",
  "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
  "return", "try", "while", "with", "yield", nullptr };

const char* kBuiltins[] = { "abs", "all", "any", "bool", "dict", "enumerate", "filter", "float",
  "int", "len", "list", "map", "max", "min", "open", "print", "range", "repr", "set", "sorted",
  "str", "sum", "tuple", "type", "zip", "self", nullptr };

QString alternation(const char* const* words)
{
  QStringList parts;
  for (int i = 0; words[i]; ++i)
  {
    parts << QString::fromLatin1(words[i]);
  }
  return QStringLiteral("\\b(?:") + parts.join(QLatin1Char('|')) + QStringLiteral(")\\b");
}
}

pqSHYXPythonSyntaxHighlighter::pqSHYXPythonSyntaxHighlighter(QPlainTextEdit* editor)
  : QSyntaxHighlighter(editor->document())
  , Editor(editor)
{
  editor->installEventFilter(this);
  editor->setLineWrapMode(QPlainTextEdit::NoWrap);
  QFont font(QStringLiteral("Monospace"));
  font.setStyleHint(QFont::TypeWriter);
  editor->setFont(font);
  const QFontMetrics metrics = editor->fontMetrics();
  editor->setTabStopDistance(metrics.horizontalAdvance(QLatin1String(kFourSpaces)));

  const bool dark = editor->palette().color(QPalette::Base).lightness() < 128;
  this->setupFormats(dark);

  Rule kw;
  kw.pattern = QRegularExpression(alternation(kKeywords));
  kw.format = this->KeywordFormat;
  this->Rules.push_back(kw);

  Rule bi;
  bi.pattern = QRegularExpression(alternation(kBuiltins));
  bi.format = this->BuiltinFormat;
  this->Rules.push_back(bi);

  Rule num;
  num.pattern = QRegularExpression(QStringLiteral("\\b(?:0[xX][0-9A-Fa-f]+|\\d+(?:\\.\\d*)?(?:[eE][+-]?\\d+)?)\\b"));
  num.format = this->NumberFormat;
  this->Rules.push_back(num);

  Rule defn;
  defn.pattern = QRegularExpression(QStringLiteral("\\b(?:def|class)\\s+(\\w+)"));
  defn.format = this->DefFormat;
  defn.capture = 1;
  this->Rules.push_back(defn);
}

void pqSHYXPythonSyntaxHighlighter::setupFormats(bool dark)
{
  this->KeywordFormat.setFontWeight(QFont::Bold);
  this->DefFormat.setFontWeight(QFont::Bold);
  this->CommentFormat.setFontItalic(true);

  if (dark)
  {
    this->KeywordFormat.setForeground(QColor(86, 156, 214));
    this->BuiltinFormat.setForeground(QColor(78, 201, 176));
    this->StringFormat.setForeground(QColor(206, 145, 120));
    this->CommentFormat.setForeground(QColor(106, 153, 85));
    this->NumberFormat.setForeground(QColor(181, 206, 168));
    this->DefFormat.setForeground(QColor(220, 220, 170));
  }
  else
  {
    this->KeywordFormat.setForeground(QColor(0, 112, 32));
    this->BuiltinFormat.setForeground(QColor(0, 80, 128));
    this->StringFormat.setForeground(QColor(186, 33, 33));
    this->CommentFormat.setForeground(QColor(64, 128, 128));
    this->NumberFormat.setForeground(QColor(64, 64, 192));
    this->DefFormat.setForeground(QColor(0, 0, 128));
  }
}

void pqSHYXPythonSyntaxHighlighter::highlightBlock(const QString& text)
{
  for (const Rule& rule : this->Rules)
  {
    QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
    while (it.hasNext())
    {
      const QRegularExpressionMatch m = it.next();
      if (rule.capture > 0)
      {
        this->setFormat(m.capturedStart(rule.capture), m.capturedLength(rule.capture), rule.format);
      }
      else
      {
        this->setFormat(m.capturedStart(), m.capturedLength(), rule.format);
      }
    }
  }
  this->highlightStringsAndComments(text);
}

void pqSHYXPythonSyntaxHighlighter::highlightStringsAndComments(const QString& text)
{
  enum
  {
    Normal = 0,
    TripleSingle = 1,
    TripleDouble = 2
  };

  int state = this->previousBlockState();
  if (state < 0)
  {
    state = Normal;
  }

  int i = 0;
  const int n = text.size();
  while (i < n)
  {
    if (state == TripleSingle || state == TripleDouble)
    {
      const QString delim =
        (state == TripleSingle) ? QStringLiteral("'''") : QStringLiteral("\"\"\"");
      const int end = text.indexOf(delim, i);
      if (end < 0)
      {
        this->setFormat(i, n - i, this->StringFormat);
        this->setCurrentBlockState(state);
        return;
      }
      this->setFormat(i, end + 3 - i, this->StringFormat);
      i = end + 3;
      state = Normal;
      continue;
    }

    if (i + 2 < n && text.mid(i, 3) == QLatin1String("'''"))
    {
      state = TripleSingle;
      this->setFormat(i, 3, this->StringFormat);
      i += 3;
      continue;
    }
    if (i + 2 < n && text.mid(i, 3) == QLatin1String("\"\"\""))
    {
      state = TripleDouble;
      this->setFormat(i, 3, this->StringFormat);
      i += 3;
      continue;
    }

    const QChar c = text.at(i);
    if (c == QLatin1Char('#'))
    {
      this->setFormat(i, n - i, this->CommentFormat);
      break;
    }

    if (c == QLatin1Char('\'') || c == QLatin1Char('"'))
    {
      int j = i + 1;
      while (j < n)
      {
        if (text.at(j) == QLatin1Char('\\') && j + 1 < n)
        {
          j += 2;
          continue;
        }
        if (text.at(j) == c)
        {
          ++j;
          break;
        }
        ++j;
      }
      this->setFormat(i, j - i, this->StringFormat);
      i = j;
      continue;
    }

    ++i;
  }
  this->setCurrentBlockState(Normal);
}

bool pqSHYXPythonSyntaxHighlighter::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == this->Editor && event->type() == QEvent::KeyPress)
  {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->modifiers() == Qt::NoModifier && keyEvent->key() == Qt::Key_Tab)
    {
      QTextCursor tc = this->Editor->textCursor();
      tc.select(QTextCursor::LineUnderCursor);
      if (tc.selectedText().trimmed().isEmpty())
      {
        tc.insertText(QLatin1String(kFourSpaces));
        return true;
      }
    }
  }
  return QSyntaxHighlighter::eventFilter(obj, event);
}
