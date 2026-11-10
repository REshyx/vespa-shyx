#include "pqSHYXAIMemory.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int kAgentInjectChars = 4000;
constexpr int kAgentWriteChars = 4000;
constexpr int kSkillBodyChars = 16000;
constexpr int kCatalogChars = 6000;
constexpr int kDescriptionChars = 240;
constexpr int kUserFileChars = 100000;
constexpr auto kAgentRole = "__agent__";

struct Skill
{
  QString id;
  QString name;
  QString description;
  QString filePath;
};

QString memoryRoot()
{
  const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
    QStringLiteral("/SHYX/AIAssistant/memory");
  QDir().mkpath(root + QStringLiteral("/skills"));
  return QDir::cleanPath(root);
}

QString agentPath()
{
  return memoryRoot() + QStringLiteral("/Agent.md");
}

QString skillsDir()
{
  return memoryRoot() + QStringLiteral("/skills");
}

QString skillFile(const QString& id)
{
  return skillsDir() + QLatin1Char('/') + id + QStringLiteral("/SKILL.md");
}

QString readUtf8(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
  {
    return {};
  }
  QString text = QString::fromUtf8(file.readAll());
  if (text.startsWith(QChar(0xFEFF)))
  {
    text.remove(0, 1);
  }
  return text;
}

bool writeUtf8(const QString& path, const QString& text, QString* error)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
  {
    if (error)
    {
      *error = file.errorString();
    }
    return false;
  }
  const QByteArray bytes = text.toUtf8();
  if (file.write(bytes) != bytes.size() || !file.commit())
  {
    if (error)
    {
      *error = file.errorString();
    }
    return false;
  }
  return true;
}

bool idAllowed(const QString& id)
{
  if (id.isEmpty() || id.size() > 48 || id == QLatin1String(kAgentRole) || id.startsWith(QLatin1Char('-')) ||
    id.endsWith(QLatin1Char('-')) || id.contains(QLatin1String("--")))
  {
    return false;
  }
  for (const QChar c : id)
  {
    if (c == QLatin1Char('-'))
    {
      continue;
    }
    const bool asciiLower = c.isLetter() && c.unicode() < 128 && c.isLower();
    const bool digit = c.isDigit() && c.unicode() < 128;
    const bool cjk = c.unicode() >= 0x4e00 && c.unicode() <= 0x9fff;
    if (!asciiLower && !digit && !cjk)
    {
      return false;
    }
  }
  const QString lower = id.toLower();
  static const QStringList reserved{ QStringLiteral("con"), QStringLiteral("prn"), QStringLiteral("aux"),
    QStringLiteral("nul"), QStringLiteral("com1"), QStringLiteral("com2"), QStringLiteral("lpt1") };
  return !reserved.contains(lower);
}

QString sanitizeId(const QString& raw)
{
  QString out;
  bool pendingHyphen = false;
  const QString bad = QStringLiteral("\\/:*?\"<>|.#`");
  for (const QChar c : raw.trimmed())
  {
    if (c.isSpace() || c == QLatin1Char('_') || c == QLatin1Char('/'))
    {
      pendingHyphen = !out.isEmpty();
      continue;
    }
    if (c == QLatin1Char('-'))
    {
      pendingHyphen = !out.isEmpty();
      continue;
    }
    if (c.category() == QChar::Other_Control || bad.contains(c))
    {
      continue;
    }
    const bool asciiLetter = c.isLetter() && c.unicode() < 128;
    const bool digit = c.isDigit() && c.unicode() < 128;
    const bool cjk = c.unicode() >= 0x4e00 && c.unicode() <= 0x9fff;
    if (!asciiLetter && !digit && !cjk)
    {
      continue;
    }
    if (pendingHyphen)
    {
      out += QLatin1Char('-');
      pendingHyphen = false;
    }
    out += asciiLetter ? c.toLower() : c;
    if (out.size() >= 48)
    {
      break;
    }
  }
  while (out.endsWith(QLatin1Char('-')))
  {
    out.chop(1);
  }
  if (!idAllowed(out))
  {
    return {};
  }
  return out;
}

QString oneLine(QString text, int maxChars)
{
  text.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t]+")), QStringLiteral(" "));
  text.replace(QStringLiteral("---"), QStringLiteral("—"));
  text = text.trimmed();
  if (maxChars > 0 && text.size() > maxChars)
  {
    text = text.left(maxChars).trimmed();
  }
  return text;
}

struct Frontmatter
{
  QString name;
  QString description;
};

Frontmatter parseFrontmatter(const QString& text)
{
  Frontmatter front;
  if (!text.startsWith(QLatin1String("---")))
  {
    return front;
  }
  const int firstNl = text.indexOf(QLatin1Char('\n'));
  if (firstNl < 0)
  {
    return front;
  }
  const int close = text.indexOf(QLatin1String("\n---"), firstNl);
  if (close < 0)
  {
    return front;
  }
  const QString block = text.mid(firstNl + 1, close - firstNl - 1);
  for (QString line : block.split(QLatin1Char('\n')))
  {
    line = line.trimmed();
    QString* dest = nullptr;
    QString rest;
    if (line.startsWith(QLatin1String("name:")))
    {
      dest = &front.name;
      rest = line.mid(5);
    }
    else if (line.startsWith(QLatin1String("description:")))
    {
      dest = &front.description;
      rest = line.mid(12);
    }
    if (!dest)
    {
      continue;
    }
    rest = rest.trimmed();
    if (rest.size() >= 2 &&
      ((rest.startsWith(QLatin1Char('"')) && rest.endsWith(QLatin1Char('"'))) ||
        (rest.startsWith(QLatin1Char('\'')) && rest.endsWith(QLatin1Char('\'')))))
    {
      rest = rest.mid(1, rest.size() - 2);
    }
    *dest = rest.trimmed();
  }
  return front;
}

QString stripFrontmatter(const QString& text)
{
  if (!text.startsWith(QLatin1String("---")))
  {
    return text;
  }
  const int firstNl = text.indexOf(QLatin1Char('\n'));
  if (firstNl < 0)
  {
    return text;
  }
  const int close = text.indexOf(QLatin1String("\n---"), firstNl);
  if (close < 0)
  {
    return text;
  }
  int bodyAt = close + 4;
  if (bodyAt < text.size() && text.at(bodyAt) == QLatin1Char('\r'))
  {
    ++bodyAt;
  }
  if (bodyAt < text.size() && text.at(bodyAt) == QLatin1Char('\n'))
  {
    ++bodyAt;
  }
  return text.mid(bodyAt);
}

QList<Skill> loadSkills()
{
  QList<Skill> skills;
  const QDir dir(skillsDir());
  const QStringList ids = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& id : ids)
  {
    if (!idAllowed(id))
    {
      continue;
    }
    const QString path = skillFile(id);
    if (!QFileInfo::exists(path))
    {
      continue;
    }
    const Frontmatter front = parseFrontmatter(readUtf8(path));
    Skill skill;
    skill.id = id;
    skill.name = front.name.isEmpty() ? id : front.name;
    skill.description = oneLine(front.description, kDescriptionChars);
    skill.filePath = path;
    skills.append(skill);
  }
  std::sort(skills.begin(), skills.end(), [](const Skill& a, const Skill& b) {
    return a.id.localeAwareCompare(b.id) < 0;
  });
  return skills;
}

bool findSkill(const QString& query, Skill* out)
{
  const QString q = query.trimmed();
  if (q.isEmpty() || !out)
  {
    return false;
  }
  const QList<Skill> skills = loadSkills();
  int byName = -1;
  for (int i = 0; i < skills.size(); ++i)
  {
    if (skills.at(i).id.compare(q, Qt::CaseInsensitive) == 0)
    {
      *out = skills.at(i);
      return true;
    }
    if (byName < 0 && skills.at(i).name.compare(q, Qt::CaseInsensitive) == 0)
    {
      byName = i;
    }
  }
  if (byName < 0)
  {
    return false;
  }
  *out = skills.at(byName);
  return true;
}

QString catalogText()
{
  const QList<Skill> skills = loadSkills();
  if (skills.isEmpty())
  {
    return QStringLiteral("(no skills)");
  }
  QString out;
  int hidden = 0;
  for (int i = 0; i < skills.size(); ++i)
  {
    const Skill& skill = skills.at(i);
    QString line = QStringLiteral("- %1: %2\n").arg(skill.id, skill.description.isEmpty()
                                                      ? QStringLiteral("(no description)")
                                                      : skill.description);
    if (skill.name.compare(skill.id, Qt::CaseInsensitive) != 0)
    {
      line = QStringLiteral("- %1 (name: %2): %3\n")
               .arg(skill.id, oneLine(skill.name, 80),
                 skill.description.isEmpty() ? QStringLiteral("(no description)") : skill.description);
    }
    if (out.size() + line.size() > kCatalogChars)
    {
      hidden = skills.size() - i;
      break;
    }
    out += line;
  }
  if (hidden > 0)
  {
    out += QStringLiteral("(%1 more skills not listed; open Memory)\n").arg(hidden);
  }
  return out;
}

QString allocateId(const QString& base, QString* error)
{
  if (!QFileInfo::exists(skillFile(base)))
  {
    return base;
  }
  for (int n = 2; n < 100; ++n)
  {
    QString id = base;
    const QString suffix = QStringLiteral("-%1").arg(n);
    if (id.size() + suffix.size() > 48)
    {
      id = id.left(48 - suffix.size());
      while (id.endsWith(QLatin1Char('-')))
      {
        id.chop(1);
      }
    }
    id += suffix;
    if (idAllowed(id) && !QFileInfo::exists(skillFile(id)))
    {
      return id;
    }
  }
  if (error)
  {
    *error = QStringLiteral("Too many skills already use the name '%1'.").arg(base);
  }
  return {};
}

bool removeSkillDir(const QString& id, QString* error)
{
  if (!idAllowed(id))
  {
    if (error)
    {
      *error = QStringLiteral("Refusing to delete '%1'.").arg(id);
    }
    return false;
  }
  QDir parent(skillsDir());
  if (!parent.exists(id))
  {
    return true;
  }
  QDir dir(parent.filePath(id));
  if (!dir.exists())
  {
    return true;
  }
  if (!dir.removeRecursively())
  {
    if (error)
    {
      *error = QStringLiteral("Could not delete %1.").arg(QDir::toNativeSeparators(dir.absolutePath()));
    }
    return false;
  }
  return true;
}

class MemoryDialog : public QDialog
{
public:
  explicit MemoryDialog(QWidget* parent)
    : QDialog(parent)
  {
    this->setWindowTitle(tr("SHYX AI Memory"));
    this->resize(840, 520);
    auto* layout = new QVBoxLayout(this);
    auto* split = new QSplitter(this);
    this->List = new QListWidget(split);
    this->List->setMinimumWidth(180);
    this->Editor = new QPlainTextEdit(split);
    this->Editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    split->addWidget(this->List);
    split->addWidget(this->Editor);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);

    auto* buttons = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("New skill"), this);
    auto* deleteBtn = new QPushButton(tr("Delete"), this);
    auto* saveBtn = new QPushButton(tr("Save"), this);
    auto* openBtn = new QPushButton(tr("Open folder"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    buttons->addWidget(addBtn);
    buttons->addWidget(deleteBtn);
    buttons->addWidget(saveBtn);
    buttons->addWidget(openBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    this->PathLabel = new QLabel(this);
    this->PathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->PathLabel->setWordWrap(true);
    layout->addWidget(this->PathLabel);

    connect(this->List, &QListWidget::currentRowChanged, this, [this](int row) { this->onRowChanged(row); });
    connect(this->Editor, &QPlainTextEdit::textChanged, this, [this]() {
      if (!this->Block)
      {
        this->Dirty = true;
      }
    });
    connect(addBtn, &QPushButton::clicked, this, [this]() { this->newSkill(); });
    connect(deleteBtn, &QPushButton::clicked, this, [this]() { this->deleteCurrent(); });
    connect(saveBtn, &QPushButton::clicked, this, [this]() { this->saveCurrent(); });
    connect(openBtn, &QPushButton::clicked, this, [this]() { this->openFolder(); });
    connect(closeBtn, &QPushButton::clicked, this, [this]() { this->close(); });
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, [this]() { this->saveCurrent(); });

    this->rebuild(QString());
  }

protected:
  void closeEvent(QCloseEvent* event) override
  {
    if (!this->confirmSave())
    {
      event->ignore();
      return;
    }
    event->accept();
  }

private:
  bool isAgentRow(int row) const
  {
    QListWidgetItem* item = this->List->item(row);
    return item && item->data(Qt::UserRole).toString() == QLatin1String(kAgentRole);
  }

  QString rowId(int row) const
  {
    QListWidgetItem* item = this->List->item(row);
    if (!item || this->isAgentRow(row))
    {
      return {};
    }
    return item->data(Qt::UserRole).toString();
  }

  void rebuild(const QString& selectId)
  {
    this->Block = true;
    this->List->clear();
    auto* agent = new QListWidgetItem(tr("Agent.md"));
    agent->setData(Qt::UserRole, QString::fromLatin1(kAgentRole));
    agent->setToolTip(tr("Preferences included in every request."));
    this->List->addItem(agent);
    int selectRow = 0;
    const QList<Skill> skills = loadSkills();
    for (int i = 0; i < skills.size(); ++i)
    {
      auto* item = new QListWidgetItem(skills.at(i).id);
      item->setData(Qt::UserRole, skills.at(i).id);
      item->setToolTip(skills.at(i).description);
      this->List->addItem(item);
      if (!selectId.isEmpty() && skills.at(i).id == selectId)
      {
        selectRow = i + 1;
      }
    }
    this->CurrentRow = -1;
    this->Dirty = false;
    this->Block = false;
    this->List->setCurrentRow(selectRow);
  }

  void loadRow(int row)
  {
    this->Block = true;
    QString text;
    QString path;
    if (this->isAgentRow(row))
    {
      path = agentPath();
      text = readUtf8(path);
    }
    else
    {
      path = skillFile(this->rowId(row));
      text = readUtf8(path);
    }
    this->Editor->setPlainText(text);
    this->CurrentRow = row;
    this->Dirty = false;
    this->Block = false;
    this->showPath(path, text.size());
  }

  void showPath(const QString& path, int chars)
  {
    QString note = QDir::toNativeSeparators(path);
    note += QStringLiteral("  (%1 characters)").arg(chars);
    if (this->isAgentRow(this->CurrentRow) && chars > kAgentInjectChars)
    {
      note += tr("  Only the first %1 characters are sent with each request.").arg(kAgentInjectChars);
    }
    this->PathLabel->setText(note);
  }

  void onRowChanged(int row)
  {
    if (this->Block)
    {
      return;
    }
    if (row == this->CurrentRow)
    {
      return;
    }
    if (this->Dirty)
    {
      const auto answer = QMessageBox::question(this, tr("SHYX AI Memory"),
        tr("Save changes before switching?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
      if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !this->saveCurrent()))
      {
        this->Block = true;
        this->List->setCurrentRow(this->CurrentRow);
        this->Block = false;
        return;
      }
    }
    if (row >= 0)
    {
      this->loadRow(row);
    }
  }

  bool confirmSave()
  {
    if (!this->Dirty)
    {
      return true;
    }
    const auto answer = QMessageBox::question(this, tr("SHYX AI Memory"),
      tr("Save changes before closing?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
    {
      return false;
    }
    if (answer == QMessageBox::Discard)
    {
      this->Dirty = false;
      return true;
    }
    return this->saveCurrent();
  }

  bool saveCurrent()
  {
    if (this->CurrentRow < 0)
    {
      return false;
    }
    const QString text = this->Editor->toPlainText();
    if (text.size() > kUserFileChars)
    {
      QMessageBox::warning(this, tr("SHYX AI Memory"),
        tr("This file is over %1 characters.").arg(kUserFileChars));
      return false;
    }
    QString path;
    QString error;
    bool ok = false;
    if (this->isAgentRow(this->CurrentRow))
    {
      path = agentPath();
      ok = writeUtf8(path, text, &error);
    }
    else
    {
      const QString id = this->rowId(this->CurrentRow);
      path = skillFile(id);
      ok = idAllowed(id) && writeUtf8(path, text, &error);
      if (!idAllowed(id) && error.isEmpty())
      {
        error = tr("This skill name cannot be saved.");
      }
    }
    if (!ok)
    {
      QMessageBox::warning(this, tr("SHYX AI Memory"),
        error.isEmpty() ? tr("Could not save the file.") : error);
      return false;
    }
    this->Dirty = false;
    if (!this->isAgentRow(this->CurrentRow))
    {
      if (QListWidgetItem* item = this->List->item(this->CurrentRow))
      {
        item->setToolTip(parseFrontmatter(text).description);
      }
    }
    this->showPath(path, text.size());
    return true;
  }

  void newSkill()
  {
    if (!this->confirmSave())
    {
      return;
    }
    bool ok = false;
    const QString entered = QInputDialog::getText(this, tr("New skill"), tr("Skill name:"),
      QLineEdit::Normal, QString(), &ok)
                              .trimmed();
    if (!ok)
    {
      return;
    }
    const QString id = sanitizeId(entered);
    if (id.isEmpty())
    {
      QMessageBox::warning(this, tr("SHYX AI Memory"),
        tr("Use letters or numbers. '%1' cannot be a file name.").arg(entered));
      return;
    }
    if (QFileInfo::exists(skillFile(id)))
    {
      QMessageBox::information(this, tr("SHYX AI Memory"),
        tr("A skill named %1 already exists.").arg(id));
      this->rebuild(id);
      return;
    }
    const QString body =
      QStringLiteral("---\nname: %1\ndescription: When to use this skill.\n---\n\n# %2\n").arg(id, id);
    QString error;
    if (!writeUtf8(skillFile(id), body, &error))
    {
      QMessageBox::warning(this, tr("SHYX AI Memory"), error);
      return;
    }
    this->Dirty = false;
    this->rebuild(id);
  }

  void deleteCurrent()
  {
    if (this->CurrentRow < 0)
    {
      return;
    }
    if (this->isAgentRow(this->CurrentRow))
    {
      if (QMessageBox::question(this, tr("SHYX AI Memory"), tr("Clear Agent.md?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
      {
        return;
      }
      QString error;
      if (!writeUtf8(agentPath(), QString(), &error))
      {
        QMessageBox::warning(this, tr("SHYX AI Memory"), error);
        return;
      }
      this->Dirty = false;
      this->loadRow(this->CurrentRow);
      return;
    }
    const QString id = this->rowId(this->CurrentRow);
    if (QMessageBox::question(this, tr("SHYX AI Memory"), tr("Delete skill %1?").arg(id),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
      return;
    }
    QString error;
    if (!removeSkillDir(id, &error))
    {
      QMessageBox::warning(this, tr("SHYX AI Memory"), error);
      return;
    }
    this->Dirty = false;
    this->rebuild(QString());
  }

  void openFolder()
  {
    const QString root = memoryRoot();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(root)))
    {
      QMessageBox::warning(this, tr("SHYX AI Memory"),
        tr("Could not open %1.").arg(QDir::toNativeSeparators(root)));
    }
  }

  QListWidget* List = nullptr;
  QPlainTextEdit* Editor = nullptr;
  QLabel* PathLabel = nullptr;
  int CurrentRow = -1;
  bool Dirty = false;
  bool Block = false;
};
}

QString pqSHYXAIMemory::rootDirectory()
{
  return memoryRoot();
}

QString pqSHYXAIMemory::promptSection()
{
  QString agent = readUtf8(agentPath()).trimmed();
  if (agent.size() > kAgentInjectChars)
  {
    agent = agent.left(kAgentInjectChars).trimmed() +
      QStringLiteral("\n…(truncated; edit Agent.md in Memory)");
  }
  if (agent.isEmpty())
  {
    agent = QStringLiteral("(empty)");
  }
  return QStringLiteral(
           "\n## Agent memory\n"
           "Preferences that apply to every task. Do not put a pipeline procedure here.\n"
           "%1\n\n"
           "## Skill catalog\n"
           "When tools are available, call read_skill(name) before following a saved workflow. "
           "save_skill only when the user asks to remember a workflow; it never overwrites an "
           "existing name. update_skill only when the user asks to change an existing workflow: "
           "read it first, then pass the full revised body. It does not create or rename a skill. "
           "Omit description to keep the catalog line, or omit body to change only that line. "
           "write_agent_memory only when the user asks to remember a preference "
           "for every task, and the text argument is the full replacement for Agent.md.\n"
           "%2")
    .arg(agent, catalogText());
}

QString pqSHYXAIMemory::readSkill(const QString& name)
{
  Skill skill;
  if (!findSkill(name, &skill))
  {
    return QStringLiteral("No skill named '%1'.\nCatalog:\n%2").arg(name.trimmed(), catalogText());
  }
  QString text = readUtf8(skill.filePath);
  if (text.size() > kSkillBodyChars)
  {
    text = text.left(kSkillBodyChars) + QStringLiteral("\n…(truncated)");
  }
  return QStringLiteral("Skill id: %1\nFile: %2\n\n%3")
    .arg(skill.id, QDir::toNativeSeparators(skill.filePath), text);
}

QString pqSHYXAIMemory::saveSkill(
  const QString& name, const QString& description, const QString& body)
{
  const QString requested = sanitizeId(name);
  if (requested.isEmpty())
  {
    return QStringLiteral(
      "save_skill needs a name made of letters or numbers. '%1' cannot be a file name.")
      .arg(name.trimmed());
  }
  const QString desc = oneLine(description, kDescriptionChars);
  if (desc.isEmpty())
  {
    return QStringLiteral("save_skill needs a one-line description of when to use the skill.");
  }
  QString workflow = stripFrontmatter(body).trimmed();
  if (workflow.isEmpty())
  {
    return QStringLiteral("save_skill needs a markdown body.");
  }
  if (workflow.size() > kSkillBodyChars)
  {
    return QStringLiteral("save_skill refused: the body is %1 characters (max %2).")
      .arg(workflow.size())
      .arg(kSkillBodyChars);
  }
  QString error;
  const bool existed = QFileInfo::exists(skillFile(requested));
  const QString id = allocateId(requested, &error);
  if (id.isEmpty())
  {
    return error;
  }
  const QString file = QStringLiteral("---\nname: %1\ndescription: %2\n---\n\n%3\n").arg(id, desc, workflow);
  if (!writeUtf8(skillFile(id), file, &error))
  {
    return error.isEmpty() ? QStringLiteral("Could not write the skill file.") : error;
  }
  const QString native = QDir::toNativeSeparators(skillFile(id));
  if (existed)
  {
    return QStringLiteral(
             "'%1' already exists, so this was saved as '%2' without overwriting the original.\n"
             "File: %3")
      .arg(requested, id, native);
  }
  return QStringLiteral("Saved skill '%1'.\nFile: %2").arg(id, native);
}

QString pqSHYXAIMemory::updateSkill(
  const QString& name, const QString& description, const QString& body)
{
  Skill skill;
  if (!findSkill(name, &skill))
  {
    return QStringLiteral(
             "No skill named '%1'. update_skill only changes an existing skill. "
             "Use save_skill to create one.\nCatalog:\n%2")
      .arg(name.trimmed(), catalogText());
  }
  const bool hasDesc = !description.trimmed().isEmpty();
  QString workflow = stripFrontmatter(body).trimmed();
  const bool hasBody = !workflow.isEmpty();
  if (!hasDesc && !hasBody)
  {
    return QStringLiteral(
      "update_skill needs a new description, a new body, or both. "
      "Call read_skill first, then pass the full revised text.");
  }
  QString desc = skill.description;
  if (hasDesc)
  {
    desc = oneLine(description, kDescriptionChars);
    if (desc.isEmpty())
    {
      return QStringLiteral("update_skill needs a one-line description of when to use the skill.");
    }
  }
  if (hasBody)
  {
    if (workflow.size() > kSkillBodyChars)
    {
      return QStringLiteral("update_skill refused: the body is %1 characters (max %2).")
        .arg(workflow.size())
        .arg(kSkillBodyChars);
    }
  }
  else
  {
    workflow = stripFrontmatter(readUtf8(skill.filePath)).trimmed();
  }
  QString frontName = oneLine(skill.name.isEmpty() ? skill.id : skill.name, 80);
  if (frontName.isEmpty())
  {
    frontName = skill.id;
  }
  const QString file =
    QStringLiteral("---\nname: %1\ndescription: %2\n---\n\n%3\n").arg(frontName, desc, workflow);
  QString error;
  if (!writeUtf8(skill.filePath, file, &error))
  {
    return error.isEmpty() ? QStringLiteral("Could not write the skill file.") : error;
  }
  return QStringLiteral("Updated skill '%1'.\nFile: %2")
    .arg(skill.id, QDir::toNativeSeparators(skill.filePath));
}

QString pqSHYXAIMemory::writeAgentMemory(const QString& text)
{
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty())
  {
    return QStringLiteral(
      "write_agent_memory requires text. To clear Agent.md, use the Memory button.");
  }
  if (trimmed.size() > kAgentWriteChars)
  {
    return QStringLiteral(
             "write_agent_memory refused: %1 characters exceeds %2. Shorten it, or put a "
             "workflow in save_skill.")
      .arg(trimmed.size())
      .arg(kAgentWriteChars);
  }
  QString error;
  if (!writeUtf8(agentPath(), trimmed + QLatin1Char('\n'), &error))
  {
    return error.isEmpty() ? QStringLiteral("Could not write Agent.md.") : error;
  }
  return QStringLiteral("Agent.md updated (%1 characters).\nFile: %2")
    .arg(trimmed.size())
    .arg(QDir::toNativeSeparators(agentPath()));
}

void pqSHYXAIMemory::openEditor(QWidget* parent)
{
  MemoryDialog dialog(parent);
  dialog.exec();
}
