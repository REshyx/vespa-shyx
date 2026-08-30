#include "pqSHYXSelectionPatchTableWidget.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqDataAssemblyTreeModel.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqSelectionManager.h"
#include "pqServerManagerModel.h"
#include "pqTreeView.h"
#include "pqUndoStack.h"
#include "pqView.h"

#include "vtkAbstractArray.h"
#include "vtkAlgorithm.h"
#include "vtkBoxRepresentation.h"
#include "vtkBoxWidget2.h"
#include "vtkCallbackCommand.h"
#include "vtkCellData.h"
#include "vtkCommand.h"
#include "vtkConvertSelection.h"
#include "vtkDataArray.h"
#include "vtkDataAssembly.h"
#include "vtkDataSet.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkIdTypeArray.h"
#include "vtkInteractorObserver.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPVArrayInformation.h"
#include "vtkPVDataInformation.h"
#include "vtkPVDataSetAttributesInformation.h"
#include "vtkProperty.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkRenderer.h"
#include "vtkSMInputProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"
#include "vtkSMTrace.h"
#include "vtkSelection.h"
#include "vtkSmartPointer.h"
#include "vtkSphereRepresentation.h"
#include "vtkSphereWidget2.h"
#include "vtkStringArray.h"
#include "vtkTransform.h"
#include "vtkVariant.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSet>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// VTK's default right-click Scale uses 3D distance to the interior center. The pick plane
// sits on a face, so many screen drags look like "toward center" and the box only shrinks.
// Use display-space distance to the box instead: drag away to grow, toward it to shrink.
class vtkSHYXPatchBoxRepresentation : public vtkBoxRepresentation
{
public:
  static vtkSHYXPatchBoxRepresentation* New();
  vtkTypeMacro(vtkSHYXPatchBoxRepresentation, vtkBoxRepresentation);

protected:
  vtkSHYXPatchBoxRepresentation() = default;
  ~vtkSHYXPatchBoxRepresentation() override = default;
  void Scale(const double* p1, const double* p2, int X, int Y) override;

private:
  vtkSHYXPatchBoxRepresentation(const vtkSHYXPatchBoxRepresentation&) = delete;
  void operator=(const vtkSHYXPatchBoxRepresentation&) = delete;
};

vtkStandardNewMacro(vtkSHYXPatchBoxRepresentation);

void vtkSHYXPatchBoxRepresentation::Scale(const double*, const double*, int X, int Y)
{
  if (!this->Renderer || !this->Points)
  {
    return;
  }

  double center[3] = { 0.0, 0.0, 0.0 };
  this->Points->GetPoint(14, center);
  double dc[4] = { 0.0, 0.0, 0.0, 0.0 };
  vtkInteractorObserver::ComputeWorldToDisplay(
    this->Renderer, center[0], center[1], center[2], dc);

  const double dx1 = this->LastEventPosition[0] - dc[0];
  const double dy1 = this->LastEventPosition[1] - dc[1];
  const double dx2 = static_cast<double>(X) - dc[0];
  const double dy2 = static_cast<double>(Y) - dc[1];
  const double d1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
  const double d2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
  if (d1 < 4.0)
  {
    return;
  }

  double sf = d2 / d1;
  if (sf < 0.5)
  {
    sf = 0.5;
  }
  else if (sf > 1.5)
  {
    sf = 1.5;
  }

  for (int i = 0; i < 8; ++i)
  {
    double p[3];
    this->Points->GetPoint(i, p);
    p[0] = sf * (p[0] - center[0]) + center[0];
    p[1] = sf * (p[1] - center[1]) + center[1];
    p[2] = sf * (p[2] - center[2]) + center[2];
    this->Points->SetPoint(i, p);
  }
  this->PositionHandles();
}

namespace
{
constexpr int kColVisibility = 0;
constexpr int kColIndex = 1;
constexpr int kColName = 2;
constexpr int kColType = 3;
constexpr int kColInfo = 4;
constexpr int kRoleCellIds = Qt::UserRole;
constexpr int kRoleKind = Qt::UserRole + 1;
constexpr int kRoleParams = Qt::UserRole + 2;
constexpr int kSelectorPathRole = Qt::UserRole;
constexpr int kDataSetIndexRole = Qt::UserRole + 1;
constexpr int kVisibleRole = Qt::UserRole + 2;

vtkSMProperty* propertyFromGroup(
  vtkSMPropertyGroup* group, vtkSMProxy* proxy, const char* function, const char* fallbackName)
{
  if (group)
  {
    if (auto* p = group->GetProperty(function))
    {
      return p;
    }
  }
  return proxy ? proxy->GetProperty(fallbackName) : nullptr;
}

QString compactIdList(const std::vector<vtkIdType>& ids)
{
  if (ids.empty())
  {
    return QString();
  }
  std::vector<vtkIdType> sorted = ids;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  QStringList parts;
  vtkIdType runStart = sorted.front();
  vtkIdType runEnd = runStart;
  for (size_t i = 1; i < sorted.size(); ++i)
  {
    if (sorted[i] == runEnd + 1)
    {
      runEnd = sorted[i];
      continue;
    }
    if (runStart == runEnd)
    {
      parts << QString::number(static_cast<qint64>(runStart));
    }
    else
    {
      parts << QStringLiteral("%1-%2")
                 .arg(static_cast<qint64>(runStart))
                 .arg(static_cast<qint64>(runEnd));
    }
    runStart = runEnd = sorted[i];
  }
  if (runStart == runEnd)
  {
    parts << QString::number(static_cast<qint64>(runStart));
  }
  else
  {
    parts << QStringLiteral("%1-%2")
               .arg(static_cast<qint64>(runStart))
               .arg(static_cast<qint64>(runEnd));
  }
  return parts.join(QLatin1Char(','));
}

std::vector<vtkIdType> parseCompactIds(const QString& text)
{
  std::vector<vtkIdType> ids;
  const QStringList tokens = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
  for (QString token : tokens)
  {
    token = token.trimmed();
    const int dash = token.indexOf(QLatin1Char('-'));
    if (dash > 0 && dash + 1 < token.size())
    {
      bool okA = false;
      bool okB = false;
      const qint64 a = token.left(dash).toLongLong(&okA);
      const qint64 b = token.mid(dash + 1).toLongLong(&okB);
      if (okA && okB)
      {
        const qint64 lo = a < b ? a : b;
        const qint64 hi = a < b ? b : a;
        for (qint64 v = lo; v <= hi; ++v)
        {
          ids.push_back(static_cast<vtkIdType>(v));
        }
        continue;
      }
    }
    bool ok = false;
    const qint64 v = token.toLongLong(&ok);
    if (ok)
    {
      ids.push_back(static_cast<vtkIdType>(v));
    }
  }
  return ids;
}

int countCompactIds(const QString& text)
{
  return static_cast<int>(parseCompactIds(text).size());
}

QString formatDoubles(const double* v, int n)
{
  QStringList parts;
  parts.reserve(n);
  for (int i = 0; i < n; ++i)
  {
    parts << QString::number(v[i], 'g', 16);
  }
  return parts.join(QLatin1Char(','));
}

bool parseDoubles(const QString& text, double* v, int n)
{
  const QStringList tokens = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
  if (tokens.size() < n)
  {
    return false;
  }
  for (int i = 0; i < n; ++i)
  {
    bool ok = false;
    v[i] = tokens[i].trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(v[i]))
    {
      return false;
    }
  }
  return true;
}

void unitBoxBounds(double b[6])
{
  b[0] = b[2] = b[4] = -0.5;
  b[1] = b[3] = b[5] = 0.5;
}

void aabbToUnitCubeMatrix(const double b[6], double m[16])
{
  vtkNew<vtkMatrix4x4> mat;
  mat->Identity();
  mat->SetElement(0, 0, b[1] - b[0]);
  mat->SetElement(1, 1, b[3] - b[2]);
  mat->SetElement(2, 2, b[5] - b[4]);
  mat->SetElement(0, 3, 0.5 * (b[0] + b[1]));
  mat->SetElement(1, 3, 0.5 * (b[2] + b[3]));
  mat->SetElement(2, 3, 0.5 * (b[4] + b[5]));
  vtkMatrix4x4::DeepCopy(m, mat);
}

QString paramsFromBoxRepresentation(vtkBoxRepresentation* repr)
{
  if (!repr)
  {
    return QString();
  }
  vtkNew<vtkTransform> t;
  repr->GetTransform(t);
  t->Update();
  double m[16] = { 0 };
  vtkMatrix4x4::DeepCopy(m, t->GetMatrix());
  return formatDoubles(m, 16);
}

void applyParamsToBoxRepresentation(vtkBoxRepresentation* repr, const QString& params)
{
  if (!repr)
  {
    return;
  }
  double unit[6];
  unitBoxBounds(unit);
  repr->PlaceWidget(unit);

  vtkNew<vtkTransform> t;
  double m[16] = { 0 };
  if (parseDoubles(params, m, 16))
  {
    vtkNew<vtkMatrix4x4> mat;
    mat->DeepCopy(m);
    t->SetMatrix(mat);
  }
  else
  {
    double b[6] = { -1, 1, -1, 1, -1, 1 };
    if (!parseDoubles(params, b, 6))
    {
      return;
    }
    aabbToUnitCubeMatrix(b, m);
    vtkNew<vtkMatrix4x4> mat;
    mat->DeepCopy(m);
    t->SetMatrix(mat);
  }
  repr->SetTransform(t);
}

vtkDataSet* clientInputDataSet(vtkSMProxy* filter)
{
  if (!filter)
  {
    return nullptr;
  }
  auto* inputProp = vtkSMInputProperty::SafeDownCast(filter->GetProperty("Input"));
  if (!inputProp || inputProp->GetNumberOfProxies() == 0)
  {
    return nullptr;
  }
  auto* src = vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
  if (!src)
  {
    return nullptr;
  }
  auto* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  if (!alg)
  {
    return nullptr;
  }
  const unsigned int port = inputProp->GetOutputPortForConnection(0);
  return vtkDataSet::SafeDownCast(alg->GetOutputDataObject(static_cast<int>(port)));
}

vtkDataSet* clientSelectedDataSet(pqOutputPort* port)
{
  if (!port)
  {
    return nullptr;
  }
  pqPipelineSource* src = port->getSource();
  vtkSMProxy* sm = src ? src->getProxy() : nullptr;
  auto* alg = sm ? vtkAlgorithm::SafeDownCast(sm->GetClientSideObject()) : nullptr;
  if (!alg)
  {
    return nullptr;
  }
  return vtkDataSet::SafeDownCast(alg->GetOutputDataObject(port->getPortNumber()));
}

void appendMappedCellIds(vtkIdTypeArray* selected, vtkDataSet* convertTarget, vtkDataSet* inputDs,
  std::vector<vtkIdType>& ids)
{
  if (!selected || !inputDs)
  {
    return;
  }
  const vtkIdType n = selected->GetNumberOfTuples();
  const vtkIdType nMesh = inputDs->GetNumberOfCells();
  vtkIdTypeArray* origIds = nullptr;
  if (convertTarget)
  {
    origIds = vtkIdTypeArray::SafeDownCast(convertTarget->GetCellData()->GetArray("vtkOriginalCellIds"));
  }
  ids.reserve(ids.size() + static_cast<size_t>(n));
  for (vtkIdType i = 0; i < n; ++i)
  {
    vtkIdType cid = selected->GetValue(i);
    if (origIds && cid >= 0 && cid < origIds->GetNumberOfTuples())
    {
      cid = origIds->GetValue(cid);
    }
    if (cid >= 0 && cid < nMesh)
    {
      ids.push_back(cid);
    }
  }
}

bool collectActiveCellIds(vtkDataSet* inputDs, std::vector<vtkIdType>& ids, QString& error)
{
  ids.clear();
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqSelectionManager* selMgr = core ? core->selectionManager() : nullptr;
  if (!selMgr || !selMgr->hasActiveSelection())
  {
    error = QObject::tr("No active view selection.");
    return false;
  }
  pqOutputPort* port = selMgr->getSelectedPort();
  vtkSMSourceProxy* selIn = port ? port->getSelectionInput() : nullptr;
  if (!selIn)
  {
    error = QObject::tr("Active selection has no selection source.");
    return false;
  }
  selIn->UpdatePipeline();
  auto* selAlg = vtkAlgorithm::SafeDownCast(selIn->GetClientSideObject());
  vtkSelection* selection =
    selAlg ? vtkSelection::SafeDownCast(selAlg->GetOutputDataObject(0)) : nullptr;
  if (!selection)
  {
    error = QObject::tr("Could not read vtkSelection.");
    return false;
  }
  if (!inputDs)
  {
    error = QObject::tr("Filter Input has no client-side vtkDataSet.");
    return false;
  }

  vtkDataSet* selectedDs = clientSelectedDataSet(port);
  vtkDataSet* convertTarget = selectedDs ? selectedDs : inputDs;
  vtkNew<vtkIdTypeArray> selected;
  vtkConvertSelection::GetSelectedCells(selection, convertTarget, selected);
  appendMappedCellIds(selected, convertTarget, inputDs, ids);
  if (ids.empty() && selectedDs && selectedDs != inputDs)
  {
    selected->Initialize();
    vtkConvertSelection::GetSelectedCells(selection, inputDs, selected);
    appendMappedCellIds(selected, inputDs, inputDs, ids);
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  if (ids.empty())
  {
    error = QObject::tr("View selection did not resolve to cell ids on the filter Input.");
    return false;
  }
  return true;
}

bool isGeometryClassName(const QString& className)
{
  if (className.isEmpty())
  {
    return false;
  }
  const QString lower = className.toLower();
  if (lower.contains(QLatin1String("table")) || lower.contains(QLatin1String("selection")) ||
    lower.contains(QLatin1String("graph")) || lower.contains(QLatin1String("tree")))
  {
    return false;
  }
  return lower.contains(QLatin1String("dataset")) || lower.contains(QLatin1String("polydata")) ||
    lower.contains(QLatin1String("unstructured")) || lower.contains(QLatin1String("imagedata")) ||
    lower.contains(QLatin1String("structured")) || lower.contains(QLatin1String("partitioned")) ||
    lower.contains(QLatin1String("multiblock")) || lower.contains(QLatin1String("composite")) ||
    lower.contains(QLatin1String("hyper"));
}

void collectConsumers(pqPipelineSource* src, QSet<pqPipelineSource*>& out)
{
  if (!src || out.contains(src))
  {
    return;
  }
  out.insert(src);
  const QList<pqPipelineSource*> consumers = src->getAllConsumers();
  for (pqPipelineSource* c : consumers)
  {
    collectConsumers(c, out);
  }
}

void styleBoxRepresentation(vtkBoxRepresentation* repr)
{
  if (!repr)
  {
    return;
  }
  if (auto* p = repr->GetOutlineProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
    p->SetLineWidth(2.0);
  }
  if (auto* p = repr->GetSelectedOutlineProperty())
  {
    p->SetColor(1.0, 0.85, 0.15);
  }
  if (auto* p = repr->GetHandleProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
  }
  if (auto* p = repr->GetSelectedHandleProperty())
  {
    p->SetColor(1.0, 0.85, 0.15);
  }
  repr->SetPlaceFactor(1.0);
  repr->HandlesOn();
  repr->SetOutlineFaceWires(1);
}

void styleSphereRepresentation(vtkSphereRepresentation* repr)
{
  if (!repr)
  {
    return;
  }
  repr->SetRepresentationToSurface();
  repr->SetPhiResolution(24);
  repr->SetThetaResolution(32);
  repr->HandleVisibilityOn();
  if (auto* p = repr->GetSphereProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
    p->SetOpacity(0.35);
  }
  if (auto* p = repr->GetSelectedSphereProperty())
  {
    p->SetColor(1.0, 0.85, 0.15);
    p->SetOpacity(0.45);
  }
  if (auto* p = repr->GetHandleProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
  }
}

constexpr int kMaxUniqueCellDataValues = 256;

bool isIntegralDataType(int vtkType)
{
  switch (vtkType)
  {
    case VTK_BIT:
    case VTK_CHAR:
    case VTK_SIGNED_CHAR:
    case VTK_UNSIGNED_CHAR:
    case VTK_SHORT:
    case VTK_UNSIGNED_SHORT:
    case VTK_INT:
    case VTK_UNSIGNED_INT:
    case VTK_LONG:
    case VTK_UNSIGNED_LONG:
    case VTK_ID_TYPE:
    case VTK_LONG_LONG:
    case VTK_UNSIGNED_LONG_LONG:
      return true;
    default:
      return false;
  }
}

QString sanitizePatchName(const QString& raw)
{
  QString s = raw;
  s.replace(QLatin1Char('\r'), QLatin1Char(' '));
  s.replace(QLatin1Char('\n'), QLatin1Char(' '));
  s.replace(QLatin1Char('\t'), QLatin1Char(' '));
  return s.trimmed();
}

QString cellDataValueName(vtkAbstractArray* arr, vtkIdType i, const QString& arrayName)
{
  if (!arr)
  {
    return QString();
  }
  if (auto* str = vtkStringArray::SafeDownCast(arr))
  {
    const QString name = sanitizePatchName(QString::fromUtf8(str->GetValue(i).c_str()));
    if (name.isEmpty())
    {
      return QStringLiteral("%1_empty").arg(arrayName);
    }
    return name;
  }
  if (auto* data = vtkDataArray::SafeDownCast(arr))
  {
    const double v = data->GetComponent(i, 0);
    if (!std::isfinite(v))
    {
      return QString();
    }
    if (isIntegralDataType(data->GetDataType()) || std::floor(v) == v)
    {
      return QString::number(static_cast<qint64>(std::llround(v)));
    }
    return QString::number(v, 'g', 8);
  }
  const vtkVariant variant = arr->GetVariantValue(i);
  const QString name = sanitizePatchName(QString::fromUtf8(variant.ToString().c_str()));
  if (name.isEmpty())
  {
    return QStringLiteral("%1_empty").arg(arrayName);
  }
  return name;
}

vtkSMSourceProxy* inputSourceProxy(vtkSMProxy* filter, unsigned int& port)
{
  port = 0;
  if (!filter)
  {
    return nullptr;
  }
  auto* inputProp = vtkSMInputProperty::SafeDownCast(filter->GetProperty("Input"));
  if (!inputProp || inputProp->GetNumberOfProxies() == 0)
  {
    return nullptr;
  }
  port = inputProp->GetOutputPortForConnection(0);
  return vtkSMSourceProxy::SafeDownCast(inputProp->GetProxy(0));
}

QStringList cellDataArrayNames(vtkSMProxy* filter)
{
  QStringList names;
  unsigned int port = 0;
  vtkSMSourceProxy* src = inputSourceProxy(filter, port);
  if (src)
  {
    src->UpdatePipeline();
  }

  if (vtkDataSet* ds = clientInputDataSet(filter))
  {
    vtkCellData* cd = ds->GetCellData();
    const int n = cd ? cd->GetNumberOfArrays() : 0;
    for (int i = 0; i < n; ++i)
    {
      vtkAbstractArray* arr = cd->GetAbstractArray(i);
      const char* name = arr ? arr->GetName() : nullptr;
      if (name && name[0] != '\0')
      {
        names << QString::fromUtf8(name);
      }
    }
    names.removeDuplicates();
    return names;
  }

  if (!src)
  {
    return names;
  }
  vtkPVDataInformation* di = src->GetDataInformation(port);
  vtkPVDataSetAttributesInformation* cellInfo = di ? di->GetCellDataInformation() : nullptr;
  const int n = cellInfo ? cellInfo->GetNumberOfArrays() : 0;
  for (int i = 0; i < n; ++i)
  {
    vtkPVArrayInformation* ai = cellInfo->GetArrayInformation(i);
    const char* name = ai ? ai->GetName() : nullptr;
    if (name && name[0] != '\0')
    {
      names << QString::fromUtf8(name);
    }
  }
  names.removeDuplicates();
  return names;
}

QIcon eyeIcon(Qt::CheckState state)
{
  if (state == Qt::Unchecked)
  {
    return QIcon(QStringLiteral(":/pqWidgets/Icons/pqEyeballClosed.svg"));
  }
  if (state == Qt::Checked)
  {
    return QIcon(QStringLiteral(":/pqWidgets/Icons/pqEyeball.svg"));
  }

  const QIcon open(QStringLiteral(":/pqWidgets/Icons/pqEyeball.svg"));
  QIcon mixed;
  for (const int dim : { 16, 20, 24, 32 })
  {
    const QPixmap src = open.pixmap(dim, dim);
    QPixmap faded(src.size());
    faded.fill(Qt::transparent);
    QPainter painter(&faded);
    painter.setOpacity(0.4);
    painter.drawPixmap(0, 0, src);
    painter.end();
    mixed.addPixmap(faded);
  }
  return mixed;
}

QIcon eyeIcon(bool visible)
{
  return eyeIcon(visible ? Qt::Checked : Qt::Unchecked);
}

QStringList checkedSelectorsFromProperty(
  vtkSMStringVectorProperty* prop, vtkDataAssembly* assembly)
{
  if (!prop)
  {
    return { QStringLiteral("/") };
  }

  const std::vector<std::string>& elems = prop->GetElements();
  if (elems.empty())
  {
    return {};
  }

  const char* root = assembly ? assembly->GetRootNodeName() : nullptr;
  const std::string rootName = std::string("/") + (root ? root : "");
  if (elems[0] == "/" || elems[0] == rootName)
  {
    return { QStringLiteral("/") };
  }

  QStringList checked;
  checked.reserve(static_cast<int>(elems.size()));
  for (const std::string& elem : elems)
  {
    checked.push_back(QString::fromStdString(elem));
  }
  return checked;
}

int findNodeByDataSetIndex(vtkDataAssembly* assembly, int parent, unsigned int index)
{
  if (!assembly || parent < 0)
  {
    return -1;
  }

  const std::vector<unsigned int> indices =
    assembly->GetDataSetIndices(parent, /*traverse_subtree=*/false);
  for (unsigned int value : indices)
  {
    if (value == index)
    {
      return parent;
    }
  }

  const int n = assembly->GetNumberOfChildren(parent);
  for (int i = 0; i < n; ++i)
  {
    const int found = findNodeByDataSetIndex(assembly, assembly->GetChild(parent, i), index);
    if (found >= 0)
    {
      return found;
    }
  }
  return -1;
}

int findNodeByLabel(vtkDataAssembly* assembly, int parent, const QString& label)
{
  if (!assembly || parent < 0 || label.isEmpty())
  {
    return -1;
  }

  const char* nodeLabel = nullptr;
  if (assembly->GetAttribute(parent, "label", nodeLabel) && nodeLabel && nodeLabel[0] != '\0' &&
    QString::fromUtf8(nodeLabel) == label)
  {
    return parent;
  }

  const int n = assembly->GetNumberOfChildren(parent);
  for (int i = 0; i < n; ++i)
  {
    const int found = findNodeByLabel(assembly, assembly->GetChild(parent, i), label);
    if (found >= 0)
    {
      return found;
    }
  }
  return -1;
}

vtkSMStringVectorProperty* blockVisibilityProperty(vtkSMProxy* reprProxy)
{
  if (!reprProxy)
  {
    return nullptr;
  }
  auto* visibility =
    vtkSMStringVectorProperty::SafeDownCast(reprProxy->GetProperty("BlockVisibilities"));
  return visibility ? visibility
                    : vtkSMStringVectorProperty::SafeDownCast(reprProxy->GetProperty("BlockSelectors"));
}
}

struct pqSHYXSelectionPatchShapeHost
{
  struct Entry
  {
    QString Kind;
    int TableRow = -1;
    vtkSmartPointer<vtkBoxWidget2> Box;
    vtkSmartPointer<vtkSphereWidget2> Sphere;
  };
  std::vector<Entry> Entries;
  vtkSmartPointer<vtkCallbackCommand> Observer;
  vtkRenderWindowInteractor* Interactor = nullptr;
};

pqSHYXSelectionPatchTableWidget::pqSHYXSelectionPatchTableWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  auto* tip = new QLabel(
    tr("Add geometry patches from a 3D cell selection, an Input CellData array (one row per unique "
       "value), another pipeline node, or a box/sphere shape. All rows become PDC partitions "
       "(SnappyHexMesh can use any of them as a Region). Click the eye to show or hide that patch "
       "in the active view (same as Hide Block; same-named rows share one block). Port 0 is added "
       "patches; port 1 is Input minus selection-row cells. Apply on Add (default) Applies after "
       "each Add or Remove. Rename only (any name). The table keeps every row; Apply merges same "
       "names. Check Show Interactable widget and select a Box or Sphere row to edit it in the "
       "3D view."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  this->Model = new QStandardItemModel(0, 5, this);
  this->Model->setHeaderData(kColVisibility, Qt::Horizontal, eyeIcon(true), Qt::DecorationRole);
  this->Model->setHeaderData(kColVisibility, Qt::Horizontal,
    tr("Show or hide this patch in the active view"), Qt::ToolTipRole);
  this->Model->setHeaderData(kColIndex, Qt::Horizontal, tr("#"));
  this->Model->setHeaderData(kColName, Qt::Horizontal, tr("Name"));
  this->Model->setHeaderData(kColType, Qt::Horizontal, tr("Type"));
  this->Model->setHeaderData(kColInfo, Qt::Horizontal, tr("Info"));

  this->View = new pqTreeView(this);
  this->View->setObjectName("SHYXSelectionPatchTable");
  this->View->setRootIsDecorated(false);
  this->View->setAlternatingRowColors(true);
  this->View->setAllColumnsShowFocus(true);
  this->View->setUniformRowHeights(true);
  this->View->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->View->setSelectionMode(QAbstractItemView::ExtendedSelection);
  this->View->setEditTriggers(
    QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  this->View->setSortingEnabled(false);
  this->View->setMaximumRowCountBeforeScrolling(
    pqPropertyWidget::hintsWidgetHeightNumberOfRows(smgroup ? smgroup->GetHints() : nullptr, 8));
  this->View->setModel(this->Model);

  auto* header = this->View->header();
  header->setSectionsClickable(true);
  header->setHighlightSections(false);
  header->setSectionResizeMode(kColVisibility, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColIndex, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColName, QHeaderView::Stretch);
  header->setSectionResizeMode(kColType, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColInfo, QHeaderView::ResizeToContents);
  header->setStretchLastSection(false);
  vbox->addWidget(this->View, 1);

  auto* addRow = new QHBoxLayout();
  addRow->setContentsMargins(0, 0, 0, 0);
  addRow->setSpacing(4);
  auto* addSelBtn = new QPushButton(tr("Add from selection"), this);
  addSelBtn->setToolTip(tr("Snapshot the current cell selection as a new geo_N row."));

  this->PipelineMenu = new QMenu(this);
  auto* addPipeBtn = new QToolButton(this);
  addPipeBtn->setText(tr("Add from pipeline"));
  addPipeBtn->setPopupMode(QToolButton::InstantPopup);
  addPipeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
  addPipeBtn->setMenu(this->PipelineMenu);
  addPipeBtn->setToolTip(
    tr("Add a pipeline geometry node as an extra patch (same PDC entries as selection rows)."));

  this->CellDataMenu = new QMenu(this);
  auto* addCellDataBtn = new QToolButton(this);
  addCellDataBtn->setText(tr("Add from Celldata"));
  addCellDataBtn->setPopupMode(QToolButton::InstantPopup);
  addCellDataBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
  addCellDataBtn->setMenu(this->CellDataMenu);
  addCellDataBtn->setToolTip(
    tr("Pick a CellData array on the Input. Each unique value becomes a selection patch "
       "(name = value; first component of vectors). Integer and string labels work best."));

  auto* shapeMenu = new QMenu(this);
  shapeMenu->addAction(tr("Box"), this, &pqSHYXSelectionPatchTableWidget::onAddBox);
  shapeMenu->addAction(tr("Sphere"), this, &pqSHYXSelectionPatchTableWidget::onAddSphere);
  auto* addShapeBtn = new QToolButton(this);
  addShapeBtn->setText(tr("Add from shape"));
  addShapeBtn->setPopupMode(QToolButton::InstantPopup);
  addShapeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
  addShapeBtn->setMenu(shapeMenu);
  addShapeBtn->setToolTip(
    tr("Add a parametric box or sphere (initial size/position like Sphere Selection). "
       "Check Show Interactable widget and keep the row selected to drag it."));

  addRow->addWidget(addSelBtn);
  addRow->addWidget(addCellDataBtn);
  addRow->addWidget(addPipeBtn);
  addRow->addWidget(addShapeBtn);
  addRow->addStretch(1);
  vbox->addLayout(addRow);

  auto* editRow = new QHBoxLayout();
  editRow->setContentsMargins(0, 0, 0, 0);
  editRow->setSpacing(4);
  auto* removeBtn = new QPushButton(tr("Remove selected"), this);
  this->ShowInteractable = new QCheckBox(tr("Show Interactable widget"), this);
  this->ShowInteractable->setObjectName("SHYXSelectionPatchShowInteractable");
  this->ShowInteractable->setChecked(false);
  this->ShowInteractable->setToolTip(
    tr("When checked, the selected Box or Sphere row shows a draggable 3D widget. "
       "Selection and pipeline rows have no widget."));
  this->ApplyOnAdd = new QCheckBox(tr("Apply on Add/Remove"), this);
  this->ApplyOnAdd->setChecked(true);
  this->ApplyOnAdd->setToolTip(
    tr("When checked (default), Add and Remove also Apply so Added patches and Remaining cells "
       "refresh. After Remove, dropped selection cells reappear on Remaining cells. Uncheck to "
       "leave outputs stale so already-added cells can be selected again."));
  editRow->addWidget(removeBtn);
  editRow->addStretch(1);
  editRow->addWidget(this->ShowInteractable);
  editRow->addWidget(this->ApplyOnAdd);
  vbox->addLayout(editRow);

  this->Status = new QLabel(this);
  this->Status->setWordWrap(true);
  this->Status->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(this->Status);

  QObject::connect(this->Model, &QStandardItemModel::itemChanged, this,
    &pqSHYXSelectionPatchTableWidget::onItemChanged);
  QObject::connect(addSelBtn, &QPushButton::clicked, this,
    &pqSHYXSelectionPatchTableWidget::onAddFromSelection);
  QObject::connect(removeBtn, &QPushButton::clicked, this,
    &pqSHYXSelectionPatchTableWidget::onRemoveSelected);
  QObject::connect(this->PipelineMenu, &QMenu::aboutToShow, this,
    &pqSHYXSelectionPatchTableWidget::onPopulatePipelineMenu);
  QObject::connect(this->CellDataMenu, &QMenu::aboutToShow, this,
    &pqSHYXSelectionPatchTableWidget::onPopulateCellDataMenu);
  QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::viewChanged, this,
    &pqSHYXSelectionPatchTableWidget::onActiveViewChanged);
  this->connectViewVisibility(pqActiveObjects::instance().activeView());
  QObject::connect(this->ShowInteractable, &QCheckBox::toggled, this,
    &pqSHYXSelectionPatchTableWidget::onShowInteractableToggled);
  QObject::connect(this->View, &pqTreeView::clicked, this,
    &pqSHYXSelectionPatchTableWidget::onViewClicked);
  QObject::connect(this->View->header(), &QHeaderView::sectionClicked, this,
    &pqSHYXSelectionPatchTableWidget::onHeaderSectionClicked);
  QObject::connect(this->View->selectionModel(), &QItemSelectionModel::selectionChanged, this,
    [this](const QItemSelection&, const QItemSelection&) { this->onTableSelectionChanged(); });

  vtkSMProperty* namesProp = propertyFromGroup(smgroup, smproxy, "Names", "PatchNames");
  if (namesProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(namesProp) : nullptr;
    this->NamesPropertyName = QString::fromUtf8(pname ? pname : "PatchNames");
    this->addPropertyLink(
      this, this->NamesPropertyName.toUtf8().data(), SIGNAL(patchesChanged()), namesProp);
  }
  vtkSMProperty* idsProp = propertyFromGroup(smgroup, smproxy, "CellIds", "PatchCellIds");
  if (idsProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(idsProp) : nullptr;
    this->CellIdsPropertyName = QString::fromUtf8(pname ? pname : "PatchCellIds");
    this->addPropertyLink(
      this, this->CellIdsPropertyName.toUtf8().data(), SIGNAL(patchesChanged()), idsProp);
  }
  vtkSMProperty* kindsProp = propertyFromGroup(smgroup, smproxy, "Kinds", "PatchKinds");
  if (kindsProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(kindsProp) : nullptr;
    this->KindsPropertyName = QString::fromUtf8(pname ? pname : "PatchKinds");
    this->addPropertyLink(
      this, this->KindsPropertyName.toUtf8().data(), SIGNAL(patchesChanged()), kindsProp);
  }
  vtkSMProperty* paramsProp = propertyFromGroup(smgroup, smproxy, "Params", "PatchParams");
  if (paramsProp)
  {
    const char* pname = smproxy ? smproxy->GetPropertyName(paramsProp) : nullptr;
    this->ParamsPropertyName = QString::fromUtf8(pname ? pname : "PatchParams");
    this->addPropertyLink(
      this, this->ParamsPropertyName.toUtf8().data(), SIGNAL(patchesChanged()), paramsProp);
  }

  this->setChangeAvailableAsChangeFinished(true);
  this->rebuildFromProperty();

  this->BlockVisibilityVTKConnect = vtkEventQtSlotConnect::New();
  QObject::connect(&pqActiveObjects::instance(),
    QOverload<pqDataRepresentation*>::of(&pqActiveObjects::representationChanged), this,
    &pqSHYXSelectionPatchTableWidget::onActiveViewOrRepresentationChanged);
  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    if (auto* src = smm->findItem<pqPipelineSource*>(smproxy))
    {
      this->RepresentationConnections.push_back(
        QObject::connect(src, &pqPipelineSource::representationAdded, this,
          [this](pqPipelineSource*, pqDataRepresentation*, int)
          { this->connectBlockVisibilityObserver(); }));
      this->RepresentationConnections.push_back(
        QObject::connect(src, &pqPipelineSource::representationRemoved, this,
          [this](pqPipelineSource*, pqDataRepresentation*, int)
          { this->connectBlockVisibilityObserver(); }));
      this->RepresentationConnections.push_back(QObject::connect(
        src, QOverload<pqPipelineSource*>::of(&pqPipelineSource::dataUpdated), this,
        [this](pqPipelineSource*) { this->updateEyeIcons(); }));
    }
  }
  this->connectBlockVisibilityObserver();
}

pqSHYXSelectionPatchTableWidget::~pqSHYXSelectionPatchTableWidget()
{
  this->disconnectViewVisibilityLinks();
  this->disconnectBlockVisibilityObserver();
  for (const QMetaObject::Connection& c : this->RepresentationConnections)
  {
    QObject::disconnect(c);
  }
  this->RepresentationConnections.clear();
  if (this->BlockVisibilityVTKConnect)
  {
    this->BlockVisibilityVTKConnect->Delete();
    this->BlockVisibilityVTKConnect = nullptr;
  }
  this->destroyShapeWidgets();
}

void pqSHYXSelectionPatchTableWidget::select()
{
  this->Superclass::select();
  this->ShapeWidgetsVisible = true;
  this->syncShapeWidgets();
}

void pqSHYXSelectionPatchTableWidget::deselect()
{
  this->ShapeWidgetsVisible = false;
  this->setShapeWidgetsEnabled(false);
  this->Superclass::deselect();
}

void pqSHYXSelectionPatchTableWidget::setView(pqView* view)
{
  this->Superclass::setView(view);
  this->connectViewVisibility(pqActiveObjects::instance().activeView());
  this->connectBlockVisibilityObserver();
  this->syncShapeWidgets();
}

void pqSHYXSelectionPatchTableWidget::onActiveViewChanged()
{
  this->connectViewVisibility(pqActiveObjects::instance().activeView());
  this->connectBlockVisibilityObserver();
  this->syncShapeWidgets();
}

void pqSHYXSelectionPatchTableWidget::onShowInteractableToggled(bool)
{
  this->syncShapeWidgets();
}

void pqSHYXSelectionPatchTableWidget::onTableSelectionChanged()
{
  this->syncShapeWidgets();
}

bool pqSHYXSelectionPatchTableWidget::shapeWidgetsWanted() const
{
  if (!(this->ShapeWidgetsVisible && this->ShowInteractable && this->ShowInteractable->isChecked()))
  {
    return false;
  }
  return this->isSourceVisibleInView(pqActiveObjects::instance().activeView());
}

void pqSHYXSelectionPatchTableWidget::disconnectViewVisibilityLinks()
{
  for (const QMetaObject::Connection& c : this->ViewVisibilityConnections)
  {
    QObject::disconnect(c);
  }
  this->ViewVisibilityConnections.clear();
}

void pqSHYXSelectionPatchTableWidget::connectViewVisibility(pqView* view)
{
  this->disconnectViewVisibilityLinks();
  if (!view)
  {
    return;
  }
  this->ViewVisibilityConnections.push_back(QObject::connect(view,
    &pqView::representationVisibilityChanged, this,
    [this](pqRepresentation*, bool) { this->syncShapeWidgets(); }));
}

bool pqSHYXSelectionPatchTableWidget::isSourceVisibleInView(pqView* view) const
{
  if (!view)
  {
    return true;
  }
  auto* smm = pqApplicationCore::instance()->getServerManagerModel();
  auto* src = smm ? smm->findItem<pqPipelineSource*>(this->proxy()) : nullptr;
  if (!src)
  {
    return true;
  }
  bool foundRepr = false;
  for (pqRepresentation* repr : view->getRepresentations())
  {
    auto* dr = qobject_cast<pqDataRepresentation*>(repr);
    if (!dr || dr->getInput() != src)
    {
      continue;
    }
    foundRepr = true;
    if (dr->isVisible())
    {
      return true;
    }
  }
  return !foundRepr;
}

QList<int> pqSHYXSelectionPatchTableWidget::selectedShapeRowIndices() const
{
  QList<int> out;
  if (!this->View || !this->View->selectionModel())
  {
    return out;
  }
  QList<int> rows;
  const QModelIndexList selected = this->View->selectionModel()->selectedRows();
  rows.reserve(selected.size());
  for (const QModelIndex& idx : selected)
  {
    rows.push_back(idx.row());
  }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  for (int r : rows)
  {
    const QString kind = this->normalizedKind(
      this->Model->item(r, kColIndex) ? this->Model->item(r, kColIndex)->data(kRoleKind).toString()
                                      : QString());
    if (kind == QLatin1String("box") || kind == QLatin1String("sphere"))
    {
      out.push_back(r);
    }
  }
  return out;
}

bool pqSHYXSelectionPatchTableWidget::event(QEvent* e)
{
  if (e->type() == QEvent::DynamicPropertyChange && !this->UpdatingFromUI)
  {
    auto* devt = static_cast<QDynamicPropertyChangeEvent*>(e);
    const QString name = QString::fromLatin1(devt->propertyName());
    if (name == this->NamesPropertyName || name == this->CellIdsPropertyName ||
      name == this->KindsPropertyName || name == this->ParamsPropertyName)
    {
      this->rebuildFromProperty();
      return true;
    }
  }
  return this->Superclass::event(e);
}

void pqSHYXSelectionPatchTableWidget::apply()
{
  QScopedValueRollback<bool> guard(this->Applying, true);
  this->writeBackProperty();
  this->Superclass::apply();
}

void pqSHYXSelectionPatchTableWidget::reset()
{
  this->Superclass::reset();
  this->rebuildFromProperty();
}

void pqSHYXSelectionPatchTableWidget::onItemChanged(QStandardItem*)
{
  if (this->UpdatingFromProperty)
  {
    return;
  }
  this->writeBackProperty();
}

void pqSHYXSelectionPatchTableWidget::setStatus(const QString& text, bool error)
{
  if (!this->Status)
  {
    return;
  }
  this->Status->setText(text);
  this->Status->setStyleSheet(error ? QStringLiteral("color: #b00020; font-size: 11px;")
                                    : QStringLiteral("color: gray; font-size: 11px;"));
}

int pqSHYXSelectionPatchTableWidget::nextPartIndex() const
{
  int maxN = -1;
  const int rows = this->Model->rowCount();
  for (int r = 0; r < rows; ++r)
  {
    const QString name = this->Model->item(r, kColName)
      ? this->Model->item(r, kColName)->text().trimmed()
      : QString();
    const int under = name.lastIndexOf(QLatin1Char('_'));
    if (under < 0 || under + 1 >= name.size())
    {
      continue;
    }
    bool ok = false;
    const int n = name.mid(under + 1).toInt(&ok);
    if (ok)
    {
      maxN = std::max(maxN, n);
    }
  }
  return maxN + 1;
}

QString pqSHYXSelectionPatchTableWidget::normalizedKind(const QString& kind) const
{
  const QString k = kind.trimmed().toLower();
  if (k == QLatin1String("pipeline") || k == QLatin1String("box") || k == QLatin1String("sphere"))
  {
    return k;
  }
  return QStringLiteral("selection");
}

QString pqSHYXSelectionPatchTableWidget::kindLabel(const QString& kind) const
{
  const QString k = this->normalizedKind(kind);
  if (k == QLatin1String("pipeline"))
  {
    return tr("Pipeline");
  }
  if (k == QLatin1String("box"))
  {
    return tr("Box");
  }
  if (k == QLatin1String("sphere"))
  {
    return tr("Sphere");
  }
  return tr("Selection");
}

QString pqSHYXSelectionPatchTableWidget::infoText(const PatchRow& row) const
{
  const QString k = this->normalizedKind(row.Kind);
  if (k == QLatin1String("selection"))
  {
    return QString::number(countCompactIds(row.CellIds));
  }
  if (k == QLatin1String("pipeline"))
  {
    return row.Params;
  }
  if (k == QLatin1String("box"))
  {
    double b[6] = { 0, 0, 0, 0, 0, 0 };
    if (parseDoubles(row.Params, b, 6))
    {
      return tr("box");
    }
  }
  if (k == QLatin1String("sphere"))
  {
    double s[4] = { 0, 0, 0, 0 };
    if (parseDoubles(row.Params, s, 4))
    {
      return tr("r=%1").arg(s[3], 0, 'g', 4);
    }
  }
  return QStringLiteral("—");
}

void pqSHYXSelectionPatchTableWidget::appendRow(
  const PatchRow& row, const QString& okStatus, bool errorStatus)
{
  this->appendRows(QList<PatchRow>{ row }, okStatus, errorStatus);
}

void pqSHYXSelectionPatchTableWidget::appendRows(
  const QList<PatchRow>& extra, const QString& okStatus, bool errorStatus)
{
  if (extra.isEmpty())
  {
    this->setStatus(okStatus, errorStatus);
    return;
  }
  QList<PatchRow> rows = this->collectRows();
  rows.append(extra);
  this->rebuildRows(rows);
  this->writeBackProperty();
  if (this->View && this->View->selectionModel() && this->Model->rowCount() > 0)
  {
    const int firstNew = this->Model->rowCount() - extra.size();
    const int lastNew = this->Model->rowCount() - 1;
    if (firstNew >= 0 && lastNew >= firstNew)
    {
      const QModelIndex topLeft = this->Model->index(firstNew, 0);
      const QModelIndex bottomRight = this->Model->index(lastNew, 0);
      this->View->selectionModel()->select(QItemSelection(topLeft, bottomRight),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
  }
  this->setStatus(okStatus, errorStatus);
  if (this->ApplyOnAdd && this->ApplyOnAdd->isChecked())
  {
    this->applyOutputsIfChecked();
  }
}

void pqSHYXSelectionPatchTableWidget::onAddFromSelection()
{
  std::vector<vtkIdType> ids;
  QString error;
  vtkDataSet* inputDs = clientInputDataSet(this->proxy());
  if (!collectActiveCellIds(inputDs, ids, error))
  {
    this->setStatus(error, true);
    return;
  }

  PatchRow row;
  row.Name = QStringLiteral("geo_%1").arg(this->nextPartIndex());
  row.Kind = QStringLiteral("selection");
  row.CellIds = compactIdList(ids);
  this->appendRow(row,
    (this->ApplyOnAdd && this->ApplyOnAdd->isChecked())
      ? tr("Added %1 (%2 cells). Applying to refresh added and remaining ports.")
          .arg(row.Name)
          .arg(ids.size())
      : tr("Added %1 (%2 cells). Remaining port not refreshed; overlaps allowed until Apply.")
          .arg(row.Name)
          .arg(ids.size()));
}

void pqSHYXSelectionPatchTableWidget::onPopulateCellDataMenu()
{
  if (!this->CellDataMenu)
  {
    return;
  }
  this->CellDataMenu->clear();

  const QStringList names = cellDataArrayNames(this->proxy());
  if (names.isEmpty())
  {
    auto* a = this->CellDataMenu->addAction(tr("(no CellData arrays on Input)"));
    a->setEnabled(false);
    return;
  }

  vtkDataSet* ds = clientInputDataSet(this->proxy());
  vtkCellData* cd = ds ? ds->GetCellData() : nullptr;
  for (const QString& name : names)
  {
    QString label = name;
    vtkAbstractArray* arr = cd ? cd->GetAbstractArray(name.toUtf8().constData()) : nullptr;
    if (arr && arr->GetNumberOfComponents() > 1)
    {
      label = tr("%1 (%2 comps, first used)").arg(name).arg(arr->GetNumberOfComponents());
    }
    auto* act = this->CellDataMenu->addAction(label);
    QObject::connect(act, &QAction::triggered, this, [this, name]() { this->onAddFromCellData(name); });
  }
}

void pqSHYXSelectionPatchTableWidget::onAddFromCellData(const QString& arrayName)
{
  if (arrayName.isEmpty())
  {
    this->setStatus(tr("No CellData array selected."), true);
    return;
  }

  unsigned int port = 0;
  if (vtkSMSourceProxy* src = inputSourceProxy(this->proxy(), port))
  {
    src->UpdatePipeline();
  }

  vtkDataSet* inputDs = clientInputDataSet(this->proxy());
  if (!inputDs)
  {
    this->setStatus(tr("Filter Input has no client-side vtkDataSet."), true);
    return;
  }
  vtkCellData* cd = inputDs->GetCellData();
  vtkAbstractArray* arr = cd ? cd->GetAbstractArray(arrayName.toUtf8().constData()) : nullptr;
  if (!arr)
  {
    this->setStatus(tr("CellData array '%1' was not found on Input.").arg(arrayName), true);
    return;
  }

  const vtkIdType nCells = inputDs->GetNumberOfCells();
  const vtkIdType nTuples = arr->GetNumberOfTuples();
  const vtkIdType n = nCells < nTuples ? nCells : nTuples;
  if (n <= 0)
  {
    this->setStatus(tr("CellData array '%1' has no values on Input cells.").arg(arrayName), true);
    return;
  }

  QStringList order;
  QMap<QString, std::vector<vtkIdType>> groups;
  vtkIdType skipped = 0;
  for (vtkIdType i = 0; i < n; ++i)
  {
    const QString key = cellDataValueName(arr, i, arrayName);
    if (key.isEmpty())
    {
      ++skipped;
      continue;
    }
    auto it = groups.find(key);
    if (it == groups.end())
    {
      if (order.size() >= kMaxUniqueCellDataValues)
      {
        this->setStatus(
          tr("CellData '%1' has more than %2 unique values. Pick a discrete label array "
             "(patch name, region id), not a continuous scalar.")
            .arg(arrayName)
            .arg(kMaxUniqueCellDataValues),
          true);
        return;
      }
      order.push_back(key);
      groups.insert(key, std::vector<vtkIdType>());
      it = groups.find(key);
    }
    it.value().push_back(i);
  }

  if (order.isEmpty())
  {
    this->setStatus(
      tr("CellData '%1' had no usable values (all empty or non-finite).").arg(arrayName), true);
    return;
  }

  QList<PatchRow> extra;
  extra.reserve(order.size());
  vtkIdType nCellsAdded = 0;
  for (const QString& name : order)
  {
    const std::vector<vtkIdType>& ids = groups[name];
    if (ids.empty())
    {
      continue;
    }
    PatchRow row;
    row.Name = name;
    row.Kind = QStringLiteral("selection");
    row.CellIds = compactIdList(ids);
    extra.push_back(row);
    nCellsAdded += static_cast<vtkIdType>(ids.size());
  }

  QString status =
    tr("Added %1 patch(es) from CellData '%2' (%3 cells).")
      .arg(extra.size())
      .arg(arrayName)
      .arg(static_cast<qint64>(nCellsAdded));
  if (skipped > 0)
  {
    status += QLatin1Char(' ');
    status += tr("Skipped %1 non-finite value(s).").arg(static_cast<qint64>(skipped));
  }
  if (this->ApplyOnAdd && this->ApplyOnAdd->isChecked())
  {
    status += QLatin1Char(' ');
    status += tr("Applying to refresh added and remaining ports.");
  }
  this->appendRows(extra, status);
}

void pqSHYXSelectionPatchTableWidget::applyOutputsIfChecked()
{
  QTimer::singleShot(0, this, []() {
    if (auto* core = pqPVApplicationCore::instance())
    {
      Q_EMIT core->triggerApply();
    }
  });
}

bool pqSHYXSelectionPatchTableWidget::alreadyHasPipeline(
  vtkSMProxy* producer, unsigned int port) const
{
  if (!this->proxy() || !producer)
  {
    return false;
  }
  auto* ip = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("CustomPatches"));
  if (!ip)
  {
    return false;
  }
  const unsigned int n = ip->GetNumberOfProxies();
  for (unsigned int i = 0; i < n; ++i)
  {
    if (ip->GetProxy(i) == producer && ip->GetOutputPortForConnection(i) == port)
    {
      return true;
    }
  }
  return false;
}

void pqSHYXSelectionPatchTableWidget::onPopulatePipelineMenu()
{
  if (!this->PipelineMenu)
  {
    return;
  }
  this->PipelineMenu->clear();

  auto* sm = pqApplicationCore::instance() ? pqApplicationCore::instance()->getServerManagerModel()
                                           : nullptr;
  if (!sm)
  {
    auto* a = this->PipelineMenu->addAction(tr("(no pipeline)"));
    a->setEnabled(false);
    return;
  }

  pqPipelineSource* selfSrc = sm->findItem<pqPipelineSource*>(this->proxy());
  QSet<pqPipelineSource*> forbidden;
  if (selfSrc)
  {
    collectConsumers(selfSrc, forbidden);
  }

  vtkSMProxy* inputProducer = nullptr;
  unsigned int inputPort = 0;
  if (auto* inputProp = vtkSMInputProperty::SafeDownCast(
        this->proxy() ? this->proxy()->GetProperty("Input") : nullptr))
  {
    if (inputProp->GetNumberOfProxies() > 0)
    {
      inputProducer = inputProp->GetProxy(0);
      inputPort = inputProp->GetOutputPortForConnection(0);
    }
  }

  int added = 0;
  const QList<pqPipelineSource*> sources = sm->findItems<pqPipelineSource*>();
  for (pqPipelineSource* src : sources)
  {
    if (!src || !src->getProxy() || forbidden.contains(src))
    {
      continue;
    }
    const int nPorts = src->getNumberOfOutputPorts();
    for (int p = 0; p < nPorts; ++p)
    {
      pqOutputPort* port = src->getOutputPort(p);
      vtkPVDataInformation* di = port ? port->getDataInformation() : nullptr;
      const QString className =
        di ? QString::fromUtf8(di->GetDataClassName() ? di->GetDataClassName() : "") : QString();
      if (!isGeometryClassName(className))
      {
        continue;
      }
      if (src->getProxy() == inputProducer && static_cast<unsigned int>(p) == inputPort)
      {
        continue;
      }
      QString label = src->getSMName();
      if (nPorts > 1)
      {
        label = QStringLiteral("%1 (port %2)").arg(src->getSMName()).arg(p);
      }
      auto* act = this->PipelineMenu->addAction(label);
      vtkSMProxy* producer = src->getProxy();
      const unsigned int uport = static_cast<unsigned int>(p);
      if (this->alreadyHasPipeline(producer, uport))
      {
        act->setEnabled(false);
        act->setText(tr("%1 (already added)").arg(label));
      }
      QObject::connect(act, &QAction::triggered, this, [this, producer, uport, label]() {
        this->addPipelineSource(producer, uport, label);
      });
      ++added;
    }
  }

  if (added == 0)
  {
    auto* a = this->PipelineMenu->addAction(tr("(no geometry sources)"));
    a->setEnabled(false);
  }
}

void pqSHYXSelectionPatchTableWidget::addPipelineSource(
  vtkSMProxy* producer, unsigned int port, const QString& label)
{
  if (!producer || !this->proxy())
  {
    this->setStatus(tr("Could not add pipeline source."), true);
    return;
  }
  if (this->alreadyHasPipeline(producer, port))
  {
    this->setStatus(tr("%1 is already a patch.").arg(label), true);
    return;
  }
  auto* ip = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("CustomPatches"));
  if (!ip)
  {
    this->setStatus(tr("CustomPatches input is missing on the filter."), true);
    return;
  }

  vtkSMPropertyHelper helper(this->proxy(), "CustomPatches");
  helper.Add(producer, port);

  PatchRow row;
  row.Name = QStringLiteral("geo_%1").arg(this->nextPartIndex());
  row.Kind = QStringLiteral("pipeline");
  row.Params = label;
  this->appendRow(row, tr("Added %1 from pipeline (%2).").arg(row.Name).arg(label));
}

void pqSHYXSelectionPatchTableWidget::removeCustomPatchConnections(
  const QList<int>& pipelineIndicesDescending)
{
  if (!this->proxy() || pipelineIndicesDescending.isEmpty())
  {
    return;
  }
  auto* ip = vtkSMInputProperty::SafeDownCast(this->proxy()->GetProperty("CustomPatches"));
  if (!ip)
  {
    return;
  }
  QSet<int> drop;
  for (int idx : pipelineIndicesDescending)
  {
    drop.insert(idx);
  }
  std::vector<std::pair<vtkSMProxy*, unsigned int>> keep;
  const unsigned int n = ip->GetNumberOfProxies();
  keep.reserve(n);
  for (unsigned int i = 0; i < n; ++i)
  {
    if (!drop.contains(static_cast<int>(i)))
    {
      keep.emplace_back(ip->GetProxy(i), ip->GetOutputPortForConnection(i));
    }
  }
  ip->RemoveAllProxies();
  for (const auto& conn : keep)
  {
    if (conn.first)
    {
      ip->AddInputConnection(conn.first, conn.second);
    }
  }
  ip->Modified();
}

bool pqSHYXSelectionPatchTableWidget::computeShapePlacement(double center[3], double& radius) const
{
  center[0] = center[1] = center[2] = 0.0;
  radius = 1.0;

  pqView* view = pqActiveObjects::instance().activeView();
  auto* rview = qobject_cast<pqRenderView*>(view);
  vtkSMRenderViewProxy* rmp = rview ? rview->getRenderViewProxy() : nullptr;
  vtkRenderer* ren = rmp ? rmp->GetRenderer() : nullptr;
  vtkDataSet* ds = clientInputDataSet(this->proxy());

  bool haveCenter = false;
  if (ren && rmp)
  {
    int* size = ren->GetSize();
    if (size && size[0] > 0 && size[1] > 0)
    {
      const int displayPos[2] = { size[0] / 2, size[1] / 2 };
      double world[3] = { 0.0, 0.0, 0.0 };
      double normal[3] = { 0.0, 0.0, 0.0 };
      if (rmp->ConvertDisplayToPointOnSurface(displayPos, world, normal, /*snapOnMeshPoint=*/true) &&
        std::isfinite(world[0]) && std::isfinite(world[1]) && std::isfinite(world[2]))
      {
        center[0] = world[0];
        center[1] = world[1];
        center[2] = world[2];
        haveCenter = true;
      }
    }
  }

  if (!haveCenter && ds)
  {
    double b[6];
    ds->GetBounds(b);
    center[0] = 0.5 * (b[0] + b[1]);
    center[1] = 0.5 * (b[2] + b[3]);
    center[2] = 0.5 * (b[4] + b[5]);
    haveCenter = true;
  }

  if (ren && haveCenter)
  {
    int* size = ren->GetSize();
    if (size && size[0] > 0 && size[1] > 0)
    {
      const double pixelRadius = 0.15 * static_cast<double>(std::min(size[0], size[1]));
      ren->SetWorldPoint(center[0], center[1], center[2], 1.0);
      ren->WorldToDisplay();
      double displayCenter[3];
      ren->GetDisplayPoint(displayCenter);
      double worldA[4];
      vtkInteractorObserver::ComputeDisplayToWorld(
        ren, displayCenter[0], displayCenter[1], displayCenter[2], worldA);
      double worldB[4];
      vtkInteractorObserver::ComputeDisplayToWorld(
        ren, displayCenter[0] + pixelRadius, displayCenter[1], displayCenter[2], worldB);
      const double dx = worldA[0] - worldB[0];
      const double dy = worldA[1] - worldB[1];
      const double dz = worldA[2] - worldB[2];
      radius = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
  }

  if (!(radius > 0.0) || !std::isfinite(radius))
  {
    if (ds)
    {
      double b[6];
      ds->GetBounds(b);
      radius = 0.15 * std::max({ b[1] - b[0], b[3] - b[2], b[5] - b[4], 1e-6 });
    }
    else
    {
      radius = 1.0;
    }
  }
  return haveCenter && radius > 0.0;
}

void pqSHYXSelectionPatchTableWidget::addShapeRow(const QString& kind)
{
  double c[3] = { 0.0, 0.0, 0.0 };
  double r = 1.0;
  this->computeShapePlacement(c, r);

  PatchRow row;
  row.Name = QStringLiteral("geo_%1").arg(this->nextPartIndex());
  row.Kind = kind;
  if (kind == QLatin1String("box"))
  {
    const double b[6] = { c[0] - r, c[0] + r, c[1] - r, c[1] + r, c[2] - r, c[2] + r };
    double m[16] = { 0 };
    aabbToUnitCubeMatrix(b, m);
    row.Params = formatDoubles(m, 16);
  }
  else
  {
    const double s[4] = { c[0], c[1], c[2], r };
    row.Params = formatDoubles(s, 4);
  }
  this->appendRow(row,
    tr("Added %1 (%2). Check Show Interactable widget and keep this row selected to drag it.")
      .arg(row.Name)
      .arg(this->kindLabel(kind)));
  if (this->ShowInteractable && !this->ShowInteractable->isChecked())
  {
    this->ShowInteractable->setChecked(true);
  }
  else
  {
    this->syncShapeWidgets();
  }
}

void pqSHYXSelectionPatchTableWidget::onAddBox()
{
  this->addShapeRow(QStringLiteral("box"));
}

void pqSHYXSelectionPatchTableWidget::onAddSphere()
{
  this->addShapeRow(QStringLiteral("sphere"));
}

void pqSHYXSelectionPatchTableWidget::onRemoveSelected()
{
  const QModelIndexList selected = this->View->selectionModel()->selectedRows();
  if (selected.isEmpty())
  {
    this->setStatus(tr("Select table rows to remove."), true);
    return;
  }
  QList<int> rows;
  rows.reserve(selected.size());
  for (const QModelIndex& idx : selected)
  {
    rows.push_back(idx.row());
  }
  std::sort(rows.begin(), rows.end());
  QSet<int> selectedSet;
  for (int r : rows)
  {
    selectedSet.insert(r);
  }

  QList<int> pipeToDrop;
  int pipeIdx = 0;
  const int n = this->Model->rowCount();
  for (int r = 0; r < n; ++r)
  {
    const QString kind = this->normalizedKind(
      this->Model->item(r, kColIndex) ? this->Model->item(r, kColIndex)->data(kRoleKind).toString()
                                      : QString());
    if (kind == QLatin1String("pipeline"))
    {
      if (selectedSet.contains(r))
      {
        pipeToDrop.push_back(pipeIdx);
      }
      ++pipeIdx;
    }
  }
  std::sort(pipeToDrop.begin(), pipeToDrop.end(), std::greater<int>());
  this->removeCustomPatchConnections(pipeToDrop);

  std::sort(rows.begin(), rows.end(), std::greater<int>());
  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  for (int r : rows)
  {
    this->Model->removeRow(r);
  }
  this->writeBackProperty();
  this->syncShapeWidgets();
  if (this->ApplyOnAdd && this->ApplyOnAdd->isChecked())
  {
    this->setStatus(
      tr("Removed %1 row(s). Applying so remaining cells can be added again.").arg(rows.size()),
      false);
    this->applyOutputsIfChecked();
  }
  else
  {
    this->setStatus(
      tr("Removed %1 row(s). Remaining port not refreshed until Apply.").arg(rows.size()), false);
  }
}

QStringList pqSHYXSelectionPatchTableWidget::linesFromProperty(const QString& propertyName) const
{
  QStringList lines;
  if (propertyName.isEmpty())
  {
    return lines;
  }
  const QVariant v = this->property(propertyName.toUtf8().constData());
  const QString text = v.toString();
  if (text.isEmpty())
  {
    return lines;
  }
  return text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
}

void pqSHYXSelectionPatchTableWidget::rebuildFromProperty()
{
  const QStringList names = this->linesFromProperty(this->NamesPropertyName);
  const QStringList ids = this->linesFromProperty(this->CellIdsPropertyName);
  const QStringList kinds = this->linesFromProperty(this->KindsPropertyName);
  const QStringList params = this->linesFromProperty(this->ParamsPropertyName);
  int n = static_cast<int>(names.size());
  n = std::max(n, static_cast<int>(ids.size()));
  n = std::max(n, static_cast<int>(kinds.size()));
  n = std::max(n, static_cast<int>(params.size()));
  QList<PatchRow> rows;
  rows.reserve(n);
  for (int i = 0; i < n; ++i)
  {
    PatchRow row;
    row.Name = i < names.size() ? names[i] : QString();
    row.CellIds = i < ids.size() ? ids[i] : QString();
    row.Kind = this->normalizedKind(i < kinds.size() ? kinds[i] : QString());
    row.Params = i < params.size() ? params[i] : QString();
    if (row.Name.isEmpty() && row.CellIds.isEmpty() && row.Params.isEmpty() &&
      row.Kind == QLatin1String("selection"))
    {
      continue;
    }
    rows.push_back(row);
  }
  this->rebuildRows(rows);
  this->syncShapeWidgets();
  this->updateEyeIcons();
}

void pqSHYXSelectionPatchTableWidget::rebuildRows(const QList<PatchRow>& rows)
{
  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  this->Model->removeRows(0, this->Model->rowCount());
  QMap<QString, int> firstSeenIndex;
  int nextDs = 0;
  for (int i = 0; i < rows.size(); ++i)
  {
    const PatchRow& row = rows[i];
    QString outName = row.Name.trimmed();
    if (outName.isEmpty())
    {
      outName = QStringLiteral("geo_%1").arg(i);
    }
    if (!firstSeenIndex.contains(outName))
    {
      firstSeenIndex.insert(outName, nextDs++);
    }
    const int dsIndex = firstSeenIndex.value(outName);
    auto* visItem = new QStandardItem();
    visItem->setEditable(false);
    visItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
    visItem->setTextAlignment(Qt::AlignCenter);
    visItem->setIcon(eyeIcon(true));
    visItem->setData(true, kVisibleRole);
    visItem->setData(dsIndex, kDataSetIndexRole);
    visItem->setData(QStringLiteral("/patches/patch_%1").arg(dsIndex), kSelectorPathRole);
    visItem->setToolTip(tr("Show/hide this patch in the active view (same-named rows stay linked)"));
    auto* indexItem = new QStandardItem(QString::number(i));
    indexItem->setEditable(false);
    indexItem->setData(row.CellIds, kRoleCellIds);
    indexItem->setData(this->normalizedKind(row.Kind), kRoleKind);
    indexItem->setData(row.Params, kRoleParams);
    auto* nameItem = new QStandardItem(row.Name);
    auto* typeItem = new QStandardItem(this->kindLabel(row.Kind));
    typeItem->setEditable(false);
    auto* infoItem = new QStandardItem(this->infoText(row));
    infoItem->setEditable(false);
    this->Model->appendRow({ visItem, indexItem, nameItem, typeItem, infoItem });
  }
  this->updateEyeIcons();
}

QList<pqSHYXSelectionPatchTableWidget::PatchRow> pqSHYXSelectionPatchTableWidget::collectRows() const
{
  QList<PatchRow> rows;
  const int n = this->Model->rowCount();
  rows.reserve(n);
  for (int r = 0; r < n; ++r)
  {
    PatchRow row;
    row.Name = this->Model->item(r, kColName) ? this->Model->item(r, kColName)->text() : QString();
    if (auto* indexItem = this->Model->item(r, kColIndex))
    {
      row.CellIds = indexItem->data(kRoleCellIds).toString();
      row.Kind = this->normalizedKind(indexItem->data(kRoleKind).toString());
      row.Params = indexItem->data(kRoleParams).toString();
    }
    rows.push_back(row);
  }
  return rows;
}

void pqSHYXSelectionPatchTableWidget::writeBackProperty()
{
  QScopedValueRollback<bool> guard(this->UpdatingFromUI, true);
  const QList<PatchRow> rows = this->collectRows();
  QStringList names;
  QStringList ids;
  QStringList kinds;
  QStringList params;
  names.reserve(rows.size());
  ids.reserve(rows.size());
  kinds.reserve(rows.size());
  params.reserve(rows.size());
  for (const PatchRow& row : rows)
  {
    names << row.Name;
    ids << row.CellIds;
    kinds << this->normalizedKind(row.Kind);
    params << row.Params;
  }
  const QString nameText = names.join(QLatin1Char('\n'));
  const QString idText = ids.join(QLatin1Char('\n'));
  const QString kindText = kinds.join(QLatin1Char('\n'));
  const QString paramText = params.join(QLatin1Char('\n'));
  if (!this->NamesPropertyName.isEmpty())
  {
    this->setProperty(this->NamesPropertyName.toUtf8().constData(), nameText);
  }
  if (!this->CellIdsPropertyName.isEmpty())
  {
    this->setProperty(this->CellIdsPropertyName.toUtf8().constData(), idText);
  }
  if (!this->KindsPropertyName.isEmpty())
  {
    this->setProperty(this->KindsPropertyName.toUtf8().constData(), kindText);
  }
  if (!this->ParamsPropertyName.isEmpty())
  {
    this->setProperty(this->ParamsPropertyName.toUtf8().constData(), paramText);
  }
  if (this->Applying)
  {
    // Superclass::apply() copies the Qt properties through pqPropertyLinks.
    return;
  }
  Q_EMIT this->patchesChanged();
  // pqPropertiesPanel enables Apply only after changeAvailable, then changeFinished.
  // patchesChanged updates unchecked SM values; these two signals mark the proxy MODIFIED.
  Q_EMIT this->changeAvailable();
  Q_EMIT this->changeFinished();
}

void pqSHYXSelectionPatchTableWidget::ProcessShapeEvents(
  vtkObject* caller, unsigned long eid, void* clientdata, void* /*calldata*/)
{
  auto* self = reinterpret_cast<pqSHYXSelectionPatchTableWidget*>(clientdata);
  if (self)
  {
    self->onShapeWidgetEvent(caller, eid);
  }
}

void pqSHYXSelectionPatchTableWidget::destroyShapeWidgets()
{
  if (!this->ShapeHost)
  {
    return;
  }
  for (auto& entry : this->ShapeHost->Entries)
  {
    if (entry.Box)
    {
      entry.Box->RemoveObserver(this->ShapeHost->Observer);
      entry.Box->Off();
      entry.Box->SetInteractor(nullptr);
    }
    if (entry.Sphere)
    {
      entry.Sphere->RemoveObserver(this->ShapeHost->Observer);
      entry.Sphere->Off();
      entry.Sphere->SetInteractor(nullptr);
    }
  }
  this->ShapeHost->Entries.clear();
  this->ShapeHost->Interactor = nullptr;
  this->ShapeHost->Observer = nullptr;
}

void pqSHYXSelectionPatchTableWidget::setShapeWidgetsEnabled(bool on)
{
  if (!this->ShapeHost)
  {
    return;
  }
  for (auto& entry : this->ShapeHost->Entries)
  {
    if (entry.Box)
    {
      if (on)
      {
        entry.Box->On();
      }
      else
      {
        entry.Box->Off();
      }
    }
    if (entry.Sphere)
    {
      if (on)
      {
        entry.Sphere->On();
      }
      else
      {
        entry.Sphere->Off();
      }
    }
  }
  if (auto* view = qobject_cast<pqRenderView*>(pqActiveObjects::instance().activeView()))
  {
    view->render();
  }
}

void pqSHYXSelectionPatchTableWidget::onShapeWidgetEvent(vtkObject* caller, unsigned long eid)
{
  if (!this->ShapeHost || !caller)
  {
    return;
  }
  int shapeIndex = -1;
  for (int i = 0; i < static_cast<int>(this->ShapeHost->Entries.size()); ++i)
  {
    const auto& e = this->ShapeHost->Entries[static_cast<size_t>(i)];
    if (caller == e.Box.Get() || caller == e.Sphere.Get())
    {
      shapeIndex = i;
      break;
    }
  }
  if (shapeIndex < 0)
  {
    return;
  }

  const int tableRow = this->ShapeHost->Entries[static_cast<size_t>(shapeIndex)].TableRow;
  if (tableRow < 0 || tableRow >= this->Model->rowCount())
  {
    return;
  }

  auto* indexItem = this->Model->item(tableRow, kColIndex);
  auto* infoItem = this->Model->item(tableRow, kColInfo);
  if (!indexItem)
  {
    return;
  }

  const auto& entry = this->ShapeHost->Entries[static_cast<size_t>(shapeIndex)];
  QString params;
  if (entry.Kind == QLatin1String("box") && entry.Box)
  {
    auto* repr = vtkBoxRepresentation::SafeDownCast(entry.Box->GetRepresentation());
    if (!repr)
    {
      return;
    }
    params = paramsFromBoxRepresentation(repr);
  }
  else if (entry.Kind == QLatin1String("sphere") && entry.Sphere)
  {
    auto* repr = vtkSphereRepresentation::SafeDownCast(entry.Sphere->GetRepresentation());
    if (!repr)
    {
      return;
    }
    double s[4] = { 0, 0, 0, 0 };
    repr->GetCenter(s);
    s[3] = repr->GetRadius();
    params = formatDoubles(s, 4);
  }
  else
  {
    return;
  }

  this->InteractingShape = (eid == vtkCommand::InteractionEvent);
  {
    QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
    indexItem->setData(params, kRoleParams);
    PatchRow tmp;
    tmp.Kind = entry.Kind;
    tmp.Params = params;
    if (infoItem)
    {
      infoItem->setText(this->infoText(tmp));
    }
  }
  this->writeBackProperty();

  if (eid == vtkCommand::EndInteractionEvent)
  {
    this->InteractingShape = false;
    if (this->ApplyOnAdd && this->ApplyOnAdd->isChecked())
    {
      this->applyOutputsIfChecked();
    }
  }
}

void pqSHYXSelectionPatchTableWidget::syncShapeWidgets()
{
  if (this->InteractingShape)
  {
    return;
  }

  pqView* view = pqActiveObjects::instance().activeView();
  auto* rview = qobject_cast<pqRenderView*>(view);
  vtkSMRenderViewProxy* rmp = rview ? rview->getRenderViewProxy() : nullptr;
  vtkRenderWindowInteractor* iren = rmp ? rmp->GetInteractor() : nullptr;

  QList<PatchRow> shapes;
  QList<int> shapeRows;
  if (this->shapeWidgetsWanted())
  {
    shapeRows = this->selectedShapeRowIndices();
    const QList<PatchRow> all = this->collectRows();
    for (int r : shapeRows)
    {
      if (r < 0 || r >= all.size())
      {
        continue;
      }
      PatchRow copy = all[r];
      copy.Kind = this->normalizedKind(copy.Kind);
      shapes.push_back(copy);
    }
  }

  if (!this->shapeWidgetsWanted() || !iren || shapes.isEmpty())
  {
    this->destroyShapeWidgets();
    if (rview)
    {
      rview->render();
    }
    return;
  }

  if (!this->ShapeHost)
  {
    this->ShapeHost.reset(new pqSHYXSelectionPatchShapeHost());
  }

  bool kindsMatch = this->ShapeHost->Interactor == iren &&
    this->ShapeHost->Entries.size() == static_cast<size_t>(shapes.size());
  if (kindsMatch)
  {
    for (int i = 0; i < shapes.size(); ++i)
    {
      const auto& entry = this->ShapeHost->Entries[static_cast<size_t>(i)];
      if (entry.Kind != shapes[i].Kind || entry.TableRow != shapeRows[i])
      {
        kindsMatch = false;
        break;
      }
    }
  }

  if (!kindsMatch)
  {
    this->destroyShapeWidgets();
    this->ShapeHost.reset(new pqSHYXSelectionPatchShapeHost());
    this->ShapeHost->Observer = vtkSmartPointer<vtkCallbackCommand>::New();
    this->ShapeHost->Observer->SetClientData(this);
    this->ShapeHost->Observer->SetCallback(&pqSHYXSelectionPatchTableWidget::ProcessShapeEvents);
    this->ShapeHost->Interactor = iren;

    for (int i = 0; i < shapes.size(); ++i)
    {
      const PatchRow& row = shapes[i];
      pqSHYXSelectionPatchShapeHost::Entry entry;
      entry.Kind = row.Kind;
      entry.TableRow = shapeRows[i];
      if (row.Kind == QLatin1String("box"))
      {
        auto box = vtkSmartPointer<vtkBoxWidget2>::New();
        auto repr = vtkSmartPointer<vtkSHYXPatchBoxRepresentation>::New();
        styleBoxRepresentation(repr);
        box->SetRepresentation(repr);
        box->SetInteractor(iren);
        box->RotationEnabledOn();
        box->TranslationEnabledOn();
        box->ScalingEnabledOn();
        box->MoveFacesEnabledOn();
        box->SetPriority(1.0);
        box->AddObserver(vtkCommand::InteractionEvent, this->ShapeHost->Observer, 1.0);
        box->AddObserver(vtkCommand::EndInteractionEvent, this->ShapeHost->Observer, 1.0);
        applyParamsToBoxRepresentation(repr, row.Params);
        box->On();
        entry.Box = box;
      }
      else
      {
        auto sphere = vtkSmartPointer<vtkSphereWidget2>::New();
        auto repr = vtkSmartPointer<vtkSphereRepresentation>::New();
        styleSphereRepresentation(repr);
        sphere->SetRepresentation(repr);
        sphere->SetInteractor(iren);
        sphere->SetPriority(1.0);
        sphere->AddObserver(vtkCommand::InteractionEvent, this->ShapeHost->Observer, 1.0);
        sphere->AddObserver(vtkCommand::EndInteractionEvent, this->ShapeHost->Observer, 1.0);
        double s[4] = { 0, 0, 0, 1 };
        parseDoubles(row.Params, s, 4);
        if (!(s[3] > 0.0))
        {
          s[3] = 1.0;
        }
        double place[6] = { s[0] - s[3], s[0] + s[3], s[1] - s[3], s[1] + s[3], s[2] - s[3],
          s[2] + s[3] };
        repr->PlaceWidget(place);
        repr->SetCenter(s[0], s[1], s[2]);
        repr->SetRadius(s[3]);
        sphere->On();
        entry.Sphere = sphere;
      }
      this->ShapeHost->Entries.push_back(entry);
    }
  }
  else
  {
    for (int i = 0; i < shapes.size(); ++i)
    {
      auto& entry = this->ShapeHost->Entries[static_cast<size_t>(i)];
      if (entry.Kind == QLatin1String("box") && entry.Box)
      {
        if (auto* repr = vtkBoxRepresentation::SafeDownCast(entry.Box->GetRepresentation()))
        {
          applyParamsToBoxRepresentation(repr, shapes[i].Params);
        }
        entry.Box->SetInteractor(iren);
        entry.Box->On();
      }
      else if (entry.Sphere)
      {
        if (auto* repr = vtkSphereRepresentation::SafeDownCast(entry.Sphere->GetRepresentation()))
        {
          double s[4] = { 0, 0, 0, 1 };
          if (parseDoubles(shapes[i].Params, s, 4) && s[3] > 0.0)
          {
            repr->SetCenter(s[0], s[1], s[2]);
            repr->SetRadius(s[3]);
          }
        }
        entry.Sphere->SetInteractor(iren);
        entry.Sphere->On();
      }
    }
  }

  if (rview)
  {
    rview->render();
  }
}

void pqSHYXSelectionPatchTableWidget::onViewClicked(const QModelIndex& index)
{
  if (!index.isValid() || index.column() != kColVisibility)
  {
    return;
  }
  this->toggleRowVisibility(index.row());
}

void pqSHYXSelectionPatchTableWidget::onHeaderSectionClicked(int logicalIndex)
{
  if (logicalIndex != kColVisibility)
  {
    return;
  }
  this->toggleAllVisibility();
}

void pqSHYXSelectionPatchTableWidget::onBlockVisibilityModified()
{
  if (this->UpdatingBlockVisibility)
  {
    return;
  }
  this->updateEyeIcons();
}

void pqSHYXSelectionPatchTableWidget::onActiveViewOrRepresentationChanged()
{
  this->connectBlockVisibilityObserver();
}

void pqSHYXSelectionPatchTableWidget::disconnectBlockVisibilityObserver()
{
  if (this->BlockVisibilityVTKConnect)
  {
    this->BlockVisibilityVTKConnect->Disconnect();
  }
  this->ObservedRepresentation = nullptr;
}

void pqSHYXSelectionPatchTableWidget::connectBlockVisibilityObserver()
{
  pqDataRepresentation* repr = this->currentRepresentation();
  if (repr == this->ObservedRepresentation)
  {
    this->updateEyeIcons();
    return;
  }

  this->disconnectBlockVisibilityObserver();
  this->ObservedRepresentation = repr;
  if (!repr || !this->BlockVisibilityVTKConnect)
  {
    this->updateEyeIcons();
    return;
  }

  if (vtkSMStringVectorProperty* prop = this->visibilityProperty(repr))
  {
    this->BlockVisibilityVTKConnect->Connect(
      prop, vtkCommand::ModifiedEvent, this, SLOT(onBlockVisibilityModified()));
  }
  this->updateEyeIcons();
}

pqDataRepresentation* pqSHYXSelectionPatchTableWidget::currentRepresentation() const
{
  auto* smm = pqApplicationCore::instance()->getServerManagerModel();
  auto* src = smm ? smm->findItem<pqPipelineSource*>(this->proxy()) : nullptr;
  if (!src)
  {
    return nullptr;
  }

  pqView* view = this->view();
  if (!view)
  {
    view = pqActiveObjects::instance().activeView();
  }
  if (view)
  {
    if (auto* repr = src->getRepresentation(0, view))
    {
      return repr;
    }
  }

  const QList<pqView*> views = src->getViews();
  for (pqView* candidate : views)
  {
    if (auto* repr = src->getRepresentation(0, candidate))
    {
      return repr;
    }
  }
  return nullptr;
}

vtkSMStringVectorProperty* pqSHYXSelectionPatchTableWidget::visibilityProperty(
  pqDataRepresentation* repr) const
{
  return blockVisibilityProperty(repr ? repr->getProxy() : nullptr);
}

vtkDataAssembly* pqSHYXSelectionPatchTableWidget::activeAssembly(pqDataRepresentation* repr) const
{
  if (!repr)
  {
    return nullptr;
  }

  vtkPVDataInformation* info = repr->getInputDataInformation();
  if (!info)
  {
    auto* source = vtkSMSourceProxy::SafeDownCast(this->proxy());
    info = source ? source->GetDataInformation(0) : nullptr;
  }
  if (!info)
  {
    return nullptr;
  }

  vtkSMProxy* reprProxy = repr->getProxy();
  const char* assemblyName = nullptr;
  if (reprProxy && reprProxy->GetProperty("Assembly"))
  {
    assemblyName = vtkSMPropertyHelper(reprProxy, "Assembly").GetAsString();
  }
  if (assemblyName && assemblyName[0] != '\0')
  {
    if (vtkDataAssembly* named = info->GetDataAssembly(assemblyName))
    {
      return named;
    }
  }
  return info->GetDataAssembly();
}

QString pqSHYXSelectionPatchTableWidget::outputNameForRow(int row) const
{
  if (!this->Model || row < 0 || row >= this->Model->rowCount())
  {
    return {};
  }
  const QString name = this->Model->item(row, kColName)
    ? this->Model->item(row, kColName)->text().trimmed()
    : QString();
  if (!name.isEmpty())
  {
    return name;
  }
  return QStringLiteral("geo_%1").arg(row);
}

QString pqSHYXSelectionPatchTableWidget::selectorForRow(int row) const
{
  if (!this->Model)
  {
    return {};
  }
  auto* visItem = this->Model->item(row, kColVisibility);
  const QString stored = visItem ? visItem->data(kSelectorPathRole).toString() : QString();
  auto* repr = this->currentRepresentation();
  vtkDataAssembly* assembly = this->activeAssembly(repr);
  if (!assembly)
  {
    return stored;
  }

  const QString label = this->outputNameForRow(row);
  const int byLabel = findNodeByLabel(assembly, vtkDataAssembly::GetRootNode(), label);
  if (byLabel >= 0)
  {
    return QString::fromStdString(assembly->GetNodePath(byLabel));
  }

  if (!stored.isEmpty() && assembly->GetFirstNodeByPath(stored.toUtf8().constData()) >= 0)
  {
    return stored;
  }

  const int dsIndex = visItem ? visItem->data(kDataSetIndexRole).toInt() : -1;
  if (dsIndex >= 0)
  {
    const int node =
      findNodeByDataSetIndex(assembly, vtkDataAssembly::GetRootNode(), static_cast<unsigned int>(dsIndex));
    if (node >= 0)
    {
      return QString::fromStdString(assembly->GetNodePath(node));
    }
  }
  return stored;
}

void pqSHYXSelectionPatchTableWidget::updateEyeIcons()
{
  if (!this->Model)
  {
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingFromProperty, true);
  auto* repr = this->currentRepresentation();
  vtkDataAssembly* assembly = this->activeAssembly(repr);
  vtkSMStringVectorProperty* prop = this->visibilityProperty(repr);

  pqDataAssemblyTreeModel treeModel;
  bool haveTree = false;
  if (assembly)
  {
    treeModel.setUserCheckable(true);
    treeModel.setDataAssembly(assembly);
    treeModel.setCheckedNodes(checkedSelectorsFromProperty(prop, assembly));
    haveTree = true;
  }

  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* item = this->Model->item(row, kColVisibility);
    if (!item)
    {
      continue;
    }

    bool visible = true;
    if (haveTree)
    {
      const QString path = this->selectorForRow(row);
      if (!path.isEmpty())
      {
        const std::vector<int> nodes = assembly->SelectNodes({ path.toStdString() });
        if (!nodes.empty())
        {
          const QModelIndex idx = treeModel.index(nodes.front());
          visible = treeModel.data(idx, Qt::CheckStateRole).toInt() != Qt::Unchecked;
        }
      }
    }

    item->setIcon(eyeIcon(visible));
    item->setData(visible, kVisibleRole);
    item->setToolTip(visible ? tr("Hide this patch in the active view")
                             : tr("Show this patch in the active view"));
  }

  if (this->View)
  {
    this->View->viewport()->repaint();
    if (this->View->header())
    {
      const Qt::CheckState headerState = this->headerVisibilityState();
      this->Model->setHeaderData(
        kColVisibility, Qt::Horizontal, eyeIcon(headerState), Qt::DecorationRole);
      QString tip;
      switch (headerState)
      {
        case Qt::Checked:
          tip = tr("Hide all listed patches in the active view");
          break;
        case Qt::Unchecked:
          tip = tr("Show all listed patches in the active view");
          break;
        default:
          tip = tr("Some patches are hidden. Click to show all listed patches");
          break;
      }
      this->Model->setHeaderData(kColVisibility, Qt::Horizontal, tip, Qt::ToolTipRole);
      this->View->header()->viewport()->repaint();
    }
  }
}

void pqSHYXSelectionPatchTableWidget::toggleRowVisibility(int row)
{
  if (!this->Model || row < 0 || row >= this->Model->rowCount())
  {
    return;
  }

  auto* visItem = this->Model->item(row, kColVisibility);
  const bool currentlyVisible = visItem ? visItem->data(kVisibleRole).toBool() : true;
  const QString name = this->outputNameForRow(row);
  QList<int> rows;
  for (int r = 0; r < this->Model->rowCount(); ++r)
  {
    if (this->outputNameForRow(r) == name)
    {
      rows.push_back(r);
    }
  }
  if (rows.isEmpty())
  {
    rows.push_back(row);
  }
  this->setBlocksVisible(rows, !currentlyVisible);
}

Qt::CheckState pqSHYXSelectionPatchTableWidget::headerVisibilityState() const
{
  if (!this->Model || this->Model->rowCount() == 0)
  {
    return Qt::Checked;
  }

  int visibleCount = 0;
  int totalCount = 0;
  for (int row = 0; row < this->Model->rowCount(); ++row)
  {
    auto* item = this->Model->item(row, kColVisibility);
    if (!item)
    {
      continue;
    }
    ++totalCount;
    if (item->data(kVisibleRole).toBool())
    {
      ++visibleCount;
    }
  }

  if (totalCount == 0 || visibleCount == totalCount)
  {
    return Qt::Checked;
  }
  if (visibleCount == 0)
  {
    return Qt::Unchecked;
  }
  return Qt::PartiallyChecked;
}

void pqSHYXSelectionPatchTableWidget::toggleAllVisibility()
{
  if (!this->Model || this->Model->rowCount() == 0)
  {
    return;
  }

  if (this->headerVisibilityState() == Qt::Checked)
  {
    QList<int> rows;
    rows.reserve(this->Model->rowCount());
    for (int row = 0; row < this->Model->rowCount(); ++row)
    {
      rows.push_back(row);
    }
    this->setBlocksVisible(rows, false);
    return;
  }

  auto* repr = this->currentRepresentation();
  vtkSMProxy* reprProxy = repr ? repr->getProxy() : nullptr;
  vtkSMStringVectorProperty* prop = this->visibilityProperty(repr);
  if (!repr || !reprProxy || !prop)
  {
    this->setStatus(tr("No display of Added patches in the active view to show/hide."), true);
    return;
  }

  QScopedValueRollback<bool> guard(this->UpdatingBlockVisibility, true);
  SM_SCOPED_TRACE(PropertiesModified).arg("proxy", reprProxy);
  BEGIN_UNDO_SET(tr("Show All Blocks"));
  prop->SetElements(std::vector<std::string>({ "/" }));
  reprProxy->UpdateVTKObjects();
  END_UNDO_SET();
  repr->renderViewEventually();
  this->updateEyeIcons();
}

void pqSHYXSelectionPatchTableWidget::setBlocksVisible(const QList<int>& rows, bool visible)
{
  auto* repr = this->currentRepresentation();
  vtkSMProxy* reprProxy = repr ? repr->getProxy() : nullptr;
  vtkSMStringVectorProperty* prop = this->visibilityProperty(repr);
  vtkDataAssembly* assembly = this->activeAssembly(repr);
  if (!repr || !reprProxy || !prop || !assembly)
  {
    this->setStatus(tr("Apply first so Added patches exist, then use the eye to show/hide."), true);
    return;
  }

  QList<int> nodeIds;
  for (int row : rows)
  {
    const QString path = this->selectorForRow(row);
    if (path.isEmpty())
    {
      continue;
    }
    const std::vector<int> nodes = assembly->SelectNodes({ path.toStdString() });
    for (int id : nodes)
    {
      nodeIds.push_back(id);
    }
  }
  if (nodeIds.isEmpty())
  {
    this->setStatus(tr("This patch is not in the current output yet. Apply first."), true);
    return;
  }

  pqDataAssemblyTreeModel treeModel;
  treeModel.setUserCheckable(true);
  treeModel.setDataAssembly(assembly);
  treeModel.setCheckedNodes(checkedSelectorsFromProperty(prop, assembly));

  const QModelIndexList indexes = treeModel.index(nodeIds);
  for (const QModelIndex& idx : indexes)
  {
    treeModel.setData(idx, visible ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
  }

  const QStringList checkedNodes = treeModel.checkedNodes();
  std::vector<std::string> values(static_cast<size_t>(checkedNodes.size()));
  for (int i = 0; i < checkedNodes.size(); ++i)
  {
    values[static_cast<size_t>(i)] = checkedNodes[i].toStdString();
  }

  QScopedValueRollback<bool> guard(this->UpdatingBlockVisibility, true);
  SM_SCOPED_TRACE(PropertiesModified).arg("proxy", reprProxy);
  BEGIN_UNDO_SET(visible ? tr("Show Block") : tr("Hide Block"));
  prop->SetElements(values);
  reprProxy->UpdateVTKObjects();
  END_UNDO_SET();
  repr->renderViewEventually();
  this->updateEyeIcons();
}
