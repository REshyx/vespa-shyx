#include "pqSHYXSnappyCastellatedWidget.h"

#include "pqSHYXSnappyPatchTableWidget.h"

#include "vtkSMIntRangeDomain.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

pqSHYXSnappyCastellatedWidget::pqSHYXSnappyCastellatedWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup*, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(8);

  auto* castellatedProp =
    vtkSMIntVectorProperty::SafeDownCast(smproxy ? smproxy->GetProperty("CastellatedMesh") : nullptr);
  auto* check = new QCheckBox(
    QString::fromUtf8(castellatedProp && castellatedProp->GetXMLLabel()
        ? castellatedProp->GetXMLLabel()
        : "Castellated mesh"),
    this);
  check->setObjectName(QStringLiteral("CastellatedMesh"));
  vbox->addWidget(check);
  if (castellatedProp)
  {
    this->addPropertyLink(check, "checked", SIGNAL(toggled(bool)), castellatedProp);
  }

  auto* dependent = new QWidget(this);
  auto* depLayout = new QVBoxLayout(dependent);
  depLayout->setContentsMargins(0, 0, 0, 0);
  depLayout->setSpacing(8);

  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(4);

  auto addIntSpin = [this, smproxy, dependent, form](const char* name) {
    auto* prop = vtkSMIntVectorProperty::SafeDownCast(smproxy ? smproxy->GetProperty(name) : nullptr);
    if (!prop)
    {
      return;
    }
    auto* spin = new QSpinBox(dependent);
    spin->setObjectName(QString::fromLatin1(name));
    int lo = 0;
    int hi = 2000000000;
    if (auto* domain = vtkSMIntRangeDomain::SafeDownCast(prop->GetDomain("range")))
    {
      lo = domain->GetMinimum(0);
      hi = domain->GetMaximum(0);
      if (hi < lo)
      {
        hi = lo;
      }
    }
    spin->setRange(lo, hi);
    const char* label = prop->GetXMLLabel();
    form->addRow(QString::fromUtf8(label ? label : name), spin);
    this->addPropertyLink(spin, "value", SIGNAL(valueChanged(int)), prop);
  };
  addIntSpin("MaxGlobalCells");
  addIntSpin("NCellsBetweenLevels");
  addIntSpin("RefinementMin");
  addIntSpin("RefinementMax");
  depLayout->addLayout(form);

  auto* surfaceBox = new QGroupBox(tr("Surface patches"), dependent);
  auto* surfaceLay = new QVBoxLayout(surfaceBox);
  surfaceLay->setContentsMargins(4, 4, 4, 4);
  this->SurfacesTable = new pqSHYXSnappyPatchTableWidget(
    smproxy, pqSHYXSnappyPatchTableWidget::Surfaces, surfaceBox);
  this->SurfacesTable->setObjectName(QStringLiteral("SHYXSnappySurfacePatches"));
  surfaceLay->addWidget(this->SurfacesTable);
  depLayout->addWidget(surfaceBox);

  auto* regionBox = new QGroupBox(tr("Region patches"), dependent);
  auto* regionLay = new QVBoxLayout(regionBox);
  regionLay->setContentsMargins(4, 4, 4, 4);
  this->RegionsTable = new pqSHYXSnappyPatchTableWidget(
    smproxy, pqSHYXSnappyPatchTableWidget::Regions, regionBox);
  this->RegionsTable->setObjectName(QStringLiteral("SHYXSnappyRegionPatches"));
  regionLay->addWidget(this->RegionsTable);
  depLayout->addWidget(regionBox);

  vbox->addWidget(dependent);

  QObject::connect(check, &QCheckBox::toggled, dependent, &QWidget::setEnabled);
  dependent->setEnabled(check->isChecked());

  // Nested tables are not registered with pqProxyWidget. Forward both signals:
  // pqPropertiesPanel ignores changeFinished unless changeAvailable was seen first,
  // so Apply stays disabled if only changeFinished is re-emitted.
  auto forwardChanges = [this](pqPropertyWidget* table) {
    QObject::connect(table, &pqPropertyWidget::changeAvailable, this,
      &pqPropertyWidget::changeAvailable);
    QObject::connect(table, &pqPropertyWidget::changeFinished, this,
      &pqPropertyWidget::changeFinished);
  };
  forwardChanges(this->SurfacesTable);
  forwardChanges(this->RegionsTable);

  this->setChangeAvailableAsChangeFinished(true);
}

pqSHYXSnappyCastellatedWidget::~pqSHYXSnappyCastellatedWidget() = default;

void pqSHYXSnappyCastellatedWidget::apply()
{
  if (this->SurfacesTable)
  {
    this->SurfacesTable->apply();
  }
  if (this->RegionsTable)
  {
    this->RegionsTable->apply();
  }
  this->Superclass::apply();
}

void pqSHYXSnappyCastellatedWidget::reset()
{
  this->Superclass::reset();
  if (this->SurfacesTable)
  {
    this->SurfacesTable->reset();
  }
  if (this->RegionsTable)
  {
    this->RegionsTable->reset();
  }
}
