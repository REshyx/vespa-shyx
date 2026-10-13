#ifndef pqSHYXDeleteSelectionShortcut_h
#define pqSHYXDeleteSelectionShortcut_h

#include <QObject>

/**
 * RenderView shortcut: Ctrl+D creates SHYX Delete Selected Cells on the dataset
 * that owns the active selection and Applies it.
 *
 * Plain D is left for ParaView point selection. The shortcut is ignored while
 * focus is outside a RenderView.
 */
class pqSHYXDeleteSelectionShortcut : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  explicit pqSHYXDeleteSelectionShortcut(QObject* parent = nullptr);
  ~pqSHYXDeleteSelectionShortcut() override;

  void onStartup();
  void onShutdown();

  /// Create SHYXDeleteSelectedCellsFilter on the selected port and Apply.
  static void deleteActiveSelection();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  static bool renderViewHasFocus();

  Q_DISABLE_COPY(pqSHYXDeleteSelectionShortcut)

  bool Installed = false;
};

#endif
