#include "pqSHYXQuickFilterWheelConfigDialog.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
/// Marks the non-checkable section title rows.
constexpr int kIsHeaderRole = Qt::UserRole + 1;
}

//-----------------------------------------------------------------------------
pqSHYXQuickFilterWheelConfigDialog::pqSHYXQuickFilterWheelConfigDialog(
  const QStringList& ids, const QStringList& labels, const QStringList& groups,
  const QSet<QString>& checkedIds, QWidget* parent)
  : Superclass(parent)
{
  this->setWindowTitle(tr("Quick filter wheel"));
  this->resize(420, 560);

  auto* layout = new QVBoxLayout(this);

  auto* hint = new QLabel(
    tr("Check the filters to put on the wheel: SHYX filters (including SHYX Delete Selected "
       "Cells) and ParaView / VESPA built-in filters (including Extract Surface) that can run on "
       "the active object. Nothing is checked by default; the wheel always keeps a \"+\" sector "
       "that reopens this list."),
    this);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  this->Search = new QLineEdit(this);
  this->Search->setPlaceholderText(tr("Search actions..."));
  this->Search->setClearButtonEnabled(true);
  layout->addWidget(this->Search);

  this->List = new QListWidget(this);
  this->List->setSelectionMode(QAbstractItemView::SingleSelection);
  this->List->setUniformItemSizes(true);
  layout->addWidget(this->List, 1);

  auto* quickButtons = new QHBoxLayout;
  auto* checkAll = new QPushButton(tr("All"), this);
  auto* checkNone = new QPushButton(tr("None"), this);
  checkAll->setToolTip(tr("Check every action currently visible in the list."));
  checkNone->setToolTip(tr("Uncheck every action currently visible in the list."));
  quickButtons->addWidget(checkAll);
  quickButtons->addWidget(checkNone);
  quickButtons->addStretch(1);
  layout->addLayout(quickButtons);

  auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttonBox);

  QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  QObject::connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  QObject::connect(this->Search, &QLineEdit::textChanged, this,
    &pqSHYXQuickFilterWheelConfigDialog::applySearchText);
  QObject::connect(checkAll, &QPushButton::clicked, this,
    [this]() { this->setVisibleItemsChecked(true); });
  QObject::connect(checkNone, &QPushButton::clicked, this,
    [this]() { this->setVisibleItemsChecked(false); });

  QString currentGroup;
  for (int i = 0; i < ids.size(); ++i)
  {
    const QString group = groups.value(i);
    if (!group.isEmpty() && group != currentGroup)
    {
      currentGroup = group;
      auto* header = new QListWidgetItem(group, this->List);
      header->setFlags(Qt::NoItemFlags);
      header->setData(kIsHeaderRole, true);
      QFont headerFont = header->font();
      headerFont.setBold(true);
      header->setFont(headerFont);
      header->setForeground(QColor(130, 134, 142));
      header->setBackground(QColor(244, 245, 247));
    }

    auto* item = new QListWidgetItem(labels.value(i, ids.at(i)), this->List);
    item->setData(Qt::UserRole, ids.at(i));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
    item->setCheckState(checkedIds.contains(ids.at(i)) ? Qt::Checked : Qt::Unchecked);
  }

  this->Search->setFocus();
}

//-----------------------------------------------------------------------------
pqSHYXQuickFilterWheelConfigDialog::~pqSHYXQuickFilterWheelConfigDialog() = default;

//-----------------------------------------------------------------------------
QStringList pqSHYXQuickFilterWheelConfigDialog::checkedIds() const
{
  QStringList ids;
  for (int i = 0; i < this->List->count(); ++i)
  {
    const QListWidgetItem* item = this->List->item(i);
    if (item && !isHeader(item) && item->checkState() == Qt::Checked)
    {
      ids.append(item->data(Qt::UserRole).toString());
    }
  }
  return ids;
}

//-----------------------------------------------------------------------------
bool pqSHYXQuickFilterWheelConfigDialog::isHeader(const QListWidgetItem* item)
{
  return item && item->data(kIsHeaderRole).toBool();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheelConfigDialog::applySearchText(const QString& text)
{
  const QString needle = text.trimmed();
  for (int i = 0; i < this->List->count(); ++i)
  {
    QListWidgetItem* item = this->List->item(i);
    if (!item || isHeader(item))
    {
      continue;
    }
    item->setHidden(!needle.isEmpty() && !item->text().contains(needle, Qt::CaseInsensitive));
  }
  this->updateHeaderVisibility();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheelConfigDialog::updateHeaderVisibility()
{
  // A header stays only while at least one of its rows is visible.
  int headerRow = -1;
  bool groupHasVisibleRow = false;
  for (int i = 0; i < this->List->count(); ++i)
  {
    QListWidgetItem* item = this->List->item(i);
    if (!item)
    {
      continue;
    }
    if (isHeader(item))
    {
      if (headerRow >= 0 && this->List->item(headerRow))
      {
        this->List->item(headerRow)->setHidden(!groupHasVisibleRow);
      }
      headerRow = i;
      groupHasVisibleRow = false;
      continue;
    }
    if (!item->isHidden())
    {
      groupHasVisibleRow = true;
    }
  }
  if (headerRow >= 0 && this->List->item(headerRow))
  {
    this->List->item(headerRow)->setHidden(!groupHasVisibleRow);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheelConfigDialog::setVisibleItemsChecked(bool checked)
{
  const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
  for (int i = 0; i < this->List->count(); ++i)
  {
    QListWidgetItem* item = this->List->item(i);
    if (item && !isHeader(item) && !item->isHidden())
    {
      item->setCheckState(state);
    }
  }
}
