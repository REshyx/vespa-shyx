#ifndef pqSHYXSnappyCaseFolderWidget_h
#define pqSHYXSnappyCaseFolderWidget_h

#include "pqPropertyWidget.h"

#include <QPointer>

class QLineEdit;
class QPushButton;
class pqPipelineSource;
class vtkSMPropertyGroup;
class vtkSMProxy;
class vtkSMStringVectorProperty;

/**
 * One-line read-only case folder plus a button that opens it in the file manager.
 * Re-pulls CaseFoamPath after dataUpdated so the path matches this Apply.
 */
class pqSHYXSnappyCaseFolderWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXSnappyCaseFolderWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXSnappyCaseFolderWidget() override;

  void setReadOnly(bool readOnly) override;

private Q_SLOTS:
  void refreshPath();
  void openFolder();

private:
  QString folderPath() const;

  QPointer<pqPipelineSource> PipelineSource;
  QLineEdit* PathEdit = nullptr;
  QPushButton* OpenButton = nullptr;
  vtkSMStringVectorProperty* PathProp = nullptr;
};

#endif
