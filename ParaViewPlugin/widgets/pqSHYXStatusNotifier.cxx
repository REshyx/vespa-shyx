#include "pqSHYXStatusNotifier.h"

#include "pqCoreUtilities.h"

#include <QFont>
#include <QFontMetrics>
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
constexpr int kCollapseMs = 8000;
constexpr int kHistoryLimit = 30;
constexpr int kChipMinWidth = 72;
constexpr int kChipMaxWidth = 560;
constexpr int kProgressReserve = 380;
constexpr int kPopupLabelWidth = 480;

QPointer<pqSHYXStatusNotifier>& notifierInstance()
{
  static QPointer<pqSHYXStatusNotifier> inst;
  return inst;
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
void pqSHYXStatusNotifier::show(const QString& message)
{
  instance()->showMessage(message);
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
    this->Button->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    this->Button->setText(QStringLiteral("SHYX"));
    this->Button->setToolTip(
      tr("SHYX notices. Click to show recent messages; click elsewhere to close."));

    this->Menu = new QMenu(this->Button);
    this->Menu->setObjectName(QStringLiteral("SHYXStatusMenu"));
    this->Button->setMenu(this->Menu);
    // Leftmost permanent widget: immediately left of pqProgressWidget.
    bar->insertPermanentWidget(0, this->Button, 0);
  }

  if (this->Menu)
  {
    QObject::connect(this->Menu, &QMenu::aboutToShow, this, &pqSHYXStatusNotifier::rebuildMenu,
      Qt::UniqueConnection);
  }
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::showMessage(const QString& message)
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
  if (this->History.isEmpty() || this->History.front() != trimmed)
  {
    this->History.prepend(trimmed);
    while (this->History.size() > kHistoryLimit)
    {
      this->History.removeLast();
    }
  }

  this->applyChipText(trimmed, /*expanded=*/true);
  this->CollapseTimer->start(kCollapseMs);
}

//-----------------------------------------------------------------------------
void pqSHYXStatusNotifier::applyChipText(const QString& text, bool expanded)
{
  if (!this->Button)
  {
    return;
  }

  if (!expanded)
  {
    this->Button->setMaximumWidth(QWIDGETSIZE_MAX);
    this->Button->setText(QStringLiteral("SHYX"));
    this->Button->setToolTip(this->LastMessage.isEmpty()
        ? tr("SHYX notices. Click to show recent messages.")
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

  auto* title = new QLabel(tr("Recent SHYX notices"), wrap);
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
    for (const QString& line : this->History)
    {
      auto* item = new QLabel(line, wrap);
      item->setWordWrap(true);
      item->setTextInteractionFlags(Qt::TextSelectableByMouse);
      item->setMaximumWidth(kPopupLabelWidth);
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
  this->applyChipText(QStringLiteral("SHYX"), /*expanded=*/false);
}
