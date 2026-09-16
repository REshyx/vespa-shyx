#include "pqSHYXStatusNotifier.h"

#include "pqCoreUtilities.h"

#include <QFont>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QtGlobal>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

namespace
{
constexpr char kCollapsedLabel[] = "SHYX Information";
constexpr int kCollapseMs = 8000;
constexpr int kHistoryLimit = 30;
constexpr int kChipMinWidth = 140;
constexpr int kChipMaxWidth = 560;
constexpr int kProgressReserve = 380;
constexpr int kPopupLabelWidth = 480;

constexpr char kBaseStyle[] =
  "QToolButton { text-align: left; padding-left: 6px; padding-right: 8px; "
  "border-radius: 3px; }"
  "QToolButton::menu-indicator { image: none; }";

QPointer<pqSHYXStatusNotifier>& notifierInstance()
{
  static QPointer<pqSHYXStatusNotifier> inst;
  return inst;
}
}

//-----------------------------------------------------------------------------
QString pqSHYXStatusNotifier::levelColor(Level level)
{
  const bool dark = pqCoreUtilities::isDarkTheme();
  switch (level)
  {
    case Level::Warning:
      return dark ? QStringLiteral("#e3b341") : QStringLiteral("#9a6700");
    case Level::Error:
      return dark ? QStringLiteral("#ff7b72") : QStringLiteral("#cf222e");
    case Level::Info:
    default:
      return dark ? QStringLiteral("#3fb950") : QStringLiteral("#1a7f37");
  }
}

//-----------------------------------------------------------------------------
pqSHYXStatusNotifier* pqSHYXStatusNotifier::instance()
{
  auto& inst = notifierInstance();
  if (!inst)
  {
    QWidget* parent = pqCoreUtilities::mainWidget();
    inst = new pqSHYXStatusNotifier(parent);
  }
  return inst;
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::info(const QString& message)
{
  show(message, Level::Info);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::warning(const QString& message)
{
  show(message, Level::Warning);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::error(const QString& message)
{
  show(message, Level::Error);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::show(const QString& message, Level level)
{
  instance()->showMessage(message, level);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::shutdown()
{
  auto& inst = notifierInstance();
  delete inst.data();
}

//-----------------------------------------------------------------------------
pqSHYXStatusNotifier::pqSHYXStatusNotifier(QObject* parent)
  : Superclass(parent)
{
  this->CollapseTimer = new QTimer(this);
  this->CollapseTimer->setSingleShot(true);
  QObject::connect(
    this->CollapseTimer, &QTimer::timeout, this, &pqSHYXStatusNotifier::collapseChip);
}

//-----------------------------------------------------------------------------
pqSHYXStatusNotifier::~pqSHYXStatusNotifier()
{
  if (this->StatusBar && this->Button)
  {
    this->StatusBar->removeWidget(this->Button);
    delete this->Button;
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXStatusNotifier::ensureInstalled()
{
  if (this->Button && this->StatusBar)
  {
    return true;
  }

  auto* main = qobject_cast<QMainWindow*>(pqCoreUtilities::mainWidget());
  if (!main)
  {
    return false;
  }
  QStatusBar* bar = main->statusBar();
  if (!bar)
  {
    return false;
  }

  this->StatusBar = bar;
  if (QToolButton* existing = bar->findChild<QToolButton*>(QStringLiteral("SHYXStatusChip")))
  {
    this->Button = existing;
    this->Menu = existing->menu();
  }
  else
  {
    this->Button = new QToolButton(bar);
    this->Button->setObjectName(QStringLiteral("SHYXStatusChip"));
    this->Button->setAutoRaise(true);
    this->Button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    this->Button->setPopupMode(QToolButton::InstantPopup);
    this->Button->setFocusPolicy(Qt::NoFocus);
    this->Button->setCursor(Qt::PointingHandCursor);
    this->Button->setText(QString::fromUtf8(kCollapsedLabel));
    this->Button->setToolTip(
      tr("SHYX Information. Click to show recent messages; click elsewhere to close."));
    this->applyChipStyle(/*colored=*/false);

    this->Menu = new QMenu(this->Button);
    this->Menu->setObjectName(QStringLiteral("SHYXStatusMenu"));
    this->Button->setMenu(this->Menu);
    // Non-permanent: left side of the status bar (permanent widgets sit on the right).
    bar->insertWidget(0, this->Button, 0);
    this->Button->show();
  }

  if (this->Menu)
  {
    QObject::connect(this->Menu, &QMenu::aboutToShow, this, &pqSHYXStatusNotifier::rebuildMenu,
      Qt::UniqueConnection);
  }
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::showMessage(const QString& message, Level level)
{
  const QString trimmed = message.trimmed();
  if (trimmed.isEmpty())
  {
    return;
  }

  if (!this->ensureInstalled())
  {
    if (auto* main = qobject_cast<QMainWindow*>(pqCoreUtilities::mainWidget()))
    {
      if (QStatusBar* bar = main->statusBar())
      {
        bar->showMessage(trimmed, kCollapseMs);
      }
    }
    return;
  }

  this->LastMessage = trimmed;
  this->LastLevel = level;
  if (this->History.isEmpty() || this->History.front().text != trimmed)
  {
    this->History.prepend({ trimmed, level });
    while (this->History.size() > kHistoryLimit)
    {
      this->History.removeLast();
    }
  }

  this->applyChipText(trimmed, /*expanded=*/true);
  this->CollapseTimer->start(kCollapseMs);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::applyChipStyle(bool colored)
{
  if (!this->Button)
  {
    return;
  }
  if (!colored)
  {
    this->Button->setStyleSheet(QString::fromUtf8(kBaseStyle));
    return;
  }

  const bool dark = pqCoreUtilities::isDarkTheme();
  QString bg;
  switch (this->LastLevel)
  {
    case Level::Warning:
      bg = dark ? QStringLiteral("rgba(227, 179, 65, 0.22)")
                : QStringLiteral("rgba(154, 103, 0, 0.16)");
      break;
    case Level::Error:
      bg = dark ? QStringLiteral("rgba(255, 123, 114, 0.22)")
                : QStringLiteral("rgba(207, 34, 46, 0.14)");
      break;
    case Level::Info:
    default:
      bg = dark ? QStringLiteral("rgba(63, 185, 80, 0.22)")
                : QStringLiteral("rgba(26, 127, 55, 0.14)");
      break;
  }
  const QString color = levelColor(this->LastLevel);
  this->Button->setStyleSheet(QStringLiteral(
    "QToolButton { text-align: left; padding-left: 6px; padding-right: 8px; "
    "border-radius: 3px; color: %1; background: %2; font-weight: 600; }"
    "QToolButton::menu-indicator { image: none; }")
                                .arg(color, bg));
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::applyChipText(const QString& text, bool expanded)
{
  if (!this->Button)
  {
    return;
  }
  this->Button->show();
  this->applyChipStyle(/*colored=*/expanded);

  if (!expanded)
  {
    this->Button->setMaximumWidth(QWIDGETSIZE_MAX);
    this->Button->setText(text.isEmpty() ? QString::fromUtf8(kCollapsedLabel) : text);
    this->Button->setToolTip(this->LastMessage.isEmpty()
        ? tr("SHYX Information. Click to show recent messages.")
        : this->LastMessage);
    this->Button->updateGeometry();
    return;
  }

  int maxW = kChipMaxWidth;
  if (this->StatusBar)
  {
    maxW = qBound(kChipMinWidth, this->StatusBar->width() - kProgressReserve, kChipMaxWidth);
  }
  this->Button->setMaximumWidth(maxW);
  const int inner = qMax(kChipMinWidth - 24, maxW - 28);
  const QString elided = this->Button->fontMetrics().elidedText(text, Qt::ElideRight, inner);
  this->Button->setText(elided);
  this->Button->setToolTip(text);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::rebuildMenu()
{
  if (!this->Menu)
  {
    return;
  }
  this->Menu->clear();

  auto* wrap = new QWidget(this->Menu);
  auto* layout = new QVBoxLayout(wrap);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(6);

  auto* title = new QLabel(tr("SHYX Information"), wrap);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  title->setFont(titleFont);
  layout->addWidget(title);

  if (this->History.isEmpty())
  {
    auto* empty = new QLabel(tr("No notices yet."), wrap);
    empty->setMaximumWidth(kPopupLabelWidth);
    layout->addWidget(empty);
  }
  else
  {
    for (const HistoryItem& entry : this->History)
    {
      auto* item = new QLabel(entry.text, wrap);
      item->setWordWrap(true);
      item->setTextInteractionFlags(Qt::TextSelectableByMouse);
      item->setMaximumWidth(kPopupLabelWidth);
      item->setStyleSheet(
        QStringLiteral("color: %1;").arg(levelColor(entry.level)));
      layout->addWidget(item);
    }
  }

  auto* act = new QWidgetAction(this->Menu);
  act->setDefaultWidget(wrap);
  this->Menu->addAction(act);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::collapseChip()
{
  this->applyChipText(QString::fromUtf8(kCollapsedLabel), /*expanded=*/false);
}
