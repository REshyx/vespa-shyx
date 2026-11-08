#ifndef pqSHYXQuickFilterWheelConfigDialog_h
#define pqSHYXQuickFilterWheelConfigDialog_h

#include <QDialog>
#include <QSet>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

/**
 * Checklist opened by the quick-filter wheel's "+" sector.
 *
 * Shows the filters that can be placed on the wheel (the SHYX filters and the
 * ParaView / VESPA built-in filters registered in this build) with a check
 * state, grouped by section title, plus a search field that hides non-matching
 * rows. The caller owns persistence; this dialog only reports the checked ids
 * in catalog order.
 */
class pqSHYXQuickFilterWheelConfigDialog : public QDialog
{
  Q_OBJECT
  typedef QDialog Superclass;

public:
  /// \a groups holds the section title per row; a title change inserts a header.
  pqSHYXQuickFilterWheelConfigDialog(const QStringList& ids, const QStringList& labels,
    const QStringList& groups, const QSet<QString>& checkedIds, QWidget* parent = nullptr);
  ~pqSHYXQuickFilterWheelConfigDialog() override;

  /// Checked ids in the order the catalog was supplied.
  QStringList checkedIds() const;

private:
  /// True for the non-checkable section title rows.
  static bool isHeader(const QListWidgetItem* item);

  void applySearchText(const QString& text);
  void setVisibleItemsChecked(bool checked);
  /// Hide a header when every row below it (up to the next header) is hidden.
  void updateHeaderVisibility();

  QLineEdit* Search = nullptr;
  QListWidget* List = nullptr;

  Q_DISABLE_COPY(pqSHYXQuickFilterWheelConfigDialog)
};

#endif
