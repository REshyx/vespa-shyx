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
 * Optional Case Directory (leave empty for %TEMP%) plus a button that opens the
 * folder actually written (CaseFoamPath after Apply, else Case Directory).
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
  void browseFolder();
  void refreshOpenButton();
  void openFolder();

private:
  QString resolvedFolder() const;

  QPointer<pqPipelineSource> PipelineSource;
  QLineEdit* PathEdit = nullptr;
  QPushButton* BrowseButton = nullptr;
  QPushButton* OpenButton = nullptr;
  vtkSMStringVectorProperty* DirProp = nullptr;
  vtkSMStringVectorProperty* PathProp = nullptr;
};

#endif
