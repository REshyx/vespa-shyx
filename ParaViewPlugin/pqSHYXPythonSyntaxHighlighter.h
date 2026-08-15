#ifndef pqSHYXPythonSyntaxHighlighter_h
#define pqSHYXPythonSyntaxHighlighter_h

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class QPlainTextEdit;

/**
 * Python highlighter for the SHYX AI Assistant code box.
 * Imitates Programmable Filter (pqPythonSyntaxHighlighter): monospace, tab width
 * of 4 spaces, Tab at line start inserts 4 spaces. Uses QSyntaxHighlighter so it
 * works even when the plugin is built against a ParaView without pqPython.
 */
class pqSHYXPythonSyntaxHighlighter : public QSyntaxHighlighter
{
public:
  explicit pqSHYXPythonSyntaxHighlighter(QPlainTextEdit* editor);
  ~pqSHYXPythonSyntaxHighlighter() override = default;

protected:
  void highlightBlock(const QString& text) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  struct Rule
  {
    QRegularExpression pattern;
    QTextCharFormat format;
    int capture = 0;
  };

  void setupFormats(bool dark);
  void highlightStringsAndComments(const QString& text);

  QPlainTextEdit* Editor = nullptr;
  QVector<Rule> Rules;
  QTextCharFormat KeywordFormat;
  QTextCharFormat BuiltinFormat;
  QTextCharFormat StringFormat;
  QTextCharFormat CommentFormat;
  QTextCharFormat NumberFormat;
  QTextCharFormat DefFormat;
};

#endif
