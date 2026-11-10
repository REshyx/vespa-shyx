#ifndef pqSHYXAIMemory_h
#define pqSHYXAIMemory_h

#include <QString>

class QWidget;

/**
 * On-disk memory for SHYX AI Assistant. Not QSettings and not the registry.
 * Windows: %APPDATA%/SHYX/AIAssistant/memory/Agent.md
 * and memory/skills/<id>/SKILL.md.
 * Agent.md is injected into every request. Skill files are listed by id and
 * description; the body is loaded with read_skill. save_skill creates a file;
 * update_skill overwrites an existing one.
 */
namespace pqSHYXAIMemory
{
QString rootDirectory();
QString promptSection();
QString readSkill(const QString& name);
QString saveSkill(const QString& name, const QString& description, const QString& body);
QString updateSkill(const QString& name, const QString& description, const QString& body);
QString writeAgentMemory(const QString& text);
void openEditor(QWidget* parent);
}

#endif
