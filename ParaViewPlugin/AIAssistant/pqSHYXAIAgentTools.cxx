#include "pqSHYXAIAgentTools.h"

#include "pqSHYXAIMemory.h"
#include "pqSHYXAIOutputLog.h"
#include "pqSHYXGrowSelectionWithSimilarController.h"
#include "pqSHYXProximityGapSelectionController.h"

#include "pqActiveObjects.h"
#include "pqAnimationManager.h"
#include "pqAnimationScene.h"
#include "pqApplicationCore.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineFilter.h"
#include "pqPipelineSource.h"
#include "pqScalarsToColors.h"
#include "pqSelectionManager.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkDataAssembly.h"
#include "vtkPVArrayInformation.h"
#include "vtkPVDataInformation.h"
#include "vtkPVDataSetAttributesInformation.h"
#include "vtkPVProxyDefinitionIterator.h"
#include "vtkPVXMLElement.h"
#include "vtkSMDocumentation.h"
#include "vtkSMEnumerationDomain.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMPropertyIterator.h"
#include "vtkSMProxy.h"
#include "vtkSMProxyDefinitionManager.h"
#include "vtkSMProxyListDomain.h"
#include "vtkSMProxyManager.h"
#include "vtkSMProxyProperty.h"
#include "vtkCamera.h"
#include "vtkMath.h"
#include "vtkRenderer.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMRepresentationProxy.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSmartPointer.h"

#include <QColor>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QSize>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kMaxPropLines = 80;
constexpr int kMaxPropElems = 8;
constexpr int kMaxSelIds = 64;
constexpr int kMaxFilterHits = 40;
constexpr int kMaxBlockNodes = 80;
constexpr int kMaxTimeSteps = 40;
constexpr int kMaxPickPoints = 32;

QString formatHelperValues(vtkSMProperty* prop, int maxElems);
QString propTypeShort(vtkSMProperty* prop);

QJsonObject fn(const char* name, const char* desc, const QJsonObject& properties = QJsonObject(),
  const QJsonArray& required = QJsonArray())
{
  QJsonObject parameters;
  parameters.insert(QStringLiteral("type"), QStringLiteral("object"));
  parameters.insert(QStringLiteral("properties"), properties);
  if (!required.isEmpty())
  {
    parameters.insert(QStringLiteral("required"), required);
  }
  QJsonObject function;
  function.insert(QStringLiteral("name"), QString::fromUtf8(name));
  function.insert(QStringLiteral("description"), QString::fromUtf8(desc));
  function.insert(QStringLiteral("parameters"), parameters);
  QJsonObject tool;
  tool.insert(QStringLiteral("type"), QStringLiteral("function"));
  tool.insert(QStringLiteral("function"), function);
  return tool;
}

QJsonObject strArg(const char* desc)
{
  return QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") },
    { QStringLiteral("description"), QString::fromUtf8(desc) } };
}

QJsonObject numArg(const char* desc)
{
  return QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") },
    { QStringLiteral("description"), QString::fromUtf8(desc) } };
}

QJsonObject intArg(const char* desc)
{
  return QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") },
    { QStringLiteral("description"), QString::fromUtf8(desc) } };
}

QJsonObject boolArg(const char* desc)
{
  return QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") },
    { QStringLiteral("description"), QString::fromUtf8(desc) } };
}

int jsonInt(const QJsonObject& args, const char* key, int def = 0)
{
  const QJsonValue v = args.value(QLatin1String(key));
  if (v.isDouble())
  {
    return v.toInt(def);
  }
  if (v.isString())
  {
    bool ok = false;
    const int n = v.toString().toInt(&ok);
    return ok ? n : def;
  }
  return def;
}

double jsonDouble(const QJsonObject& args, const char* key, double def = 0.0)
{
  const QJsonValue v = args.value(QLatin1String(key));
  if (v.isDouble())
  {
    return v.toDouble(def);
  }
  if (v.isString())
  {
    bool ok = false;
    const double n = v.toString().toDouble(&ok);
    return ok ? n : def;
  }
  return def;
}

QString collapseWs(const QString& text, int maxChars)
{
  QString t = text.simplified();
  if (maxChars > 0 && t.size() > maxChars)
  {
    t = t.left(maxChars - 3) + QStringLiteral("...");
  }
  return t;
}

QString fileNameOf(vtkSMProxy* proxy)
{
  if (!proxy)
  {
    return {};
  }
  if (vtkSMProperty* p = proxy->GetProperty("FileName"))
  {
    const char* s = vtkSMPropertyHelper(p).GetAsString();
    if (s && s[0])
    {
      return QString::fromUtf8(s);
    }
  }
  if (vtkSMProperty* p = proxy->GetProperty("FileNames"))
  {
    vtkSMPropertyHelper h(p);
    const unsigned int n = h.GetNumberOfElements();
    QStringList parts;
    for (unsigned int i = 0; i < n && i < 4; ++i)
    {
      const char* s = h.GetAsString(i);
      if (s && s[0])
      {
        parts << QString::fromUtf8(s);
      }
    }
    return parts.join(QStringLiteral("; "));
  }
  return {};
}

void appendArrays(QString& out, const char* attr, vtkPVDataSetAttributesInformation* info)
{
  if (!info)
  {
    return;
  }
  const int n = info->GetNumberOfArrays();
  if (n <= 0)
  {
    return;
  }
  out += QStringLiteral("  %1 arrays:\n").arg(QString::fromUtf8(attr));
  const int shown = n < 16 ? n : 16;
  for (int i = 0; i < shown; ++i)
  {
    vtkPVArrayInformation* ai = info->GetArrayInformation(i);
    if (!ai || !ai->GetName())
    {
      continue;
    }
    out += QStringLiteral("    %1 comps=%2")
             .arg(QString::fromUtf8(ai->GetName()))
             .arg(ai->GetNumberOfComponents());
    const int ncomp = std::min(ai->GetNumberOfComponents(), 3);
    for (int c = 0; c < ncomp; ++c)
    {
      double r[2] = { 0, 0 };
      ai->GetComponentRange(c, r);
      out += QStringLiteral(" range%1=[%2, %3]").arg(c).arg(r[0]).arg(r[1]);
    }
    out += QLatin1Char('\n');
  }
  if (n > shown)
  {
    out += QStringLiteral("    ...\n");
  }
}

QString describePort(vtkSMSourceProxy* src, int port)
{
  if (!src)
  {
    return QStringLiteral("(none)\n");
  }
  QString out;
  const char* label = src->GetXMLLabel();
  const char* portName = src->GetOutputPortName(static_cast<unsigned int>(port));
  out += QStringLiteral("%1 (%2)  port=%3%4\n")
           .arg(QString::fromUtf8(label && label[0] ? label : src->GetXMLName()))
           .arg(QString::fromUtf8(src->GetXMLName()))
           .arg(port)
           .arg(portName && portName[0]
               ? QStringLiteral(" name=%1").arg(QString::fromUtf8(portName))
               : QString());
  vtkPVDataInformation* di = src->GetDataInformation(port);
  if (!di)
  {
    return out + QStringLiteral("  (no data info)\n");
  }
  const char* cls = di->GetDataClassName();
  const char* composite = di->GetCompositeDataClassName();
  out += QStringLiteral("  type=%1").arg(QString::fromUtf8(cls ? cls : "?"));
  if (composite && composite[0])
  {
    out += QStringLiteral("  composite=%1").arg(QString::fromUtf8(composite));
  }
  out += QStringLiteral("  points=%1  cells=%2\n")
           .arg(static_cast<qint64>(di->GetNumberOfPoints()))
           .arg(static_cast<qint64>(di->GetNumberOfCells()));
  double b[6] = { 0, 0, 0, 0, 0, 0 };
  di->GetBounds(b);
  out += QStringLiteral("  bounds=[%1, %2] [%3, %4] [%5, %6]\n")
           .arg(b[0])
           .arg(b[1])
           .arg(b[2])
           .arg(b[3])
           .arg(b[4])
           .arg(b[5]);
  appendArrays(out, "point", di->GetPointDataInformation());
  appendArrays(out, "cell", di->GetCellDataInformation());
  appendArrays(out, "field", di->GetFieldDataInformation());
  return out;
}

pqServerManagerModel* smModel()
{
  pqApplicationCore* core = pqApplicationCore::instance();
  return core ? core->getServerManagerModel() : nullptr;
}

pqPipelineSource* findSourceByName(const QString& name)
{
  pqPipelineSource* active = pqActiveObjects::instance().activeSource();
  const QString want = name.trimmed();
  if (want.isEmpty())
  {
    return active;
  }
  auto* sm = smModel();
  if (!sm)
  {
    return nullptr;
  }
  pqPipelineSource* ci = nullptr;
  const QList<pqPipelineSource*> all = sm->findItems<pqPipelineSource*>();
  for (pqPipelineSource* s : all)
  {
    if (!s)
    {
      continue;
    }
    if (s->getSMName() == want)
    {
      return s;
    }
    if (!ci && s->getSMName().compare(want, Qt::CaseInsensitive) == 0)
    {
      ci = s;
    }
  }
  return ci;
}

QString visibilityOf(pqPipelineSource* src, int port)
{
  pqView* view = pqActiveObjects::instance().activeView();
  if (!src || !view)
  {
    return QStringLiteral("?");
  }
  pqDataRepresentation* repr = src->getRepresentation(port, view);
  if (!repr)
  {
    return QStringLiteral("no-repr");
  }
  return repr->isVisible() ? QStringLiteral("visible") : QStringLiteral("hidden");
}

QString pipelineTree()
{
  auto* sm = smModel();
  if (!sm)
  {
    return QStringLiteral("No server manager.");
  }
  QString out;
  const QList<pqPipelineSource*> sources = sm->findItems<pqPipelineSource*>();
  out += QStringLiteral("Pipeline (%1 items):\n").arg(sources.size());
  for (pqPipelineSource* src : sources)
  {
    if (!src || !src->getProxy())
    {
      continue;
    }
    vtkSMSourceProxy* sp = src->getSourceProxy();
    const bool active = (src == pqActiveObjects::instance().activeSource());
    out += QStringLiteral("- %1  xml=%2%3\n")
             .arg(src->getSMName())
             .arg(QString::fromUtf8(src->getProxy()->GetXMLName()))
             .arg(active ? QStringLiteral("  (active)") : QString());
    const QString fn = fileNameOf(src->getProxy());
    if (!fn.isEmpty())
    {
      out += QStringLiteral("    file=%1\n").arg(fn);
    }
    auto* filter = qobject_cast<pqPipelineFilter*>(src);
    if (filter && filter->getInputCount() > 0)
    {
      QStringList ins;
      const QList<pqOutputPort*> ports = filter->getInputs();
      for (pqOutputPort* p : ports)
      {
        if (p && p->getSource())
        {
          ins << p->getSource()->getSMName();
        }
      }
      if (!ins.isEmpty())
      {
        out += QStringLiteral("    inputs: %1\n").arg(ins.join(QStringLiteral(", ")));
      }
    }
    const int nports = src->getNumberOfOutputPorts();
    for (int p = 0; p < nports; ++p)
    {
      pqOutputPort* op = src->getOutputPort(p);
      const QString pname = op ? op->getPortName() : QString();
      vtkPVDataInformation* di = (sp && nports > 0) ? sp->GetDataInformation(p) : nullptr;
      const char* cls = di ? di->GetDataClassName() : nullptr;
      const char* composite = di ? di->GetCompositeDataClassName() : nullptr;
      QString type = QString::fromUtf8(cls && cls[0] ? cls : "?");
      if (composite && composite[0])
      {
        type += QStringLiteral("/%1").arg(QString::fromUtf8(composite));
      }
      const qint64 npts = di ? static_cast<qint64>(di->GetNumberOfPoints()) : 0;
      const qint64 ncells = di ? static_cast<qint64>(di->GetNumberOfCells()) : 0;
      out += QStringLiteral("    port %1%2  %3  points=%4 cells=%5  %6\n")
               .arg(p)
               .arg(pname.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(pname))
               .arg(type)
               .arg(npts)
               .arg(ncells)
               .arg(visibilityOf(src, p));
    }
  }
  return out;
}

QString sourceData(const QString& name, int port)
{
  pqPipelineSource* src = findSourceByName(name);
  if (!src || !src->getSourceProxy())
  {
    return name.trimmed().isEmpty() ? QStringLiteral("No active source.")
                                    : QStringLiteral("Source not found: %1").arg(name.trimmed());
  }
  const int nports = src->getNumberOfOutputPorts();
  if (port < 0 || (nports > 0 && port >= nports))
  {
    return QStringLiteral("Invalid port %1 for %2 (nports=%3)")
      .arg(port)
      .arg(src->getSMName())
      .arg(nports);
  }
  QString out = QStringLiteral("Source: %1  nports=%2  vis=%3\n")
                  .arg(src->getSMName())
                  .arg(nports)
                  .arg(visibilityOf(src, port));
  out += describePort(src->getSourceProxy(), port);
  const QString fn = fileNameOf(src->getProxy());
  if (!fn.isEmpty())
  {
    out += QStringLiteral("  file=%1\n").arg(fn);
  }
  return out;
}

QString enumOrInt(vtkSMProxy* p, const char* name)
{
  vtkSMProperty* prop = p ? p->GetProperty(name) : nullptr;
  if (!prop)
  {
    return {};
  }
  vtkSMPropertyHelper h(prop);
  if (h.GetNumberOfElements() == 0)
  {
    return {};
  }
  const int v = h.GetAsInt();
  if (auto* en = prop->FindDomain<vtkSMEnumerationDomain>())
  {
    if (const char* t = en->GetEntryTextForValue(v))
    {
      return QString::fromUtf8(t);
    }
  }
  return QString::number(v);
}

QList<vtkSMProxy*> selectionSourceNodes(vtkSMSourceProxy* selIn)
{
  QList<vtkSMProxy*> nodes;
  if (!selIn)
  {
    return nodes;
  }
  if (selIn->GetProperty("Input"))
  {
    vtkSMPropertyHelper inputs(selIn, "Input", /*quiet=*/true);
    const unsigned int n = inputs.GetNumberOfElements();
    for (unsigned int i = 0; i < n; ++i)
    {
      if (vtkSMProxy* child = inputs.GetAsProxy(i))
      {
        nodes.append(child);
      }
    }
  }
  if (nodes.isEmpty())
  {
    nodes.append(selIn);
  }
  return nodes;
}

void appendSelectionSourceSummary(QString& out, vtkSMProxy* src, int index)
{
  if (!src)
  {
    return;
  }
  const char* xml = src->GetXMLName();
  out += QStringLiteral("  [%1] xml=%2").arg(index).arg(QString::fromUtf8(xml ? xml : "?"));
  const QString field = enumOrInt(src, "FieldType");
  if (!field.isEmpty())
  {
    out += QStringLiteral("  field=%1").arg(field);
  }
  if (vtkSMProperty* cc = src->GetProperty("ContainingCells"))
  {
    vtkSMPropertyHelper h(cc);
    if (h.GetNumberOfElements() && h.GetAsInt())
    {
      out += QStringLiteral("  ContainingCells=1");
    }
  }
  if (vtkSMProperty* ids = src->GetProperty("IDs"))
  {
    const unsigned int n = vtkSMPropertyHelper(ids).GetNumberOfElements();
    unsigned int stride = 1;
    if (xml && QString::fromUtf8(xml).contains(QLatin1String("Composite")))
    {
      stride = 3;
    }
    else if (xml && QString::fromUtf8(xml).contains(QLatin1String("Hierarchical")))
    {
      stride = 3;
    }
    else if (xml && QString::fromUtf8(xml) == QLatin1String("IDSelectionSource"))
    {
      stride = 2;
    }
    out += QStringLiteral("  IDs=%1").arg(stride > 1 ? n / stride : n);
  }
  if (vtkSMProperty* qs = src->GetProperty("QueryString"))
  {
    const char* s = vtkSMPropertyHelper(qs).GetAsString();
    if (s && s[0])
    {
      out += QStringLiteral("  query=%1").arg(QString::fromUtf8(s));
    }
  }
  out += QLatin1Char('\n');
}

QString selectionInfo()
{
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqSelectionManager* sel = core ? core->selectionManager() : nullptr;
  if (!sel || !sel->hasActiveSelection())
  {
    return QStringLiteral("No active selection.");
  }
  pqOutputPort* port = sel->getSelectedPort();
  if (!port || !port->getSource())
  {
    return QStringLiteral("No selected port.");
  }
  QString out = QStringLiteral("Selected source: %1  port=%2\n")
                  .arg(port->getSource()->getSMName())
                  .arg(port->getPortNumber());
  vtkSMSourceProxy* selIn = port->getSelectionInput();
  if (!selIn)
  {
    return out + QStringLiteral("Selection proxy: (none)\n");
  }
  out += QStringLiteral("Selection xml=%1\n").arg(QString::fromUtf8(selIn->GetXMLName()));
  if (vtkSMProperty* expr = selIn->GetProperty("Expression"))
  {
    const char* s = vtkSMPropertyHelper(expr).GetAsString();
    if (s && s[0])
    {
      out += QStringLiteral("Selection expression=%1\n").arg(QString::fromUtf8(s));
    }
  }
  if (vtkSMProperty* io = selIn->GetProperty("InsideOut"))
  {
    vtkSMPropertyHelper h(io);
    if (h.GetNumberOfElements() && h.GetAsInt())
    {
      out += QStringLiteral("Selection InsideOut=1\n");
    }
  }
  const QList<vtkSMProxy*> nodes = selectionSourceNodes(selIn);
  out += QStringLiteral("Selection sources=%1\n").arg(nodes.size());
  for (int i = 0; i < nodes.size(); ++i)
  {
    appendSelectionSourceSummary(out, nodes[i], i);
  }

  // AppendSelections outputs vtkSelection (IDs/frustum), not geometry.
  // Extracted mesh counts/bounds come from the source's ExtractSelection proxy.
  if (vtkSMSourceProxy* srcProxy = port->getSourceProxy())
  {
    srcProxy->CreateSelectionProxies();
    if (vtkSMSourceProxy* extracted =
          srcProxy->GetSelectionOutput(static_cast<unsigned int>(port->getPortNumber())))
    {
      extracted->UpdatePipeline();
    }
  }
  vtkPVDataInformation* di = port->getSelectedDataInformation();
  if (di)
  {
    out += QStringLiteral("Extracted points=%1 cells=%2 type=%3\n")
             .arg(static_cast<qint64>(di->GetNumberOfPoints()))
             .arg(static_cast<qint64>(di->GetNumberOfCells()))
             .arg(QString::fromUtf8(di->GetDataClassName() ? di->GetDataClassName() : "?"));
    double b[6] = { 0, 0, 0, 0, 0, 0 };
    di->GetBounds(b);
    out += QStringLiteral("Extracted bounds=[%1, %2] [%3, %4] [%5, %6]\n")
             .arg(b[0])
             .arg(b[1])
             .arg(b[2])
             .arg(b[3])
             .arg(b[4])
             .arg(b[5]);
  }
  return out;
}

void addStr(QString& out, vtkSMProxy* p, const char* name)
{
  vtkSMProperty* prop = p->GetProperty(name);
  if (!prop)
  {
    return;
  }
  vtkSMPropertyHelper h(prop);
  const unsigned int n = h.GetNumberOfElements();
  QStringList vals;
  for (unsigned int i = 0; i < n && i < 8; ++i)
  {
    const char* s = h.GetAsString(i);
    vals << (s ? QString::fromUtf8(s) : QString());
  }
  if (!vals.isEmpty())
  {
    out += QStringLiteral("  %1=%2\n").arg(QString::fromUtf8(name), vals.join(QLatin1Char(',')));
  }
}

void addNum(QString& out, vtkSMProxy* p, const char* name)
{
  vtkSMProperty* prop = p->GetProperty(name);
  if (!prop)
  {
    return;
  }
  vtkSMPropertyHelper h(prop);
  const unsigned int n = h.GetNumberOfElements();
  if (n == 0)
  {
    return;
  }
  QStringList vals;
  for (unsigned int i = 0; i < n && i < 6; ++i)
  {
    vals << QString::number(h.GetAsDouble(i));
  }
  out += QStringLiteral("  %1=%2\n").arg(QString::fromUtf8(name), vals.join(QLatin1Char(',')));
}

QString displayInfo()
{
  pqDataRepresentation* repr = pqActiveObjects::instance().activeRepresentation();
  if (!repr || !repr->getProxy())
  {
    return QStringLiteral("No active representation.");
  }
  vtkSMProxy* p = repr->getProxy();
  QString out = QStringLiteral("Representation xml=%1\n").arg(QString::fromUtf8(p->GetXMLName()));
  if (pqPipelineSource* in = repr->getInput())
  {
    out += QStringLiteral("  input=%1\n").arg(in->getSMName());
  }
  addStr(out, p, "Representation");
  addNum(out, p, "Visibility");
  addNum(out, p, "Opacity");
  addStr(out, p, "ColorArrayName");
  addStr(out, p, "ColorArray");
  addNum(out, p, "ScalarVisibility");
  addNum(out, p, "MapScalars");
  addNum(out, p, "InterpolateScalarsBeforeMapping");
  addNum(out, p, "PointSize");
  addNum(out, p, "LineWidth");
  addStr(out, p, "Interpolation");
  addNum(out, p, "DiffuseColor");
  addNum(out, p, "AmbientColor");
  addNum(out, p, "Ambient");
  addNum(out, p, "Diffuse");
  addNum(out, p, "Specular");
  addNum(out, p, "EdgeVisibility");
  addNum(out, p, "RenderPointsAsSpheres");
  addNum(out, p, "RenderLinesAsTubes");
  {
    vtkSmartPointer<vtkSMPropertyIterator> it;
    it.TakeReference(p->NewPropertyIterator());
    bool anyShyx = false;
    for (it->Begin(); !it->IsAtEnd(); it->Next())
    {
      const char* key = it->GetKey();
      vtkSMProperty* prop = it->GetProperty();
      if (!key || !prop || prop->GetInformationOnly() || prop->GetIsInternal())
      {
        continue;
      }
      const QString k = QString::fromUtf8(key);
      if (!k.startsWith(QLatin1String("PG_")) && !k.startsWith(QLatin1String("AS_")) &&
        !k.startsWith(QLatin1String("PL_")) && !k.startsWith(QLatin1String("HG_")) &&
        !k.startsWith(QLatin1String("PulseGlyph_")))
      {
        continue;
      }
      if (!anyShyx)
      {
        out += QStringLiteral("  SHYX display properties:\n");
        anyShyx = true;
      }
      const QString vals = formatHelperValues(prop, kMaxPropElems);
      out += QStringLiteral("    %1=%2\n").arg(k, vals.isEmpty() ? QStringLiteral("(empty)") : vals);
    }
  }
  out += QStringLiteral("  (LUT / camera / time: inspect_view; schema: describe_proxy on the display type)\n");
  return out;
}

QString cameraInfo()
{
  pqView* view = pqActiveObjects::instance().activeView();
  if (!view || !view->getProxy())
  {
    return QStringLiteral("No active view.");
  }
  vtkSMProxy* p = view->getProxy();
  QString out = QStringLiteral("View xml=%1\n").arg(QString::fromUtf8(p->GetXMLName()));
  const QSize sz = view->getSize();
  out += QStringLiteral("  ViewSize=%1,%2\n").arg(sz.width()).arg(sz.height());
  auto addVec = [&out, p](const char* name, int nwant) {
    vtkSMProperty* prop = p->GetProperty(name);
    if (!prop)
    {
      return;
    }
    vtkSMPropertyHelper h(prop);
    QStringList vals;
    const unsigned int n = h.GetNumberOfElements();
    for (unsigned int i = 0; i < n && static_cast<int>(i) < nwant; ++i)
    {
      vals << QString::number(h.GetAsDouble(i));
    }
    if (!vals.isEmpty())
    {
      out += QStringLiteral("  %1=%2\n").arg(QString::fromUtf8(name), vals.join(QLatin1Char(',')));
    }
  };
  addVec("CameraPosition", 3);
  addVec("CameraFocalPoint", 3);
  addVec("CameraViewUp", 3);
  addVec("CameraViewAngle", 1);
  addVec("CameraParallelScale", 1);
  addVec("CameraParallelProjection", 1);
  return out;
}

QString formatHelperValues(vtkSMProperty* prop, int maxElems)
{
  vtkSMPropertyHelper h(prop);
  const unsigned int n = h.GetNumberOfElements();
  if (n == 0)
  {
    return {};
  }
  QStringList vals;
  const unsigned int shown = std::min(n, static_cast<unsigned int>(maxElems));
  for (unsigned int i = 0; i < shown; ++i)
  {
    const char* s = h.GetAsString(i);
    if (s)
    {
      vals << QString::fromUtf8(s);
    }
    else
    {
      vals << QString::number(h.GetAsDouble(i));
    }
  }
  QString out = vals.join(QLatin1Char(','));
  if (n > shown)
  {
    out += QStringLiteral(" ... (%1 elems)").arg(n);
  }
  return out;
}

QString propTypeShort(vtkSMProperty* prop)
{
  if (!prop)
  {
    return QStringLiteral("?");
  }
  if (prop->IsA("vtkSMInputProperty"))
  {
    return QStringLiteral("input");
  }
  if (prop->IsA("vtkSMProxyProperty"))
  {
    return QStringLiteral("proxy");
  }
  if (prop->IsA("vtkSMDoubleVectorProperty"))
  {
    return QStringLiteral("double");
  }
  if (prop->IsA("vtkSMIntVectorProperty"))
  {
    return QStringLiteral("int");
  }
  if (prop->IsA("vtkSMIdTypeVectorProperty"))
  {
    return QStringLiteral("id");
  }
  if (prop->IsA("vtkSMStringVectorProperty"))
  {
    return QStringLiteral("string");
  }
  return QString::fromUtf8(prop->GetClassName());
}

void dumpProxyProperties(
  QString& out, vtkSMProxy* proxy, int indent, int& count, int maxCount, int depth)
{
  if (!proxy || depth < 0 || count >= maxCount)
  {
    return;
  }
  const QString pad = QString(indent, QLatin1Char(' '));
  vtkSmartPointer<vtkSMPropertyIterator> it;
  it.TakeReference(proxy->NewPropertyIterator());
  for (it->Begin(); !it->IsAtEnd() && count < maxCount; it->Next())
  {
    vtkSMProperty* prop = it->GetProperty();
    const char* pname = it->GetKey();
    if (!prop || !pname || prop->GetInformationOnly() || prop->GetIsInternal())
    {
      continue;
    }
    if (prop->IsA("vtkSMInputProperty"))
    {
      continue;
    }
    if (auto* pp = vtkSMProxyProperty::SafeDownCast(prop))
    {
      out += QStringLiteral("%1%2 (proxy)\n").arg(pad, QString::fromUtf8(pname));
      ++count;
      const unsigned int np = pp->GetNumberOfProxies();
      for (unsigned int i = 0; i < np && count < maxCount; ++i)
      {
        vtkSMProxy* child = pp->GetProxy(i);
        if (!child)
        {
          continue;
        }
        const char* cxml = child->GetXMLName();
        out += QStringLiteral("%1  [%2] xml=%3\n")
                 .arg(pad)
                 .arg(i)
                 .arg(QString::fromUtf8(cxml ? cxml : "?"));
        dumpProxyProperties(out, child, indent + 4, count, maxCount, depth - 1);
      }
      continue;
    }
    const QString vals = formatHelperValues(prop, kMaxPropElems);
    if (vals.isEmpty())
    {
      continue;
    }
    out += QStringLiteral("%1%2=%3\n").arg(pad, QString::fromUtf8(pname), vals);
    ++count;
  }
}

QString sourceProperties(const QString& name)
{
  pqPipelineSource* src = findSourceByName(name);
  if (!src || !src->getProxy())
  {
    return QStringLiteral("Source not found.");
  }
  vtkSMProxy* proxy = src->getProxy();
  QString out = QStringLiteral("Properties of %1 (xml=%2):\n")
                  .arg(src->getSMName())
                  .arg(QString::fromUtf8(proxy->GetXMLName()));
  int count = 0;
  dumpProxyProperties(out, proxy, 2, count, kMaxPropLines, 2);
  if (count >= kMaxPropLines)
  {
    out += QStringLiteral("  ... truncated\n");
  }
  return out;
}

vtkSMSessionProxyManager* sessionPxm()
{
  vtkSMProxyManager* pm = vtkSMProxyManager::GetProxyManager();
  return pm ? pm->GetActiveSessionProxyManager() : nullptr;
}

QString menuFromHints(vtkPVXMLElement* hints)
{
  if (!hints)
  {
    return {};
  }
  vtkPVXMLElement* menu = hints->FindNestedElementByName("ShowInMenu");
  if (!menu)
  {
    vtkPVXMLElement* nested = hints->FindNestedElementByName("Hints");
    menu = nested ? nested->FindNestedElementByName("ShowInMenu") : nullptr;
  }
  if (!menu)
  {
    return {};
  }
  const char* cat = menu->GetAttribute("category");
  return cat ? QString::fromUtf8(cat) : QString();
}

QString xmlDocSummary(vtkPVXMLElement* def, int maxChars)
{
  if (!def)
  {
    return {};
  }
  vtkPVXMLElement* doc = def->FindNestedElementByName("Documentation");
  if (!doc)
  {
    return {};
  }
  const char* sh = doc->GetAttribute("short_help");
  if (sh && sh[0])
  {
    return collapseWs(QString::fromUtf8(sh), maxChars);
  }
  const char* body = doc->GetCharacterData();
  if (body && body[0])
  {
    return collapseWs(QString::fromUtf8(body), maxChars);
  }
  return {};
}

QString extraNoteForXml(const QString& xml);
bool isAsciiLetter(QChar c);

QString spacedIdentifier(const QString& ident)
{
  QString out;
  out.reserve(ident.size() + 8);
  for (int i = 0; i < ident.size(); ++i)
  {
    const QChar c = ident[i];
    if (i > 0)
    {
      const QChar prev = ident[i - 1];
      const bool nextLower = (i + 1 < ident.size() && ident[i + 1].isLower());
      if ((c.isUpper() && prev.isLower()) || (c.isUpper() && prev.isUpper() && nextLower))
      {
        out += QLatin1Char(' ');
      }
    }
    out += c;
  }
  return out;
}

bool isQueryStopword(const QString& token)
{
  static const char* kStop[] = { "a", "an", "the", "to", "of", "for", "in", "on", "at", "and", "or",
    "is", "are", "do", "how", "what", "which", "with", "from", "this", "that", "please", "find",
    "search", "list", "show", "filter", "filters", "all", "me", "i",
    "\xE7\x9A\x84", "\xE4\xBA\x86", "\xE5\x90\x97", "\xE5\x91\xA2", "\xE5\x92\x8C", "\xE4\xB8\x8E",
    "\xE6\x89\xBE", "\xE6\x90\x9C", "\xE5\x88\x97\xE5\x87\xBA" };
  for (const char* w : kStop)
  {
    if (token.compare(QString::fromUtf8(w), Qt::CaseInsensitive) == 0)
    {
      return true;
    }
  }
  return false;
}

QStringList tokenizeQuery(const QString& query)
{
  QString buf;
  QStringList raw;
  auto flush = [&]() {
    if (!buf.isEmpty())
    {
      raw << buf;
      buf.clear();
    }
  };
  for (const QChar c : query.trimmed())
  {
    const char16_t u = c.unicode();
    const bool sep = c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char(';') ||
      c == QLatin1Char('/') || c == QLatin1Char('|') || c == QLatin1Char('+') || u == 0x3001 ||
      u == 0xff0c || u == 0xff1b || u == 0x3002;
    if (sep)
    {
      flush();
    }
    else
    {
      buf += c;
    }
  }
  flush();
  QStringList out;
  for (const QString& t : raw)
  {
    if (isQueryStopword(t))
    {
      continue;
    }
    if (t.size() == 1 && isAsciiLetter(t[0]))
    {
      continue;
    }
    out << t;
  }
  if (out.isEmpty() && !query.trimmed().isEmpty())
  {
    out << query.trimmed();
  }
  return out;
}

bool fieldHasToken(const QString& field, const QString& token)
{
  return !field.isEmpty() && field.contains(token, Qt::CaseInsensitive);
}

bool queryHits(const QString& query, const QStringList& fields)
{
  const QStringList tokens = tokenizeQuery(query);
  if (tokens.isEmpty())
  {
    return true;
  }
  for (const QString& tok : tokens)
  {
    bool hit = false;
    for (const QString& f : fields)
    {
      if (fieldHasToken(f, tok))
      {
        hit = true;
        break;
      }
    }
    if (!hit)
    {
      return false;
    }
  }
  return true;
}

bool isAsciiLetter(QChar c)
{
  const char16_t u = c.unicode();
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}

bool looksLikeSpecificProxyQuery(const QString& s)
{
  auto afterPrefix = [&](const QLatin1String& prefix) {
    int i = 0;
    while (true)
    {
      i = s.indexOf(prefix, i, Qt::CaseInsensitive);
      if (i < 0)
      {
        return false;
      }
      int j = i + static_cast<int>(prefix.size());
      int letters = 0;
      while (j < s.size() && isAsciiLetter(s[j]))
      {
        ++letters;
        ++j;
      }
      if (letters >= 3)
      {
        return true;
      }
      ++i;
    }
  };
  if (afterPrefix(QLatin1String("SHYX")) || afterPrefix(QLatin1String("VESPA")) ||
    afterPrefix(QLatin1String("CGAL")))
  {
    return true;
  }
  return s.contains(QLatin1String("PulseGlyph"), Qt::CaseInsensitive) ||
    s.contains(QLatin1String("Pulse Glyphs"), Qt::CaseInsensitive) ||
    s.contains(QLatin1String("AnimatedStreamline"), Qt::CaseInsensitive) ||
    s.contains(QLatin1String("Animated Streamline"), Qt::CaseInsensitive) ||
    s.contains(QLatin1String("PointLabel"), Qt::CaseInsensitive) ||
    s.contains(QLatin1String("Point Label"), Qt::CaseInsensitive) ||
    s.contains(QLatin1String("Hologram"), Qt::CaseInsensitive);
}

bool isCatalogQuery(const QString& q)
{
  const QString s = q.trimmed();
  if (s.isEmpty())
  {
    return true;
  }
  if (looksLikeSpecificProxyQuery(s))
  {
    return false;
  }
  static const char* keys[] = { "catalog", "overview", "features", "capabilities", "capability",
    "\xE5\x8A\x9F\xE8\x83\xBD", "\xE6\x9C\x89\xE4\xBB\x80\xE4\xB9\x88", "\xE8\x83\xBD\xE5\x8A\x9B",
    "\xE6\xB8\x85\xE5\x8D\x95", "\xE9\x83\xBD\xE8\x83\xBD" };
  for (const char* k : keys)
  {
    if (s.contains(QString::fromUtf8(k), Qt::CaseInsensitive))
    {
      return true;
    }
  }
  return false;
}

bool isShyxDisplayRepresentation(const QString& xml)
{
  return xml == QLatin1String("PulseGlyphRepresentation") ||
    xml == QLatin1String("AnimatedStreamlineRepresentation") ||
    xml == QLatin1String("PointLabelRepresentation") ||
    xml == QLatin1String("HologramRepresentation");
}

QString representationDisplayName(const QString& xml)
{
  if (xml == QLatin1String("PulseGlyphRepresentation"))
  {
    return QStringLiteral("Pulse Glyphs");
  }
  if (xml == QLatin1String("AnimatedStreamlineRepresentation"))
  {
    return QStringLiteral("Animated Streamline");
  }
  if (xml == QLatin1String("PointLabelRepresentation"))
  {
    return QStringLiteral("Point Label");
  }
  if (xml == QLatin1String("HologramRepresentation"))
  {
    return QStringLiteral("Hologram");
  }
  return {};
}

void collectExposedFromElement(vtkPVXMLElement* el, const QString& wantProxy,
  QList<QPair<QString, QString>>& out, bool inMatchingSubProxy)
{
  if (!el)
  {
    return;
  }
  const QString tag = QString::fromUtf8(el->GetName() ? el->GetName() : "");
  if (tag == QLatin1String("SubProxy"))
  {
    bool match = false;
    const unsigned int n = el->GetNumberOfNestedElements();
    for (unsigned int i = 0; i < n; ++i)
    {
      vtkPVXMLElement* child = el->GetNestedElement(i);
      if (!child)
      {
        continue;
      }
      if (QString::fromUtf8(child->GetName() ? child->GetName() : "") != QLatin1String("Proxy"))
      {
        continue;
      }
      const char* pn = child->GetAttribute("proxyname");
      if (pn && QString::fromUtf8(pn) == wantProxy)
      {
        match = true;
        break;
      }
    }
    for (unsigned int i = 0; i < n; ++i)
    {
      collectExposedFromElement(el->GetNestedElement(i), wantProxy, out, match);
    }
    return;
  }
  if (inMatchingSubProxy && tag == QLatin1String("Property"))
  {
    const char* sm = el->GetAttribute("name");
    const char* ex = el->GetAttribute("exposed_name");
    if (sm && sm[0] && ex && ex[0])
    {
      const QString smName = QString::fromUtf8(sm);
      const QString exName = QString::fromUtf8(ex);
      bool exists = false;
      for (const auto& p : out)
      {
        if (p.first == smName)
        {
          exists = true;
          break;
        }
      }
      if (!exists)
      {
        out.append(qMakePair(smName, exName));
      }
    }
  }
  const unsigned int n = el->GetNumberOfNestedElements();
  for (unsigned int i = 0; i < n; ++i)
  {
    collectExposedFromElement(el->GetNestedElement(i), wantProxy, out, inMatchingSubProxy);
  }
}

QList<QPair<QString, QString>> exposedNamesForSubproxy(const QString& xml)
{
  QList<QPair<QString, QString>> out;
  vtkSMSessionProxyManager* pxm = sessionPxm();
  vtkSMProxyDefinitionManager* defs = pxm ? pxm->GetProxyDefinitionManager() : nullptr;
  if (!defs || xml.isEmpty())
  {
    return out;
  }
  const char* hosts[] = { "GeometryRepresentation", "UnstructuredGridRepresentation",
    "UniformGridRepresentation", "StructuredGridRepresentation" };
  for (const char* host : hosts)
  {
    vtkPVXMLElement* def = defs->GetProxyDefinition("representations", host, false);
    if (!def)
    {
      continue;
    }
    collectExposedFromElement(def, xml, out, false);
    if (!out.isEmpty())
    {
      return out;
    }
  }
  return out;
}

bool isShyxishProxy(const QString& xml, const QString& label)
{
  if (xml.startsWith(QLatin1String("SHYX")) || xml.startsWith(QLatin1String("CGAL")) ||
    xml.startsWith(QLatin1String("vtkCGAL")) ||
    xml.contains(QLatin1String("VESPA"), Qt::CaseInsensitive) ||
    label.startsWith(QLatin1String("SHYX")) || label.startsWith(QLatin1String("VESPA")) ||
    label.contains(QLatin1String("CGAL"), Qt::CaseInsensitive))
  {
    return true;
  }
  return isShyxDisplayRepresentation(xml);
}

int tokenTextScore(const QString& token, const QString& text, int exact, int prefix, int contains)
{
  if (text.isEmpty() || token.isEmpty())
  {
    return 0;
  }
  if (text.compare(token, Qt::CaseInsensitive) == 0)
  {
    return exact;
  }
  if (text.startsWith(token, Qt::CaseInsensitive))
  {
    return prefix;
  }
  if (text.contains(token, Qt::CaseInsensitive))
  {
    return contains;
  }
  return 0;
}

int hitScore(const QString& query, const QString& xml, const QString& label, const QString& display,
  const QString& doc, const QString& menu, const QString& extra)
{
  if (query.isEmpty())
  {
    int s = xml.startsWith(QLatin1String("SHYX")) ? 20 : 5;
    if (!display.isEmpty())
    {
      s += 20;
    }
    return s;
  }
  const QString spacedXml = spacedIdentifier(xml);
  const QString spacedLabel = spacedIdentifier(label);
  int s = 0;
  int matched = 0;
  for (const QString& tok : tokenizeQuery(query))
  {
    int nameScore = tokenTextScore(tok, xml, 100, 60, 30);
    nameScore = std::max(nameScore, tokenTextScore(tok, spacedXml, 50, 40, 28));
    nameScore = std::max(nameScore, tokenTextScore(tok, label, 80, 40, 25));
    nameScore = std::max(nameScore, tokenTextScore(tok, spacedLabel, 50, 35, 22));
    nameScore = std::max(nameScore, tokenTextScore(tok, display, 80, 50, 70));
    int noteScore = tokenTextScore(tok, doc, 0, 0, 12);
    noteScore = std::max(noteScore, tokenTextScore(tok, menu, 0, 0, 8));
    noteScore = std::max(noteScore, tokenTextScore(tok, extra, 0, 0, 18));
    const int piece = std::max(nameScore, noteScore);
    if (piece > 0)
    {
      ++matched;
    }
    s += piece;
  }
  if (matched > 1)
  {
    s += 15 * matched;
  }
  if (xml.startsWith(QLatin1String("SHYX")) || isShyxDisplayRepresentation(xml))
  {
    s += 8;
  }
  return s;
}

QString listFilters(const QString& queryRaw)
{
  vtkSMSessionProxyManager* pxm = sessionPxm();
  vtkSMProxyDefinitionManager* defs = pxm ? pxm->GetProxyDefinitionManager() : nullptr;
  if (!defs)
  {
    return QStringLiteral("No proxy definition manager.");
  }
  const QString query = isCatalogQuery(queryRaw) ? QString() : queryRaw.trimmed();
  vtkSmartPointer<vtkPVProxyDefinitionIterator> it;
  it.TakeReference(defs->NewIterator());
  if (!it)
  {
    return QStringLiteral("Could not iterate proxy definitions.");
  }
  it->AddTraversalGroupName("filters");
  it->AddTraversalGroupName("sources");
  it->AddTraversalGroupName("representations");

  struct Hit
  {
    int score;
    QString line;
  };
  QList<Hit> filterHits;
  QList<Hit> reprHits;
  for (it->GoToFirstItem(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    const char* group = it->GetGroupName();
    const char* xmlc = it->GetProxyName();
    if (!group || !xmlc)
    {
      continue;
    }
    const QString xml = QString::fromUtf8(xmlc);
    const QString groupStr = QString::fromUtf8(group);
    vtkPVXMLElement* def = it->GetProxyDefinition();
    const char* labelc = def ? def->GetAttribute("label") : nullptr;
    const QString display = representationDisplayName(xml);
    const QString label = labelc ? QString::fromUtf8(labelc) : (display.isEmpty() ? xml : display);
    const QString menu = menuFromHints(it->GetProxyHints() ? it->GetProxyHints()
                                                           : (def ? def->FindNestedElementByName("Hints") : nullptr));
    const QString docFull = xmlDocSummary(def, 400);
    const QString doc = collapseWs(docFull, 90);
    const QString extra = extraNoteForXml(xml);
    const bool shyxish = isShyxishProxy(xml, label);
    if (groupStr == QLatin1String("representations") && !shyxish)
    {
      continue;
    }
    if (query.isEmpty() && !shyxish)
    {
      continue;
    }
    if (!queryHits(query, { xml, label, menu, docFull, display, spacedIdentifier(xml),
          spacedIdentifier(label), extra }))
    {
      continue;
    }
    const int score = hitScore(query, xml, label, display, docFull, menu, extra);
    if (score <= 0 && !query.isEmpty())
    {
      continue;
    }
    QString line;
    if (groupStr == QLatin1String("representations"))
    {
      line = QStringLiteral("- %1  group=representations").arg(xml);
      if (!display.isEmpty())
      {
        line += QStringLiteral("  display='%1'  kind=display  "
                               "python: GetDisplayProperties().Representation = '%2'  "
                               "then describe_proxy for PG_/AS_/PL_/HG_ names")
                  .arg(display, display);
      }
      else if (xml.contains(QLatin1String("WidgetRepresentation")))
      {
        line += QStringLiteral("  kind=3d_widget (stent/cylinder; not Display dropdown)");
      }
      if (label != xml && display.isEmpty())
      {
        line += QStringLiteral("  label=%1").arg(label);
      }
    }
    else
    {
      line = QStringLiteral("- %1()  label=%2  group=%3").arg(xml, label, groupStr);
      if (!menu.isEmpty())
      {
        line += QStringLiteral("  menu=%1").arg(menu);
      }
    }
    if (!doc.isEmpty())
    {
      line += QStringLiteral("\n    %1").arg(doc);
    }
    if (groupStr == QLatin1String("representations"))
    {
      reprHits.push_back(Hit{ score, line });
    }
    else
    {
      filterHits.push_back(Hit{ score, line });
    }
  }
  auto byScore = [](const Hit& a, const Hit& b) { return a.score > b.score; };
  std::sort(filterHits.begin(), filterHits.end(), byScore);
  std::sort(reprHits.begin(), reprHits.end(), byScore);
  QString out;
  if (query.isEmpty())
  {
    out += QStringLiteral(
      "SHYX/VESPA catalog: filters/sources plus Display representations. "
      "Pass keywords (spaces/commas, AND) to search all ParaView filters/sources "
      "(SHYX representations always).\n");
  }
  else
  {
    const QString tokens = tokenizeQuery(query).join(QStringLiteral(" + "));
    out += QStringLiteral("Filters/sources/representations matching tokens (AND): %1\n").arg(tokens);
  }
  if (!reprHits.isEmpty())
  {
    out += QStringLiteral("Representations (not pipeline filters):\n");
    for (const Hit& h : reprHits)
    {
      out += h.line;
      out += QLatin1Char('\n');
    }
  }
  else if (query.isEmpty())
  {
    out += QStringLiteral(
      "Representations: (none loaded; expect Pulse Glyphs / Animated Streamline / Point Label / Hologram)\n");
  }
  out += QStringLiteral("Filters/sources:\n");
  const int n = std::min(static_cast<int>(filterHits.size()), kMaxFilterHits);
  if (n == 0)
  {
    out += QStringLiteral("(none)\n");
  }
  else
  {
    for (int i = 0; i < n; ++i)
    {
      out += filterHits[i].line;
      out += QLatin1Char('\n');
    }
    if (filterHits.size() > n)
    {
      out += QStringLiteral("... %1 more; narrow the query.\n").arg(filterHits.size() - n);
    }
  }
  out += QStringLiteral(
    "Python filters/sources: from paraview.simple import *; Name(Input=..., registrationName='...')\n"
    "Python display representations: GetDisplayProperties().Representation = 'Pulse Glyphs' "
    "(do not call PulseGlyphRepresentation() as a filter).\n");
  if (query.isEmpty())
  {
    out += QStringLiteral(
      "RenderView title-bar tools are not proxies: Sphere cell selection; "
      "Polygon cell selection (click vertices, not the freehand lasso); "
      "Grow selection with similar normals; Proximity gap selection. "
      "Block context menu: Select Block. "
      "Right-click Pick Center sets the center of rotation at that click (no second left-click). "
      "Selection context menu: Select Connected (connected points/lines/faces/volumes); "
      "Delete Selected Cells (also the Delete key in a RenderView: creates SHYXDeleteSelectedCellsFilter and Applies); "
      "Invert Selection; "
      "Select Similar → By Normal (grow to completion); "
      "Fill Interior (enclosed unselected faces). "
      "See lookup_shyx_docs. After the user uses them, call inspect_selection.\n");
  }
  return out;
}

vtkSMProxy* findPrototype(const QString& query, QString& groupOut, QString& xmlOut)
{
  vtkSMSessionProxyManager* pxm = sessionPxm();
  vtkSMProxyDefinitionManager* defs = pxm ? pxm->GetProxyDefinitionManager() : nullptr;
  if (!pxm || !defs || query.trimmed().isEmpty())
  {
    return nullptr;
  }
  const QString q = query.trimmed();
  const char* groups[] = { "filters", "sources", "representations" };
  for (const char* g : groups)
  {
    if (defs->HasDefinition(g, q.toUtf8().constData()))
    {
      vtkSMProxy* p = pxm->GetPrototypeProxy(g, q.toUtf8().constData());
      if (p)
      {
        groupOut = QString::fromUtf8(g);
        xmlOut = q;
        return p;
      }
    }
  }

  vtkSmartPointer<vtkPVProxyDefinitionIterator> it;
  it.TakeReference(defs->NewIterator());
  if (!it)
  {
    return nullptr;
  }
  it->AddTraversalGroupName("filters");
  it->AddTraversalGroupName("sources");
  it->AddTraversalGroupName("representations");
  QString bestGroup;
  QString bestXml;
  int best = 0;
  for (it->GoToFirstItem(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    const char* group = it->GetGroupName();
    const char* xmlc = it->GetProxyName();
    if (!group || !xmlc)
    {
      continue;
    }
    vtkPVXMLElement* def = it->GetProxyDefinition();
    const char* labelc = def ? def->GetAttribute("label") : nullptr;
    const QString xml = QString::fromUtf8(xmlc);
    const QString display = representationDisplayName(xml);
    const QString label = labelc ? QString::fromUtf8(labelc) : (display.isEmpty() ? xml : display);
    if (QString::fromUtf8(group) == QLatin1String("representations") && !isShyxishProxy(xml, label))
    {
      continue;
    }
    const QString menu = menuFromHints(it->GetProxyHints() ? it->GetProxyHints()
                                                           : (def ? def->FindNestedElementByName("Hints") : nullptr));
    const QString doc = xmlDocSummary(def, 400);
    const int score = hitScore(q, xml, label, display, doc, menu, extraNoteForXml(xml));
    if (score > best)
    {
      best = score;
      bestGroup = QString::fromUtf8(group);
      bestXml = xml;
    }
  }
  if (best < 25 || bestXml.isEmpty())
  {
    return nullptr;
  }
  vtkSMProxy* p = pxm->GetPrototypeProxy(bestGroup.toUtf8().constData(), bestXml.toUtf8().constData());
  if (p)
  {
    groupOut = bestGroup;
    xmlOut = bestXml;
  }
  return p;
}

void describeOneProperty(QString& out, vtkSMProperty* prop, const char* pname, int indent,
  const QString& pythonName = QString())
{
  if (!prop || !pname)
  {
    return;
  }
  const char* vis = prop->GetPanelVisibility();
  if (vis && QString::fromUtf8(vis) == QLatin1String("never"))
  {
    return;
  }
  const QString pad = QString(indent, QLatin1Char(' '));
  const QString shown = pythonName.isEmpty() ? QString::fromUtf8(pname) : pythonName;
  QString line = QStringLiteral("%1%2  type=%3").arg(pad, shown, propTypeShort(prop));
  if (!pythonName.isEmpty())
  {
    line += QStringLiteral("  sm=%1").arg(QString::fromUtf8(pname));
  }
  if (vis && vis[0] && QString::fromUtf8(vis) != QLatin1String("default"))
  {
    line += QStringLiteral("  panel=%1").arg(QString::fromUtf8(vis));
  }
  const QString vals = formatHelperValues(prop, kMaxPropElems);
  if (!vals.isEmpty())
  {
    line += QStringLiteral("  default=%1").arg(vals);
  }
  out += line;
  out += QLatin1Char('\n');

  if (auto* en = prop->FindDomain<vtkSMEnumerationDomain>())
  {
    QStringList entries;
    const unsigned int n = en->GetNumberOfEntries();
    for (unsigned int i = 0; i < n && i < 16; ++i)
    {
      const char* t = en->GetEntryText(i);
      entries << QStringLiteral("%1=%2").arg(QString::fromUtf8(t ? t : "?")).arg(en->GetEntryValue(i));
    }
    if (!entries.isEmpty())
    {
      out += QStringLiteral("%1  enum: %2\n").arg(pad, entries.join(QStringLiteral("; ")));
    }
  }
  if (auto* pl = prop->FindDomain<vtkSMProxyListDomain>())
  {
    QStringList names;
    const unsigned int n = pl->GetNumberOfProxies();
    for (unsigned int i = 0; i < n && i < 12; ++i)
    {
      const char* pn = pl->GetProxyName(i);
      names << QString::fromUtf8(pn ? pn : "?");
    }
    if (!names.isEmpty())
    {
      out += QStringLiteral("%1  proxy_list: %2\n").arg(pad, names.join(QStringLiteral(", ")));
    }
  }
  if (vtkSMDocumentation* doc = prop->GetDocumentation())
  {
    const char* sh = doc->GetShortHelp();
    const char* d = (!sh || !sh[0]) ? doc->GetDescription() : sh;
    if (d && d[0])
    {
      out += QStringLiteral("%1  %2\n").arg(pad, collapseWs(QString::fromUtf8(d), 160));
    }
  }
  if (auto* pp = vtkSMProxyProperty::SafeDownCast(prop))
  {
    if (pp->IsA("vtkSMInputProperty"))
    {
      return;
    }
    const unsigned int np = pp->GetNumberOfProxies();
    for (unsigned int i = 0; i < np && i < 2; ++i)
    {
      vtkSMProxy* child = pp->GetProxy(i);
      if (!child)
      {
        continue;
      }
      out += QStringLiteral("%1  current xml=%2\n")
               .arg(pad)
               .arg(QString::fromUtf8(child->GetXMLName() ? child->GetXMLName() : "?"));
      vtkSmartPointer<vtkSMPropertyIterator> cit;
      cit.TakeReference(child->NewPropertyIterator());
      int nleaf = 0;
      for (cit->Begin(); !cit->IsAtEnd() && nleaf < 20; cit->Next())
      {
        vtkSMProperty* cp = cit->GetProperty();
        const char* cn = cit->GetKey();
        if (!cp || !cn || cp->GetInformationOnly() || cp->GetIsInternal() || cp->IsA("vtkSMInputProperty"))
        {
          continue;
        }
        if (cp->IsA("vtkSMProxyProperty"))
        {
          continue;
        }
        describeOneProperty(out, cp, cn, indent + 4);
        ++nleaf;
      }
    }
  }
}

QString describeClientTool(const QString& query)
{
  const QString q = query.trimmed();
  if (q.isEmpty())
  {
    return {};
  }
  const QString ql = q.toLower();
  const bool memory = ql == QLatin1String("memory") || ql == QLatin1String("agent.md") ||
    ql == QLatin1String("agent memory") || ql.contains(QLatin1String("long-term memory")) ||
    ql.contains(QLatin1String("long term memory")) || ql.contains(QLatin1String("shyx memory")) ||
    ql.contains(QLatin1String("read_skill")) || ql.contains(QLatin1String("save_skill")) ||
    ql.contains(QLatin1String("update_skill")) || ql.contains(QLatin1String("write_agent_memory"));
  if (memory)
  {
    return QStringLiteral(
             "SHYX AI Assistant Memory (View dock; not a pipeline filter)\n"
             "Files on disk, not the registry and not .pvsm state files.\n"
             "Folder: %1\n"
             "Agent.md: short preferences included in every request. About 4000 characters are sent.\n"
             "skills/<name>/SKILL.md: one workflow per file. The request lists name and description only. "
             "Call read_skill(name) for the body.\n"
             "save_skill writes a new file and does not overwrite an existing name.\n"
             "update_skill overwrites an existing skill. Call read_skill first and pass the full "
             "revised body. It does not create or rename a file. Omit description to keep the catalog "
             "line, or omit body to change only that line.\n"
             "write_agent_memory replaces Agent.md. Use it for a preference that applies to every task, "
             "not for a pipeline procedure.\n"
             "The Memory button edits these files. Reset does not delete them.\n")
      .arg(QDir::toNativeSeparators(pqSHYXAIMemory::rootDirectory()));
  }
  const bool proximityGap = ql.contains(QLatin1String("proximity")) ||
    ql.contains(QLatin1String("near-disconnected")) ||
    ql.contains(QLatin1String("near disconnected")) ||
    ql.contains(QLatin1String("gap selection")) ||
    ql.contains(QLatin1String("shyxproximitygap"));
  const bool sphere = ql.contains(QLatin1String("sphere")) ||
    ql.contains(QLatin1String("shyxsphereselection"));
  const bool grow = ql.contains(QLatin1String("grow")) ||
    ql.contains(QLatin1String("dihedral")) || ql.contains(QLatin1String("similar normal")) ||
    ql.contains(QLatin1String("shyxgrowselection")) ||
    ql.contains(QLatin1String("select similar")) ||
    ql.contains(QLatin1String("selectsimilar")) ||
    ql.contains(QLatin1String("shyxselectsimilar")) ||
    ql.contains(QLatin1String("by normal"));
  const bool selectBlock = ql.contains(QLatin1String("selectblock")) ||
    ql.contains(QLatin1String("select block")) ||
    ql.contains(QLatin1String("select entire block")) ||
    ql.contains(QLatin1String("shyxselectblock"));
  const bool fillInterior = ql.contains(QLatin1String("fill interior")) ||
    ql.contains(QLatin1String("fillinterior")) ||
    ql.contains(QLatin1String("shyxfillinterior")) ||
    ql.contains(QLatin1String("select interior"));
  const bool selectConnected = ql.contains(QLatin1String("select connected")) ||
    ql.contains(QLatin1String("selectconnected")) ||
    ql.contains(QLatin1String("shyxselectconnected")) ||
    ql.contains(QLatin1String("select all")) ||
    ql.contains(QLatin1String("selectall")) ||
    ql.contains(QLatin1String("shyxselectall")) ||
    ql.contains(QLatin1String("connected region"));
  const bool invertSel = ql.contains(QLatin1String("invert")) ||
    ql.contains(QLatin1String("shyxinvert"));
  const bool polygon = ql == QLatin1String("polygon") ||
    ql.contains(QLatin1String("polygon selection")) ||
    ql.contains(QLatin1String("polygon cell")) ||
    ql.contains(QLatin1String("select polygon")) ||
    ql.contains(QLatin1String("shyxpolygon")) ||
    ql.contains(QStringLiteral("多边形"));
  if (polygon && !proximityGap && !sphere && !grow && !selectBlock && !fillInterior &&
    !selectConnected && !invertSel)
  {
    return QStringLiteral(
      "SHYX Polygon cell selection (RenderView title-bar; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call inspect_selection.\n"
      "Independent of the rectangle rubber band (Select Cells On) and of ParaView's freehand lasso "
      "(Select Cells With Polygon / Select Points With Polygon, which draw while the button is held).\n"
      "Interaction:\n"
      "  Toggle: title-bar polygon button (Polygon cell selection)\n"
      "  Left-click: add a vertex\n"
      "  Right-click: undo the last vertex (the view context menu does not open)\n"
      "  Double-click: close and select\n"
      "  The closing click adds a vertex when it is not on top of the last vertex\n"
      "  Needs at least 3 vertices\n"
      "  Stays on after a selection so another polygon can be drawn\n"
      "  Esc clears the draft, or leaves the mode when the draft is empty\n"
      "  Right-click the title-bar button (not the 3D view): Select through\n"
      "    Off (default): visible surface cells, same as Select Cells On\n"
      "    On: cells whose projection overlaps the polygon, including cells hidden behind the front surface\n"
      "  Selection modifiers: ParaView add/subtract/toggle (Ctrl/Shift, or the title-bar modifier buttons)\n"
      "  Middle-drag still pans; mouse wheel still zooms\n"
      "View right-click still undoes the last vertex; it does not open this menu.\n");
  }
  if (proximityGap)
  {
    const double eps = pqSHYXProximityGapSelectionController::Epsilon();
    const bool single = pqSHYXProximityGapSelectionController::SingleNearestRegion();
    const bool opposite = pqSHYXProximityGapSelectionController::TowardOppositeCenter();
    const QString epsText = eps > 0.0 ? QString::number(eps, 'g', 6)
                                      : QStringLiteral("auto (closest pair x 1.05 if single/opposite, else 2 x mean edge)");
    return QStringLiteral(
      "SHYX Proximity gap selection (RenderView title-bar; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call inspect_selection.\n"
      "Selects cells at near-disconnected contacts for later local Alpha Wrap.\n"
      "Parameters:\n"
      "  Epsilon  type=double  current=%1  units=world  default=0 (auto)\n"
      "    Euclidean gap distance. While the tool is toggled on, wheel on the button scales it.\n"
      "    Right-click to set. Wheel does nothing while the tool is off.\n"
      "  SingleNearestRegion  type=bool  current=%2  default=true\n"
      "    Used when TowardOppositeCenter is off: keep only the cluster of the closest pair.\n"
      "  TowardOppositeCenter  type=bool  current=%3  default=false\n"
      "    A = part1 points within ε of B's centroid; B = part2 points within ε of A's centroid.\n"
      "    Centroids iterate from the globally closest pair. Limits expansion along parallel surfaces.\n"
      "    When on, this mode is used instead of dist(a, whole other part).\n"
      "Interaction:\n"
      "  Toggle the title-bar two-patch/gap button on to compute on the active visible vtkPolyData\n"
      "  While on, wheel on the button: scale ε and recompute. Wheel while off does nothing.\n"
      "  Toggle off to stop; the current selection is left as-is\n"
      "  Right-click: Set gap distance ε… / Single nearest region / Toward opposite center\n"
      "Typical next step: SHYXSelectionFillAlphaReunionFilter on this cell selection.\n")
      .arg(epsText)
      .arg(single ? QStringLiteral("true") : QStringLiteral("false"))
      .arg(opposite ? QStringLiteral("true") : QStringLiteral("false"));
  }
  if (sphere && !grow)
  {
    return QStringLiteral(
      "SHYX Sphere cell selection (RenderView title-bar; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call inspect_selection.\n"
      "Interaction:\n"
      "  Toggle: title-bar sphere button\n"
      "  On enable: snap center to nearest vertex at the view center\n"
      "  Initial radius: ~15% of the viewport short edge in world units\n"
      "  Left-drag: move center (grab point stays under the cursor)\n"
      "  Hover + mouse wheel: scale radius (min 1e-12)\n"
      "  Selects cells whose vertices lie inside the sphere\n"
      "  All visible pipeline representations in the view are queried (not only the active node)\n"
      "  Composite parent (multiblock / PDC): every intersecting leaf is selected\n"
      "  Selection modifiers: ParaView add/subtract/toggle (Shift/Ctrl as usual)\n"
      "  Right-click the button: 'Apply selection on release only' (DeferSelectionUntilRelease, "
      "on by default)\n");
  }
  if (grow && !sphere && !selectBlock)
  {
    const double deg = pqSHYXGrowSelectionWithSimilarController::DihedralThresholdDegrees();
    return QStringLiteral(
      "SHYX Grow / Select Similar by normal (client Qt; not a Server Manager proxy)\n"
      "No Python constructor. After the user uses it, call inspect_selection.\n"
      "Parameters:\n"
      "  DihedralThresholdDegrees  type=double  current=%1  range=[0, 180]  default=15\n"
      "    Angle between face normals. Shared by the title-bar button and the context menu.\n"
      "    Right-click the title-bar Grow button to edit. Not a paraview.simple property.\n"
      "Title-bar Grow selection with similar normals:\n"
      "  Requires an existing cell selection\n"
      "  Grows by one ring of edge-adjacent faces with normal-normal angle <= threshold\n"
      "  Click once: one ring; press-and-hold: keep growing until no more similar neighbors\n"
      "  Status notices appear on the SHYX chip at the left of the ParaView status bar (not Output Messages)\n"
      "RenderView context menu (when a cell selection is active):\n"
      "  Select Similar → By Normal: grow ALL similar rings in one action (not one click per ring)\n")
      .arg(deg, 0, 'g', 4);
  }
  if (fillInterior && !sphere && !grow && !selectBlock && !selectConnected && !invertSel)
  {
    return QStringLiteral(
      "SHYX Fill Interior (RenderView selection context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call inspect_selection.\n"
      "Interaction:\n"
      "  Right-click in the 3D view when a cell selection is active → Fill Interior\n"
      "  Adds unselected faces that form holes completely enclosed by the current selection\n"
      "  Requires vtkPolyData. Needs a closed loop of selected faces around the interior\n"
      "  On an open surface, unselected regions that still reach a mesh opening are left unselected\n"
      "  On a closed surface, the largest enclosed complement is treated as the exterior "
      "unless it is no larger than the current selection\n");
  }
  if (selectConnected && !sphere && !grow && !selectBlock && !invertSel)
  {
    return QStringLiteral(
      "SHYX Select Connected (RenderView selection context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call inspect_selection.\n"
      "Interaction:\n"
      "  Right-click in the 3D view when a point or cell selection is active → Select Connected\n"
      "  Selects every topologically connected element in the region(s) that contain the selection:\n"
      "    points: through cells that share those points (mesh connectivity)\n"
      "    lines: other 1-D cells that share a vertex\n"
      "    faces: other 2-D cells that share an edge\n"
      "    volumes: other 3-D cells that share a face\n"
      "  Works on vtkPolyData and vtkUnstructuredGrid (not only surfaces)\n"
      "  No dihedral threshold (unlike Select Similar / By Normal)\n"
      "  Disconnected components that do not touch the selection are left unselected\n"
      "  If the mesh is a single connected component of that entity type, this selects all of them\n"
      "  Menu name is Select Connected, not Select All: it does not select the whole mesh\n"
      "  Result counts are shown on the SHYX status chip at the left of the status bar, not Output Messages\n");
  }
  if (invertSel && !sphere && !grow && !selectBlock)
  {
    return QStringLiteral(
      "SHYX Invert Selection (RenderView selection context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call inspect_selection.\n"
      "Interaction:\n"
      "  Right-click in the 3D view when a cell selection is active → Invert Selection\n"
      "  Selects currently unselected cells and deselects the current selection\n"
      "  Operates on the active dataset (not only vtkPolyData). Complement of all cells\n"
      "  If every cell was selected, the result is an empty selection\n");
  }
  const bool deleteShortcut = ql.contains(QLatin1String("delete shortcut")) ||
    ql.contains(QLatin1String("delete key")) || ql.contains(QLatin1String("del key")) ||
    ql.contains(QLatin1String("delete-key"));
  const bool recovery = ql.contains(QLatin1String("recovery")) ||
    ql.contains(QLatin1String("shyxrecovery")) || ql.contains(QLatin1String("checkpoint"));
  if (recovery)
  {
    return QStringLiteral(
      "SHYX Recovery (File menu submenu; client Qt; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties.\n"
      "File → Recovery lists *.pvsm files in the OS temp folder (%TEMP% on Windows, $TMPDIR or "
      "/tmp otherwise), newest first.\n"
      "Temp folder: %1\n"
      "Known entries: shyx_checkpoint.pvsm is kept up to date automatically and shown as "
      "'Latest checkpoint - shyx_checkpoint.pvsm'. It is rewritten when a pipeline source becomes "
      "MODIFIED (the first pending change of an Apply cycle, so it holds the last successfully "
      "applied parameter values), when the GUI creates a source/filter/reader, shortly after an "
      "Apply finishes, and shortly after a state is loaded. Only one checkpoint is kept; each "
      "write replaces it atomically, and each successful write posts a short 'checkpoint saved' "
      "notice on the SHYX status chip (left of the status bar). Uncheck 'Auto-save checkpoint' "
      "at the top of the Recovery menu to stop the updates.\n"
      "Menu actions: click a file to load that state; Auto-save checkpoint toggle; Open Temp "
      "Folder...; Refresh.\n"
      "Loading re-executes the visible pipeline. A checkpoint stores the last applied values, not "
      "the parameters that were still waiting for Apply, but an expensive filter can still take "
      "long on the current data. Loading skips the Load State Options dialog. If the state's data "
      "files moved, use File → Load State instead.\n")
      .arg(QDir::tempPath());
  }
  if (deleteShortcut && !sphere && !grow)
  {
    return QStringLiteral(
      "SHYX Delete shortcut (RenderView; creates a pipeline filter)\n"
      "Press Delete while the 3D view has focus, or right-click a point/cell selection → "
      "Delete Selected Cells.\n"
      "This creates SHYXDeleteSelectedCellsFilter on the dataset that owns the selection, "
      "copies the active selection (same as creating the filter from the menu), and Applies.\n"
      "Only unmodified Delete is handled (Ctrl/Shift/Alt/Meta + Delete are left alone). "
      "The shortcut is ignored while focus is outside the 3D view.\n"
      "Needs a vtkDataSet. Composite / multiblock selections are refused.\n"
      "Filter parameters: describe_proxy('SHYXDeleteSelectedCellsFilter'). "
      "InvertSelection stays off.\n");
  }
  const bool quickWheel = ql.contains(QLatin1String("quick filter")) ||
    ql.contains(QLatin1String("quickfilter")) || ql.contains(QLatin1String("quick-filter")) ||
    ql.contains(QLatin1String("radial")) || ql.contains(QLatin1String("pie menu")) ||
    ql.contains(QLatin1String("pimenu")) || ql.contains(QLatin1String("backtick wheel")) ||
    ql.contains(QLatin1String("grave key")) || ql.contains(QStringLiteral("轮盘"));
  if (quickWheel && !sphere && !grow && !proximityGap && !selectBlock)
  {
    return QStringLiteral(
      "SHYX quick filter wheel (RenderView; hold ` / · ; client Qt, no SM proxy of its own)\n"
      "Hold the backtick key (` on a US layout, · on a Chinese layout) over a 3D view for "
      "~200 ms: a radial quick-filter menu opens under the cursor.\n"
      "No mouse movement is needed; the key state is polled, so keyboard focus does not matter.\n"
      "  Move over a sector and release the key: run that filter (no click needed)\n"
      "  Left-click a sector: also runs it, for people used to the old gesture\n"
      "  Center hole, releasing the key over the hole or outside the ring, Esc, or a click "
      "outside: dismiss without running anything\n"
      "  Disabled (dimmed) sectors only post the reason on the SHYX status chip\n"
      "Every sector except \"+\" is a filter the user checked in the checklist; each one is created "
      "on the active pipeline object (or on the port that owns the selection when there is one) as "
      "Input and Applies. Nothing is checked by default, so a fresh configuration shows only \"+\".\n"
      "To put the Delete-key behaviour on the wheel, check SHYX Delete Selected Cells "
      "(SHYXDeleteSelectedCellsFilter, which copies the active selection). To get the built-in "
      "surface extraction, check Extract Surface (DataSetSurfaceFilter). Both are ordinary rows of "
      "the checklist now; there are no dedicated wheel actions.\n"
      "Configuration: left-click the \"+\" sector to open a checklist with two sections, SHYX and "
      "ParaView / VESPA built-in, listing every filter in this build that takes an Input "
      "(search box, All/None). Checked "
      "ids are stored in QSettings group SHYX/QuickFilterWheel, key items; the wheel always "
      "keeps the \"+\" sector, even when nothing is checked.\n"
      "Ctrl is not involved at all, so Ctrl+click / Ctrl+key shortcuts keep working. Only while "
      "ParaView is the active window with the pointer over a RenderView, and not while a text "
      "field has focus. The ` key is only swallowed once the wheel is open. "
      "The wheel itself has no Python constructor.\n"
      "Filter parameters: describe_proxy('SHYXDeleteSelectedCellsFilter'), "
      "describe_proxy('DataSetSurfaceFilter').\n");
  }
  if (selectBlock && !sphere && !grow)
  {
    return QStringLiteral(
      "SHYX Select Block (RenderView block context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call inspect_selection.\n"
      "Interaction:\n"
      "  Right-click a composite block in the 3D view (menu titled Block 'Part_1')\n"
      "  Choose Select Block to clear the current selection and select every cell "
      "in that block (BLOCK_SELECTORS, CELL)\n"
      "  Works with or without an existing cell selection; then use Extract Selection / "
      "SHYX selection filters\n");
  }
  const bool pickCenter = ql.contains(QLatin1String("pick center")) ||
    ql.contains(QLatin1String("pickcenter")) || ql.contains(QLatin1String("pick-center")) ||
    ql.contains(QLatin1String("shyxpickcenter")) ||
    ql.contains(QLatin1String("center of rotation")) ||
    ql.contains(QStringLiteral("旋转中心"));
  if (pickCenter && !sphere && !grow && !selectBlock)
  {
    return QStringLiteral(
      "SHYX Pick Center (RenderView context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties.\n"
      "Interaction:\n"
      "  Right-click in the 3D view → Pick Center\n"
      "  Sets the view center of rotation (the axes-toolbar Pick Center) to the surface "
      "point under that right-click\n"
      "  Uses the right-button press that opened the menu. It does not arm a second left-click\n"
      "  Same pick as the toolbar: ConvertDisplayToPointOnSurface, then CenterOfRotation\n"
      "  An empty-background click uses the camera focal depth, same as the toolbar\n"
      "  The toolbar button still waits for a later left-click; this menu item does not\n");
  }
  return {};
}

QString describeProxy(const QString& query)
{
  const QString client = describeClientTool(query);
  if (!client.isEmpty())
  {
    return client;
  }
  QString group;
  QString xml;
  vtkSMProxy* proto = findPrototype(query, group, xml);
  if (!proto)
  {
    return QStringLiteral("No proxy definition matching '%1'. Try list_filters or describe_proxy('sphere') / "
                          "describe_proxy('polygon') / describe_proxy('proximity gap') / describe_proxy('Pulse Glyphs') / "
                          "describe_proxy('pick center') / describe_proxy('recovery').")
      .arg(query.trimmed());
  }
  QString out;
  const QString display = representationDisplayName(xml);
  const bool displayRepr = group == QLatin1String("representations") && !display.isEmpty();
  if (group == QLatin1String("representations"))
  {
    out = QStringLiteral("Proxy %1  group=%2\n").arg(xml, group);
    if (displayRepr)
    {
      out += QStringLiteral(
        "Display type (not a pipeline filter): GetDisplayProperties().Representation = '%1'\n"
        "Do not call %2() as a filter. Python names are the exposed names below (disp.PG_Animate), "
        "not the sm= names.\n")
               .arg(display, xml);
    }
    else
    {
      out += QStringLiteral(
        "Widget/internal representation; not a pipeline filter and not a Display dropdown type.\n");
    }
  }
  else
  {
    out = QStringLiteral("Proxy %1  group=%2\nPython: %3()\n").arg(xml, group, xml);
  }
  const char* label = proto->GetXMLLabel();
  if (label && label[0])
  {
    out += QStringLiteral("label=%1\n").arg(QString::fromUtf8(label));
  }
  if (vtkSMDocumentation* doc = proto->GetDocumentation())
  {
    const char* sh = doc->GetShortHelp();
    const char* d = (!sh || !sh[0]) ? doc->GetDescription() : sh;
    if (d && d[0])
    {
      out += QStringLiteral("%1\n").arg(collapseWs(QString::fromUtf8(d), 400));
    }
  }
  if (vtkSMSourceProxy* sp = vtkSMSourceProxy::SafeDownCast(proto))
  {
    const unsigned int np = sp->GetNumberOfOutputPorts();
    out += QStringLiteral("output_ports=%1\n").arg(np);
    for (unsigned int i = 0; i < np; ++i)
    {
      const char* pn = sp->GetOutputPortName(i);
      out += QStringLiteral("  port %1%2\n")
               .arg(i)
               .arg(pn && pn[0] ? QStringLiteral(" %1").arg(QString::fromUtf8(pn)) : QString());
    }
  }

  if (displayRepr)
  {
    const QList<QPair<QString, QString>> exposed = exposedNamesForSubproxy(xml);
    out += QStringLiteral("Python: disp = GetDisplayProperties(); disp.Representation = '%1'\n").arg(display);
    if (exposed.isEmpty())
    {
      out += QStringLiteral("Could not read exposed_name map; listing subproxy SM properties.\n");
    }
    else
    {
      out += QStringLiteral("SHYX Display properties (use the first name in Python):\n");
      int n = 0;
      for (const auto& pair : exposed)
      {
        const QByteArray smUtf = pair.first.toUtf8();
        vtkSMProperty* prop = proto->GetProperty(smUtf.constData());
        if (!prop)
        {
          out += QStringLiteral("  %1  sm=%2  (not on subproxy prototype)\n").arg(pair.second, pair.first);
          continue;
        }
        describeOneProperty(out, prop, smUtf.constData(), 2, pair.second);
        ++n;
      }
      out += QStringLiteral("(%1 exposed properties)\n").arg(n);
      return out;
    }
  }

  out += QStringLiteral("Properties (skip Input / never / information_only):\n");
  vtkSmartPointer<vtkSMPropertyIterator> it;
  it.TakeReference(proto->NewPropertyIterator());
  int n = 0;
  for (it->Begin(); !it->IsAtEnd() && n < kMaxPropLines; it->Next())
  {
    vtkSMProperty* prop = it->GetProperty();
    const char* pname = it->GetKey();
    if (!prop || !pname || prop->GetInformationOnly() || prop->GetIsInternal())
    {
      continue;
    }
    describeOneProperty(out, prop, pname, 2);
    ++n;
  }
  if (n >= kMaxPropLines)
  {
    out += QStringLiteral("  ... truncated\n");
  }
  return out;
}

QString selectionIds()
{
  QString out = selectionInfo();
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqSelectionManager* sel = core ? core->selectionManager() : nullptr;
  if (!sel || !sel->hasActiveSelection())
  {
    return out;
  }
  pqOutputPort* port = sel->getSelectedPort();
  vtkSMSourceProxy* selIn = port ? port->getSelectionInput() : nullptr;
  if (!selIn)
  {
    return out;
  }
  const char* idProps[] = { "IDs", "CompositeIDs", "Locations", "Thresholds", "Values", "QueryString",
    "FieldType", "ContainingCells", "InsideOut", "Frustum" };
  const QList<vtkSMProxy*> nodes = selectionSourceNodes(selIn);
  out += QStringLiteral("Selection properties:\n");
  for (int i = 0; i < nodes.size(); ++i)
  {
    vtkSMProxy* node = nodes[i];
    if (!node)
    {
      continue;
    }
    const char* xml = node->GetXMLName();
    out += QStringLiteral("  source[%1] xml=%2\n")
             .arg(i)
             .arg(QString::fromUtf8(xml ? xml : "?"));
    for (const char* pn : idProps)
    {
      vtkSMProperty* prop = node->GetProperty(pn);
      if (!prop || prop->GetInformationOnly())
      {
        continue;
      }
      const QString pname = QString::fromUtf8(pn);
      const int cap = (pname == QLatin1String("IDs") || pname == QLatin1String("CompositeIDs"))
        ? kMaxSelIds
        : kMaxPropElems;
      QString vals;
      if (pname == QLatin1String("FieldType"))
      {
        vals = enumOrInt(node, pn);
      }
      else
      {
        vals = formatHelperValues(prop, cap);
      }
      if (!vals.isEmpty())
      {
        out += QStringLiteral("    %1=%2\n").arg(pname, vals);
      }
    }
  }
  out += QStringLiteral(
    "IDs on IDSelectionSource are (process,id) pairs; CompositeDataID is (block,process,id).\n"
    "Point coordinates are not in this dump; use the Extracted bounds/counts above.\n");
  return out;
}

void walkAssembly(QString& out, vtkDataAssembly* a, int node, int depth, int& count)
{
  if (!a || count >= kMaxBlockNodes || depth > 8)
  {
    return;
  }
  const char* name = a->GetNodeName(node);
  const char* label = a->GetAttributeOrDefault(node, "label", "");
  const QString pad = QString(depth * 2, QLatin1Char(' '));
  out += QStringLiteral("%1- %2").arg(pad, QString::fromUtf8(name ? name : "?"));
  if (label && label[0] && QString::fromUtf8(label) != QString::fromUtf8(name ? name : ""))
  {
    out += QStringLiteral("  label=%1").arg(QString::fromUtf8(label));
  }
  out += QLatin1Char('\n');
  ++count;
  const int n = a->GetNumberOfChildren(node);
  for (int i = 0; i < n && count < kMaxBlockNodes; ++i)
  {
    walkAssembly(out, a, a->GetChild(node, i), depth + 1, count);
  }
}

QString blockStructure(const QString& name, int port)
{
  pqPipelineSource* src = findSourceByName(name);
  if (!src || !src->getSourceProxy())
  {
    return name.trimmed().isEmpty() ? QStringLiteral("No active source.")
                                    : QStringLiteral("Source not found: %1").arg(name.trimmed());
  }
  const int nports = src->getNumberOfOutputPorts();
  if (port < 0 || (nports > 0 && port >= nports))
  {
    return QStringLiteral("Invalid port %1 for %2").arg(port).arg(src->getSMName());
  }
  vtkPVDataInformation* di = src->getSourceProxy()->GetDataInformation(port);
  if (!di)
  {
    return QStringLiteral("No data information.");
  }
  QString out = QStringLiteral("Blocks of %1 port=%2\n").arg(src->getSMName()).arg(port);
  const char* composite = di->GetCompositeDataClassName();
  out += QStringLiteral("composite=%1  type=%2\n")
           .arg(QString::fromUtf8(composite && composite[0] ? composite : "(not composite)"))
           .arg(QString::fromUtf8(di->GetDataClassName() ? di->GetDataClassName() : "?"));
  if (!di->IsCompositeDataSet())
  {
    return out + QStringLiteral("No hierarchy (single dataset). Use ExtractBlock only on PDC/MB.\n");
  }
  auto dumpAsm = [&out](const char* title, vtkDataAssembly* a) {
    if (!a)
    {
      return;
    }
    out += QStringLiteral("%1:\n").arg(QString::fromUtf8(title));
    int count = 0;
    walkAssembly(out, a, vtkDataAssembly::GetRootNode(), 0, count);
    if (count >= kMaxBlockNodes)
    {
      out += QStringLiteral("  ... truncated\n");
    }
  };
  dumpAsm("Hierarchy", di->GetHierarchy());
  dumpAsm("DataAssembly", di->GetDataAssembly());
  return out;
}

struct ShyxExtra
{
  const char* xml;
  const char* note;
};

const ShyxExtra kShyxExtra[] = {
  { "SHYXMeshChecker",
    "Prefer over VESPA Mesh Checker for vascular work. Port0 repaired mesh, port1 illegal primitives "
    "(soup edges / boundary rings / self-intersections). Typical follow-up for self-intersections: "
    "SHYXAutoMeshRepair." },
  { "SHYXSurfaceThickness",
    "Per-vertex wall thickness on a vtkPolyData surface. Method 0 Self-proximity (default) "
    "does not ray-cast: nearest Euclidean neighbor outside GeodesicRings hops, optional InwardOnly. "
    "Works with crossing faces (Thickness=0, ThicknessValid=1). Methods 1–2 shoot inward rays and "
    "are unreliable on self-intersections. Arrays: Thickness, ThicknessOverEdgeLength, "
    "ThicknessValid, LocalEdgeLength. MaxDistance 0 = 0.05 * longest AABB side. Color by "
    "ThicknessOverEdgeLength to spot collapsed branches (rho less than ~2)." },
  { "SHYXAutoMeshRepair",
    "Mesh Checker front-end (soup/boundary/self-intersection diagnostics + soup repair). "
    "RepairSelfIntersections is OFF by default. When on (CGAL>=5.5), RepairStage defaults to 0 "
    "Extract and Alpha Wrap: cluster intersections, dilate DilateLayers, hole-fill + alpha wrap each "
    "patch (no boolean union). Port0 remainder, port2 wrapped patches (cell SHYXAutoMeshRepairClusterId). "
    "Stage 1 Union: Input=remainder, WrappedPatches=port2 of the extract filter; CGAL union + optional "
    "bridge remesh/smooth (same as SHYXSelectionFillAlphaReunionFilter). Stage 2 is legacy one-shot "
    "(one cluster per pass then re-detect, up to MaxPasses) and can hang on corefinement. "
    "Alpha and Offset are always absolute lengths; 0 (default) uses 0.1 times the longest AABB "
    "side of the patch being wrapped. Port1 Mesh Checker illegal primitives (SHYX_CheckReason 1/2/3)." },
  { "SHYXBooleanOperationFilter",
    "Relaxed boolean; open meshes OK. FillHolesBeforeBoolean (default off) triangulates boundary "
    "cycles on Input and Source with CGAL triangulate_hole before the boolean. Strict watertight "
    "meshes can use VESPA Boolean." },
  { "SHYXAlphaWrapping",
    "CGAL alpha wrap (CGAL>=5.5): point cloud or triangle soup -> watertight 2-manifold that "
    "strictly encloses the input. Same backend as VESPAAlphaWrapping; prefer this. Alpha and "
    "Offset are always absolute lengths. 0 (default) uses 0.05 / 0.03 times the longest AABB "
    "side (BoundsDomain scaled_extent; Scale/Reset fills the suggestion). No "
    "UseAbsoluteThresholds / percent-of-diagonal mode. Field data SHYXAlphaWrappingAlpha and "
    "SHYXAlphaWrappingOffset record the values used." },
  { "SHYXHoleFillFilter",
    "SHYX hole fill; new pipelines prefer this over VESPA Hole Filling. RepairPolygonSoup "
    "(default on) is the same orient+repair_polygon_soup path as Mesh Checker port 0; raw "
    "duplicate-vertex rims otherwise often fill nothing with no error. Selection is copied at "
    "create (InitializationHelper)." },
  { "SHYXShapeSmoothing",
    "Three algorithms (MCF / Angle&Area / Fair). VESPA Shape Smoothing is MCF only. "
    "DetectFeatureEdges is OFF by default (no detect_sharp_edges / feature-mask constraints; "
    "port 1 empty)." },
  { "SHYXMeshSimplification",
    "Unified CGAL edge_collapse. Algorithm 0 Lindstrom-Turk (relocates), 1 plane QEM, "
    "2 triangle QEM, 3 probabilistic triangle QEM, 4 edge length + midpoint, "
    "5 endpoint subset (original vertices only; EndpointRanking 0 plane QEM, 1 min angle). "
    "StopCriterion 0 edge-count ratio, 1 remaining edge count, 2 edge-length threshold. "
    "PreserveBoundary and DetectFeatureEdges apply to every algorithm. Not isotropic remesh "
    "(SHYXAdaptiveIsotropicRemesher). SHYXEdgeCollapse and SHYXSubsetCoarsen remain separate filters." },
  { "SHYXSubsetCoarsen",
    "Triangle coarsen that never inserts Steiner points and never moves survivors: CGAL "
    "edge_collapse onto an original endpoint. CostStrategy 0 Plane QEM (default, geometric "
    "error) or 1 Min angle (post-collapse min interior angle in the remaining star). Not "
    "isotropic remesh (SHYXAdaptiveIsotropicRemesher split/relax/project) and not "
    "SHYXEdgeCollapse (Lindstrom-Turk / QEM / midpoint relocate). PreserveBoundary locks "
    "border edges. DetectFeatureEdges + ProtectAngle lock sharp edges. May stop above "
    "EdgeCountRatio if the link condition or constraints block further collapses." },
  { "SHYXAdaptiveIsotropicRemesher",
    "Curvature-adaptive remesh (CGAL>=6). Port 0 is the remeshed vtkPolyData. Port 1 is a "
    "vtkMultiBlockDataSet named Diagnostics with blocks 0 Sharp features (lines; empty when "
    "DetectFeatureEdges is OFF), 1 Mask patch (input faces passing the feature mask), 2 Sizing "
    "ICC preview (mesh before the last remesh iteration). "
    "IccNeighborhoodMode: 0 mesh rings (IccNeighborhoodRings, default 1 = incident faces) or "
    "1 Euclidean ball (IccBallRadius). Mutually exclusive. "
    "DetectFeatureEdges is OFF by default (no detect_sharp_edges / feature-mask constraints). "
    "Properties panel ICC size histogram is a live pre-remesh preview (same widget as SHYXRemeshWithEndpoint). "
    "SizingFieldScale multiplies the final ICC field after clamp / expansion / ScaleToRange "
    "(default 1; not re-clamped). "
    "Uniform target edge length: VESPA Isotropic Remesher." },
  { "SHYXRemeshWithEndpoint",
    "Vascular step 4: optional endpoint cull then ICC remesh / cap. Filled caps are retagged on "
    "cell EndpointIndex (wall -1, patches 1..n by area). Wall ICC neighborhood same as "
    "SHYXAdaptiveIsotropicRemesher (mesh rings vs Euclidean ball). "
    "SizingFieldScale multiplies the final wall ICC field after clamp / expansion / ScaleToRange "
    "(default 1; not re-clamped; cap remesh unchanged). "
    "RemeshProtectConstraints ON freezes wall opening loops: border edges are not split or "
    "collapsed, and opening vertices are pinned (not deleted by an incident collapse, not moved "
    "by relaxation or projection). Collapse and Relax constraints are ignored while it is ON. "
    "AnchorBoundarySize ON writes opening vertex targets to the mean border-edge length and "
    "uses Expansion ratio as the per-hop band from that length into the ICC field. "
    "BoundarySizeScale (default 1) is the shared opening multiplier. "
    "Anchor ON and Protect OFF: Opening boundary scales lists one row per positive cell "
    "EndpointIndex (the first value is that id, not a row number) with its own Scale. "
    "No positive ids falls back to open boundary loops. "
    "Empty, or every Scale equal, "
    "still uses that one factor (BoundarySizeScale, or the shared Scale). Differing Scales "
    "match each loop by centroid and are not compounded across iterations. "
    "The list is hidden when Anchor is OFF or Protect is ON; BoundarySizeScale is shown "
    "only when Anchor is OFF. "
    "Anchor OFF: opening vertices use clamped ICC curvature times BoundarySizeScale, not also "
    "SizingFieldScale; expansion and scale-to-range do not overwrite them. "
    "Anchor ON and Protect ON: the opening stays frozen at its input edge length. "
    ">1 coarsens, <1 refines. "
    "Expansion ratio <= 1 only rewrites the boundary vertices." },
  { "SHYXSkeletonExtraction",
    "Vascular step 1. Input must be watertight triangle mesh. Optional AppendCapEndpoints: "
    "cell EndpointIndex > 0 connected patches, area-weighted centroids joined to nearest "
    "skeleton point (split edge if interior hit). Attach degree > 1 = new branch: "
    "cell NewCapBranch=1 and warning; leaf (degree 1) extension stays 0. "
    "For already-capped meshes. First Apply keeps the parent mesh visible at Opacity 0.5 "
    "instead of hiding it." },
  { "SHYXVesselEndClipper",
    "Vascular step 2. Port0 clipped mesh, port1 clip planes (Point Label). "
    "Apply keeps the parent mesh visible (no input hiding)." },
  { "SHYXVmtkOpeningCenterlines",
    "Threshold cell array (default EndpointIndex > 0) -> openings, port1 seeds (Point Label). "
    "CalculateCenterline + CenterlineMethod: 0 Voronoi vtkvmtkPolyDataCenterlines on the closed "
    "surface (checked inlets / unchecked outlets; loops keep only the cheaper arm). "
    "Add Extra / Remove Extra: Extra_1 … Extra_n columns (—— / in / out); "
    "each Extra_n is an independent many-to-many extra group after the tree "
    "(every extra inlet to every extra outlet in that column); Delaunay/Voronoi "
    "are cached so extra traces and later Applies on the same surface only rerun Fast Marching. "
    "1 Network: delete those threshold cells so each cap becomes a boundary ring, then "
    "vtkvmtkPolyDataNetworkExtraction (keeps loops; inlet checks unused). AdvancementRatio "
    "is Network-only. Optional post-process (CalculateCenterline on): "
    "ComputeCenterlineAttributes (Abscissas, ParallelTransportNormals), "
    "ExtractCenterlineBranches (CenterlineIds, TractIds, GroupIds, Blanking; Voronoi intended), "
    "ComputeCenterlineGeometry (Length/Curvature/Torsion/Tortuosity, Frenet frames). "
    "Order is attributes then branches then geometry. "
    "Openings table Inlet/Remove column headers have a tri-state checkbox "
    "(none / mixed / all); click to select or clear the column. "
    "Inlet header select-all skips removed rows; Remove select-all unchecks those inlets." },
  { "SHYXVmtkCenterlineMerge",
    "One output port. Centerline polylines only (no vessel surface). Point radius array default "
    "MaximumInscribedSphereRadius. Three independent steps; all off = passthrough. "
    "ExtractCenterlineBranches (default on): vtkvmtkCenterlineBranchExtractor "
    "(slow pairwise tube test; output tracts with GroupIds/TractIds/CenterlineIds/Blanking). "
    "MergeCenterlines (default on): vtkvmtkMergeCenterlines (MergeBlanked default on = shared "
    "bifurcation vertices). Merge checkbox is visible only when Extract is on. "
    "SplitBySharedEndpoints (default on): partition polylines by endpoints within max(radius), "
    "then extract+merge each cluster so extra Appended overlapping paths cannot glue distant "
    "inlet groups; one shared-inlet set is still a single cluster. "
    "Intended for overlapping Voronoi source-target paths (Append Geometry of several "
    "Opening Centerlines / VMTK Centerlines outputs). Do not pre-split into two-point "
    "VTK_LINE cells. ReconstructSurface: polyball line + FlyingEdges on SampleDimensions "
    "(default 64^3), IsoValue 0 = tube wall of dist^2-r^2. Reconstruct without Extract/Merge "
    "uses the overlapping input (rounder crotch). Reconstruct after Merge models the merged "
    "tree (sharper crotch). Reconstruct on -> surface; otherwise current lines. "
    "ResamplingStepLength is an absolute length; 0 or Reset = 1% of the input AABB "
    "longest side (BoundsDomain scaled_extent)." },
  { "SHYXSelectionPlaneClipper",
    "Vascular step 3. Uses current selection / interactive plane. Faces (triangles, quads, "
    "polygons) give an area-weighted centroid and average normal. Lines and points fit a PCA "
    "plane when not collinear; otherwise incident surface or point normals (a single line gets "
    "a perpendicular last-resort plane). Fill Holes closes the clip opening; WheelCap fans "
    "triangles from the clip-plane origin (wheel-shaped cap) instead of ear-clip triangulation. "
    "InvertResult keeps the small fragment near the selection instead of discarding it." },
  { "SHYXTetGen",
    "Vascular step 5. Closed triangle surface -> tetrahedra. InteriorPointMode: "
    "0 Quality (Steiner, default), 1 Prescribed pipeline points (surface vertices plus "
    "optional InteriorPoints dropdown; -YS0O0, no Steiner). InteriorPoints is a pipeline "
    "node picker (same input_selector as Snappy Feature edges), not computed inside TetGen; "
    "leave (none) for surface-only. Typical pick: SHYXSkeletonExtraction. Mode 1 options: "
    "ConstrainInteriorEdges, InteriorSurfaceClearance." },
  { "SHYXDataSetToPartitionedCollection",
    "Vascular step 6. Convert dataset to PDC; then Boundary Assignment. Partitioned block names "
    "table has a leading eye per row that toggles that block in the active view (same "
    "BlockSelectors / BlockVisibilities as Hide Block). Side and node rows stay linked." },
  { "SHYXPartitionedCollectionBoundaryAssignment",
    "Vascular step 7. Call inspect_pipeline with the PDC name first. After area sort, side/node ENTITY_IDs are "
    "rewritten so rank follows numbering (largest -> smallest existing IDs). Enable Repart when inlets were "
    "already merged into one side set: it splits same-name patches by connected regions into original_n "
    "(side and node) before classify / options / optional Merge inlets. Port0 collection, "
    "port1 assignment debug. Flow Boundary Mode is auto-picked from the top-level source basename "
    "(case-insensitive; only when that path changes): plaque or pv → Single inlet (PV); aorta or hv → "
    "Single outlet (HV). plaque/aorta win over hv/pv; both of a pair leaves the mode unchanged. "
    "Chinese characters in the upstream file stem are filled into the export names as toneless "
    "pinyin (pinyin-data, first reading, ü→v); other non-ASCII stem characters become '_'. "
    "A non-ASCII export directory is still refused. "
    "Export writes Exodus + options + Nodeset + current .pvsm beside the "
    "chosen .exo. Pack into .zip writes those four files inside one .zip instead of loose files." },
  { "SHYXPartitionedCollectionBoundaryFields", "Adds boundary field arrays on an assigned PDC." },
  { "SHYXPartitionedCollectionToOpenFOAM",
    "Write an OpenFOAM case (constant/polyMesh + case.foam) from an IOSS PDC that has a volume "
    "UnstructuredGrid (TetGen -> DataSet To Partitioned Collection). Side sets become patches via "
    "volume-point GlobalIds / element_side. Node sets are ignored. Selection Append Patches has no "
    "volume and is rejected. Unmatched/floating sides are skipped with an error naming the side; "
    "overlapping sides fail. Unassigned volume boundary faces fail unless AllowDefaultFaces. "
    "CaseDirectory may be empty: Apply then writes %TEMP%/shyx-pdc-of-<id>-<mtime>/case; "
    "the Case row is one optional path (placeholder: leave empty) plus an open-folder button. "
    "VolumeArrayStatus checkboxes pick which PointData/CellData become 0/shyx_<array>. "
    "Also writes system/ simpleFoam dicts, 0/p and 0/U (zeroGradient on each PDC patch name), "
    "and volume PointData/CellData as 0/shyx_<array> (NaN is not written: interior NaNs become 0; "
    "PointData is averaged only if every vertex is finite; any NaN on a face writes 0 so data "
    "does not bleed one cell onto neighbours; patch value lists follow polyMesh face order)." },
  { "SHYXExtrudeFilter",
    "Replaces SHYX Point Extrude. Type Point = shared vertices along point normals (or a point "
    "vector array); Type Poly = each polygon along its face normal with unique vertices (scales). "
    "Output Front/Side/Back: Front is the cap (Point with Side and Back off = in-place move); "
    "Side = walls (Point: patch boundary ring; Poly: each scale edge back to original verts); "
    "Back keeps original selected faces (reversed if Front is also on). Use Selection and Use "
    "Mask Array are off by default: off ignores that input (all selected / extrude everything) "
    "and hides its controls. They are independent: either, both = intersection, neither = all. "
    "Invert flips the combined set. Avoid Intersection (Point only, default off) places vertices "
    "on offset face planes instead of moving them along vertex normals, then shortens any vertex "
    "whose face would invert, collapse, or hit a non-adjacent part of the input, so concave "
    "regions can end up shorter than Extrusion Distance. Use Normals and Direction Array are "
    "ignored while it is on. Creating the filter still "
    "copies the active selection into the widget so turning Use Selection on does not need Copy "
    "Active Selection unless the pick changed. Old SHYXPointExtrudeFilter() is gone. SHYX "
    "Selection Extrude is still the connected-patch tool along patch/point normals." },
  { "SHYXSelectionExtrudeFilter",
    "Connected-patch extrusion (shared cap vertices), not SHYX Extrude's Poly scales. "
    "Needs an active 3D selection of cells (or Selection Cell Array Name)." },
  { "SHYXInteractiveStreamTracer",
    "One filter, three interactive seed shapes: SeedShape 0 Line / 1 Sphere / 2 Rectangle (plane). "
    "Input (port 0) is the flow field. The snap surface is the dataset connected to the optional "
    "SnapSurface input (port 1, the panel's Snap target dropdown: any pipeline node, (none) = the "
    "Input itself); every vertex of that surface is a snap target (a volume is converted with "
    "vtkDataSetSurfaceFilter). So seeds can snap onto a separate vessel/stent node without merging "
    "it into the flow field. Snap mode is a client-side widget choice: Screen ray (default) casts a "
    "ray through the cursor, takes the front-most snap-surface hit, then its nearest vertex; Nearest "
    "vertex (3D) ignores the camera. Both write vertex positions, so the filter's own nearest-vertex "
    "snap in RequestData is idempotent (Python/batch only has the 3D mode, but honors SnapSurface). "
    "Line: LinePoint1 / LinePoint2 snap independently when LineSnapPoint1 / "
    "LineSnapPoint2 are on; LineResolution is subdivisions (Resolution + 1 seeds). Sphere: "
    "SphereCenter snaps when SphereSnapCenter is on; in the 3D widget left-drag moves the ball, and "
    "SphereRadius is set in the panel (no surface handle, no radius line and no R= label; "
    "right-drag still scales). "
    "SphereSeedMode 0 Surface = vtkSphereSource vertices, so the seed count is "
    "SphereThetaResolution x (SpherePhiResolution - 2) + 2 (Theta = longitude slices, "
    "Phi = pole-to-pole rings; 32 x 24 gives 706) and there are no seeds inside the ball. "
    "SphereSeedMode 1 Interior (the default, SphereInteriorPointCount = 20) = vtkPointSource "
    "uniform inside the ball with exactly that many seeds, reproducible via SphereRandomSeed. "
    "Rectangle: RectangleCenter / RectangleNormal / RectangleWidth / RectangleHeight define the "
    "quad (center-symmetric, in-plane axes derived from the normal). Only RectangleCenter snaps "
    "(RectangleSnapCenter); Place in view sets the center from the viewport-center ray and the "
    "normal to the camera direction. In the view, drag the yellow center handle to move, the "
    "single blue corner handle to resize Width/Height around the center, and either red normal "
    "cone (one on each side of the plane) to rotate it; hand-picking is display-space with "
    "center > corner > cone priority, so grabbing the center never rotates. "
    "RectangleNormal points toward the camera by default (GetViewPlaneNormal). "
    "Port 0 streamlines, port 1 seed geometry. The panel widget "
    "is client-side; Python snapping still works because the filter also snaps in RequestData. "
    "Rectangle port 1 can be added to SHYXSelectionAppendPatches (Add from pipeline) as a patch "
    "geometry; OpenFOAM polyMesh output remains SHYXPartitionedCollectionToOpenFOAM. "
    "The panel button Place in view positions the seed from the current camera instead of the "
    "dataset bounds: Line ray-picks the views 1/3 and 2/3 points, Sphere and Rectangle ray-pick "
    "the viewport center, and all snap to the nearest surface vertex. Sizes are viewport "
    "fractions converted to world units at that depth (sphere radius 15% of the short side, "
    "rectangle half side 20%); a ray that misses falls back to the dataset bounds center. "
    "After each drag ends the view center of rotation (pick center) follows the moved handle: "
    "sphere center, rectangle geometric center, or the line endpoint that was dragged "
    "(midpoint for a whole-line drag and for Place in view)." },
  { "SHYXSelectionAppendPatches",
    "Add from selection snapshots the 3D-view cell selection into the Patches table (geo_N); "
    "Copy Active Selection is not required. Add from Celldata dropdown lists Input CellData "
    "arrays and splits one unique value per table row (name = value; first component of vectors; "
    "refuses more than 256 unique values). Add from pipeline dropdown adds another pipeline "
    "geometry node as a patch. Add from shape dropdown adds a parametric Box or Sphere with an "
    "interactive 3D widget (initial size/position match Sphere Selection; Box left-click rotates). "
    "All rows are the same "
    "kind of PDC geometry partition — SnappyHexMesh Region patches can name any of them. Rename "
    "only (any name); the table keeps every row. Apply merges same names into one patch and reuses "
    "the earlier mark. Unique names are marked 0, 1, 2, ... in table order. A leading eye shows or "
    "hides that output patch in the active view (BlockSelectors / BlockVisibilities as Hide Block; "
    "same-named rows stay linked). Port 0 = added patches "
    "(PDC); port 1 = Input minus the union of selection-row cells (pipeline/box/sphere do not "
    "subtract). Apply on Add (default checkbox) Applies after each Add or Remove so both ports "
    "refresh; after Remove, dropped selection cells reappear on port 1. Uncheck it to skip Apply "
    "and allow overlapping picks. Unselected parent cells are not kept on port 0. Not the IOSS "
    "DataSetToPartitionedCollection path." },
  { "SHYXDeleteSelectedCellsFilter",
    "Needs an active cell or point selection. Creating the filter copies the Input's "
    "active selection into the Selection widget (Extract Selection-style); "
    "Copy Active Selection is only needed if the selection changes afterwards. "
    "InvertSelection (default off) deletes the complement of the selection. "
    "RenderView shortcut Delete (and the selection context menu Delete Selected Cells) "
    "creates this filter on the selected vtkDataSet and Applies it. Only unmodified Delete "
    "is handled. Delete does nothing while focus is outside the 3D view." },
  { "SHYXExtractSelectedCellsFilter",
    "Opposite of SHYX Delete Selected Cells: keeps selected cells. Unlike ParaView "
    "Extract Selection, PolyData input (or a selection of only vertex/line/polygon/strip "
    "cells) stays vtkPolyData; otherwise vtkUnstructuredGrid. Creating the filter copies "
    "the Input's active selection into the Selection widget; Copy Active Selection is only "
    "needed if the selection changes afterwards. InvertSelection (default off) keeps the "
    "complement. Empty selection (after invert) yields an empty output." },
  { "SHYXFlipSelectedCellsWindingFilter", "Needs an active cell selection." },
  { "SHYXSelectionFillAlphaReunionFilter",
    "Multiple Input connections on port 0 are merged (vtkAppendPolyData / Append Geometry). "
    "Selection is not a second pipeline port (SelectionInput hint, same as Extract Selection). "
    "It is repeatable and paired with Input order: extract each Input with its own cell ids, "
    "then remap by append offset. The panel copies every Input's current selection "
    "(Copy Input Selections). Then fill / alpha wrap / union (CGAL>=5.5). "
    "Alpha and Offset are always absolute lengths; 0 (default) uses 0.1 times the longest AABB "
    "side of the selected cells (recomputed each Apply). Copy Input Selections fills that 0.1 "
    "suggestion from the new selection AABB; Reset does the same without recopying "
    "(SHYXSelectionBoundsDomain); Scale multiplies the current value. No "
    "percent-of-diagonal mode. SelectionCellArrayName is a fallback mask on the merged mesh. "
    "SHYXBridgeCleanupMask (cell array, 1 = cleanup patch) is always written when a bridge "
    "cleanup mask exists; there is no ExportBridgeMask toggle. EnableBridgeRemesh and "
    "EnableBridgeSmooth are independent checkboxes (default both on); uncheck smooth to remesh only." },
  { "SHYXPointCloudSurfaceSDF", "Point cloud to surface SDF (VTK). Not CGAL vtkCGALSignedDistanceFunction." },
  { "SHYXArrayCurveMapper",
    "Maps a point/cell array through an editable piecewise curve. Default overwrites the "
    "selected input array (same name, 1-component; vectors become scalars) and SetActiveScalars "
    "on that array. CreateMappedArray writes OutputArrayName (default MappedArray) instead and "
    "activates the new array." },
  { "SHYXResampleLines",
    "Three independent steps on VTK_LINE / VTK_POLY_LINE networks (all default on). "
    "LineMerge pairwise-reduces polylines (binary tree: merge (0,1), (2,3), ... then those "
    "results) so overlapping trunks collapse early. In each pair the left network is kept; "
    "vertices within LineMergeTolerance of that network (0 = 1e-4 * AABB longest side) are "
    "overlapping: interior overlap is dropped and each remaining free run is snapped "
    "onto the earlier polyline (a vertex is inserted when the closest point is in a "
    "segment interior). Fuse then merges points within FuseTolerance "
    "(0 = 1e-6 * AABB longest side) so nearby ends share a vertex. Sample resamples "
    "each branch at SampleDistance (0 = 0.01 * AABB longest side); vertices with line "
    "degree != 2 are features and are kept. Each step can run alone; all off passes "
    "the input through. If spacing >= branch length, both endpoints stay as one "
    "segment; short branches are not deleted. Closed all-degree-2 loops are resampled "
    "around the cycle. Point array Degree is written last: undirected line degree of "
    "each vertex on the final network." },
  { "SHYXDisconnectedRegionFuse",
    "Join disconnected regions at a vertex pair with gap <= FuseThreshold (Kruskal). "
    "ComplianceWeight 0 = Euclidean nearest (default); 1 = among pairs inside T, prefer "
    "line tip-to-tip along outward tangent and surface connections in the tangent plane. "
    "Raising T does not move welds that already passed; it only joins farther "
    "region pairs. FuseWithinInput (default on): components inside one input are regions. "
    "Off: each port-0 connection is one region (AddInputConnection for extra pieces). "
    "FuseVerts / FuseLines / FusePolys (default all on). FusePositionMode 0 Average (midpoint), "
    "1 Snap small to large (keep the larger region's vertex; size is point count), "
    "2 Construct primitives (keep all points; lines get a new segment, surfaces get two "
    "triangles zipping the nearest incident edges, else a fallback line). "
    "Cell array SHYXFuseMark: 0 unchanged, 1 original cell incident to a fused vertex, "
    "2 new primitive (set as active cell scalars). "
    "Two regions join at one place only; "
    "a long seam is not stitched vertex-by-vertex. Triangle strips affect connectivity only." },
  { "SHYXSurfaceToVolumeMesh", "CGAL Mesh_3 tets from closed surface (alternative to TetGen)." },
  { "SHYXSnappyHexMesh",
    "Hex-dominant volume mesh. Input is vtkPartitionedDataSetCollection (each partition = one STL "
    "triSurfaceMesh / patch, no firstSolid/secondSolid) or a single vtkPolyData (wrapped as "
    "geometry). Optional FeatureEdges is a Properties-panel pipeline dropdown (not a second "
    "required input). Prefer SHYXExtendedFeatureEdgeMesh for classified "
    "extendedFeatureEdgeMesh (FoamExtendedFeatureEdgeMesh field -> "
    "constant/extendedFeatureEdgeMesh); plain polydata lines still become features.eMesh. "
    "Add partitions in Surface patches / Region patches (inside Castellated) and Layer patches "
    "(level, patchInfo type, region mode). Empty surfaces table = all partitions at Default surface level. "
    "CaseDirectory may be empty (then %TEMP%/shyx-snappy-<id>-<mtime>/case each Apply) or a chosen "
    "folder; one Case row with placeholder leave-empty plus an open-folder button "
    "(STLs in constant/triSurface). "
    "Output is vtkOpenFOAMReader's vtkMultiBlockDataSet (internalMesh plus patches), not a "
    "standalone unstructured grid. After a successful mesh, Block variables lists internalMesh "
    "and patches (names read-only; Refresh fills from output). Add/Delete Variable columns write "
    "0/shyx_BoundaryVariableN uniform volScalarFields (same 0/ style as "
    "SHYXPartitionedCollectionToOpenFOAM; empty/NaN -> 0; internalMesh row = internalField, "
    "patch rows = that patch). Patch Write Normal writes 0/shyx_BoundaryRadialValueNormal "
    "(average patch normal; ComputeBoundaryRadialValue scales by BoundaryRadialValue = 1 - x^a "
    "with BoundaryRadialNormalFalloffFactor). Changing only those values reuses the previous "
    "polyMesh and skips snappyHexMesh. Castellated off: arrays attach as cell data on the "
    "background hex. "
    "Requires VESPA_USE_SNAPPYHEXMESH." },
  { "SHYXExtendedFeatureEdgeMesh",
    "OpenFOAM extendedFeatureEdgeMesh (statically linked meshTools), same path as "
    "surfaceFeatureExtract. Input triangle surface; optional ExtraFeatureEdges lines. "
    "Output lines have EdgeStatus (external/internal/flat/open/multiple), PointStatus "
    "(convex/concave/mixed/nonFeature), RegionEdge, FeaturePoint, and field "
    "FoamExtendedFeatureEdgeMesh. Feed to SHYXSnappyHexMesh FeatureEdges. "
    "IncludedAngle is OpenFOAM includedAngle (degrees). RegionArrayName is a cell-data "
    "dropdown (default None = one region). BaffleAllRegions or cell array Baffle sets "
    "sideVolumeType BOTH. Requires VESPA_USE_SNAPPYHEXMESH." },
  { "SHYXEMeshReader",
    "Source/reader, not a filter. File → Open .eMesh / .extendedFeatureEdgeMesh, or "
    "SHYXEMeshReader(FileName=...). ASCII OpenFOAM featureEdgeMesh and "
    "extendedFeatureEdgeMesh (ASCII only; gzip/binary Foam not). Extended files get EdgeStatus / "
    "PointStatus / FoamExtendedFeatureEdgeMesh like SHYXExtendedFeatureEdgeMesh. "
    "Feed to SHYXSnappyHexMesh FeatureEdges. Does not need VESPA_USE_SNAPPYHEXMESH." },
  { "SHYXImageMorphology",
    "Voxel morphology on vtkImageData point scalars (not mesh face-ring DilateLayers). "
    "Operations: Dilate/Erode/Open/Close, morphological gradient (dilate-erode; not Filters "
    "Gradient Magnitude), internal/external gradient, white/black top-hat, binary hit-or-miss "
    "(FG SE + BG shell). ValueMode 0 Binary or 1 Grayscale (min/max). BinaryMatch 1 Threshold "
    "(default, voxel >= Threshold 0.5 is FG, else BG; result written 1/0) or 0 Equal "
    "(strict ForegroundValue/BackgroundValue; other labels copied). "
    "KernelShape 0 Box / 1 Cross / 2 Ellipsoid (default). KernelSize odd voxels "
    "default 3 3 3; 2D volumes force Z=1. Not Median. Python: SHYXImageMorphology()." },
  { "SHYXImageAntiAlias",
    "Whitaker anti-alias of a binary vtkImageData (voxel staircasing). Uses "
    "itk::AntiAliasBinaryImageFilter from the ITK that ships with VMTK (same VMTK_DIR as "
    "centerlines), not a second ITK install. Output is a float level set (inside>0, outside<0); "
    "extract the surface with Contour at 0, do not treat as 0/1 labels. BinarizeMode 1 Threshold "
    "(default, >=0.5 inside) or 0 Auto min/max (for 0/1 or 0/255 only). ResampleFactor (default 1) "
    "is vtkImageResample before anti-alias: >1 refine, <1 coarsen, 1 skip; 2D keeps Z=1. "
    "ResampleInterpolation 0 Nearest (default) / 1 Linear (re-threshold in Threshold mode). "
    "The 0.5-voxel occupancy lock is in output voxels, so factor 2 gives a tighter world-space "
    "round. If the grid changes, only the level set is kept. Not morphology and not "
    "Gaussian. Requires VESPA_USE_VMTK. Python: SHYXImageAntiAlias()." },
  { "SHYXImageTeasarCenterline",
    "Centerline polylines from a vtkImageData mask or label volume (TEASAR). Not VMTK "
    "surface Voronoi (SHYXVmtkOpeningCenterlines / SHYXVmtkPolyDataCenterlines) and not "
    "CGAL mesh skeleton (SHYXSkeletonExtraction). ForegroundMode 0 Threshold (default, "
    "scalar >= Threshold 0.5) or 1 Label (positive integers; LabelValue 0 = all labels). "
    "26-connected components. Output lines. Point data MaximumInscribedSphereRadius is the "
    "physical distance to background (image spacing). Also ObjectId, ComponentId; cell "
    "PathId. Scale 1.5 keeps side branches; 4-10 keeps mostly the trunk. ConstRadius is "
    "in the same physical units as spacing, default 0 (do not copy a nanometer constant). "
    "Invalidation radius = Scale * radius + ConstRadius (axis-aligned cube). "
    "FixBranching (default on) recomputes paths with zero penalty on the existing skeleton. "
    "FixBorders (default on) first connects contacts on the volume boundary. "
    "Loops are cut into a tree. Internal holes pull the path; close them with "
    "SHYXImageMorphology first. Does not need VESPA_USE_VMTK. "
    "Python: SHYXImageTeasarCenterline()." },
  { "PulseGlyphRepresentation",
    "Display representation, not a filter. Display dropdown 'Pulse Glyphs'. "
    "Python: GetDisplayProperties().Representation = 'Pulse Glyphs'. Never call PulseGlyphRepresentation(). "
    "Call describe_proxy('Pulse Glyphs') for PG_* names (PG_Animate, PG_IntegrationScale, PG_TimeScale, ...)." },
  { "AnimatedStreamlineRepresentation",
    "Display representation, not a filter. Display dropdown 'Animated Streamline'. "
    "Python: GetDisplayProperties().Representation = 'Animated Streamline'. "
    "Call describe_proxy('Animated Streamline') for AS_* names (AS_Animate, AS_IntegrationScale, AS_OpacityScale, ...)." },
  { "PointLabelRepresentation",
    "Display representation, not a filter. Display dropdown 'Point Label'. "
    "Python: GetDisplayProperties().Representation = 'Point Label'. "
    "Call describe_proxy('Point Label') for PL_* names (PL_ShowPointLabels, PL_PointLabelArray, PL_VertexOnly, ...)." },
  { "HologramRepresentation",
    "Display representation, not a filter. Display dropdown 'Hologram'. "
    "Python: GetDisplayProperties().Representation = 'Hologram'. Never call HologramRepresentation(). "
    "Lattice: triangle wires stay, a noise field reveals faces. Ghost: fresnel (rim reflection "
    "or facing-camera) makes triangles appear and disappear, plus the same field. "
    "HG_FresnelMode 2 is Uniform: fresnel is 1, normals are not used, and the noise or scan "
    "brightens every face the same amount. HG_FresnelPower is hidden in that mode. "
    "Surface meshes are drawn as they are. Volume cells (tets, hexes, voxels, wedges, pyramids, "
    "3D image and structured grids) are split into every face, including interior faces; quads stay "
    "quads so diagonals are not wires. "
    "HG_Flow 0/1/2 is foaming waves (7sBcRG), turbulence warp (NXS3DG), or maelstrom (MflyRf). "
    "HG_Flow 3 is Scan, a separate mode: the noise is not sampled. HG_ScanAxis is 0 X, 1 Y, 2 Z, "
    "or 3 view depth. X/Y/Z follow the dataset bounds and Display transforms. View depth is a plane "
    "perpendicular to the camera, near side of those bounds to the far side. HG_ScanWidth is the "
    "stripe thickness as a fraction of one period. HG_ScanAxis and HG_ScanWidth are hidden unless "
    "Flow is Scan. HG_NoiseScale and HG_NoisePower are hidden in Scan. HG_GroupMesh stays available. "
    "HG_BandCount is noise density, or how many stripes fit across the scan axis. "
    "HG_ScanSpeed 1 crosses the bounds in about 5 seconds in Scan. "
    "HG_NoiseScale magnifies the patches; HG_NoisePower above 1 keeps only the bright peaks. "
    "HG_GroupMesh off samples each fragment at its own position, so Scan slices through faces. "
    "On groups by the original volume cell: every face of one cell shares that cell's centroid, "
    "so the cell lights as a whole while a neighbor can still be dark. A surface polygon uses its "
    "own centroid. Grouped Scan uses the same speed, HG_BandCount, and HG_ScanWidth as an ungrouped "
    "scan. The band is a crinkle slice: a volume cell whose center lies in the band lights as a "
    "whole, and turns off again once the band has passed. Grouped noise is one value per cell. "
    "Fresnel stays per face unless HG_FresnelMode is Uniform. Ghost hides the noise except at "
    "the fresnel. In Scan, faces inside the band fill as it passes. "
    "Call describe_proxy('Hologram') for HG_* names (HG_Style, HG_Animate, HG_FresnelMode, HG_Flow, HG_ScanAxis, HG_GroupMesh, ...)." },
  { "SHYXSphereSelection",
    "Not a filter and not a proxy. RenderView title-bar sphere button. "
    "Selects every visible pipeline node in the view; on a composite parent, every intersecting "
    "block. Call describe_proxy('sphere') for interaction parameters. After the user uses it, "
    "call inspect_selection." },
  { "SHYXPolygonSelection",
    "Not a filter and not a proxy. RenderView title-bar polygon button: left-click adds a vertex, "
    "right-click undoes the last vertex, double-click closes. Right-click the button to toggle "
    "Select through (off: visible surface cells; on: also cells hidden behind the front surface). "
    "Independent of the rectangle rubber band and of ParaView's freehand lasso "
    "(Select Cells With Polygon). Call describe_proxy('polygon'). After use, call inspect_selection." },
  { "SHYXGrowSelectionWithSimilar",
    "Not a filter and not a proxy. RenderView title-bar button: one ring per click, hold to keep growing. "
    "Call describe_proxy('grow') for DihedralThresholdDegrees (default 15). After use, call inspect_selection." },
  { "SHYXProximityGapSelection",
    "Not a filter and not a proxy. RenderView title-bar button: select cells at near-disconnected "
    "contacts. Optional TowardOppositeCenter: A/B are ε-balls around each other's centroids "
    "(iterated from the closest pair), which limits parallel-surface flood. "
    "Call describe_proxy('proximity gap') for Epsilon, SingleNearestRegion, TowardOppositeCenter. "
    "Toggle the title-bar button on to compute; while on, wheel scales ε. Wheel while off does nothing. "
    "Right-click sets ε / toggles. After use, call inspect_selection. "
    "Typical follow-up: SHYXSelectionFillAlphaReunionFilter." },
  { "SHYXSelectSimilar",
    "Not a filter and not a proxy. RenderView right-click when a cell selection is active: "
    "Select Similar → By Normal grows all similar-normal rings in one shot (same dihedral threshold as Grow). "
    "Call describe_proxy('select similar'). After use, call inspect_selection." },
  { "SHYXFillInterior",
    "Not a filter and not a proxy. RenderView right-click when a cell selection is active: "
    "Fill Interior adds unselected faces enclosed by the current selection (holes inside a closed loop). "
    "Call describe_proxy('fill interior'). After use, call inspect_selection." },
  { "SHYXSelectConnected",
    "Not a filter and not a proxy. RenderView right-click when a point or cell selection is active: "
    "Select Connected selects every connected point, line, face, or volume in the region(s) that contain the current selection "
    "(points via mesh connectivity, lines sharing a vertex, faces sharing an edge, volumes sharing a face). "
    "Does not select disconnected components. "
    "Call describe_proxy('select connected') (select all still matches). After use, call inspect_selection." },
  { "SHYXInvertSelection",
    "Not a filter and not a proxy. RenderView right-click when a cell selection is active: "
    "Invert Selection selects currently unselected cells and deselects the current selection. "
    "Call describe_proxy('invert'). After use, call inspect_selection." },
  { "SHYXSelectBlock",
    "Not a filter and not a proxy. RenderView right-click on a composite block (Block 'Part_1' menu). "
    "Select Block clears the current selection then selects all cells in that block (BLOCK_SELECTORS). "
    "Call describe_proxy('select block'). "
    "After use, call inspect_selection." },
  { "SHYXPickCenter",
    "Not a filter and not a proxy. RenderView right-click → Pick Center sets the view center of "
    "rotation to the surface under that right-click (same pick as the axes-toolbar Pick Center). "
    "It does not wait for a second left-click. "
    "Call describe_proxy('pick center')." },
  { "SHYXRecovery",
    "Not a filter and not a proxy. File → Recovery lists *.pvsm files in the OS temp folder "
    "(newest first) and loads one with a click; shyx_checkpoint.pvsm is kept up to date "
    "automatically (before an Apply cycle's pending change is applied, after a source/filter/"
    "reader is created, after an Apply finishes, and after a state load). "
    "Call describe_proxy('recovery')." },
  { "SHYXQuickFilterWheel",
    "Not a filter and not a proxy. Hold the backtick key (` on a US layout, · on a Chinese "
    "layout) over a RenderView for ~200 ms: a radial quick-filter wheel opens under the cursor "
    "without moving the mouse (the key state is polled). Move over a sector and release the key "
    "to run it; left-clicking also works. Releasing over the center hole or outside the ring, "
    "Esc, or a click outside cancels. Dimmed items only report why. "
    "The trailing \"+\" sector opens a checkable list of the filters that can go on the wheel: all "
    "SHYX filters and the ParaView / VESPA built-in filters (Contour, Clip, Extract Surface, ...) "
    "that take an Input in this build, in two sections. Each checked filter is created on the "
    "active object (or the selection's port) as Input and Applied; SHYX Delete Selected Cells "
    "behaves like the Delete key because it copies the active selection. Nothing is checked by "
    "default, so a fresh wheel shows only \"+\". Checked ids persist in QSettings group "
    "SHYX/QuickFilterWheel (key items). "
    "Ctrl is not involved, so Ctrl+click and Ctrl shortcuts are unaffected. "
    "Call describe_proxy('quick filter wheel')." },
  { "SHYXCameraAdjust",
    "Source (Filters → SHYX), not a filter and not a Display representation: named camera presets "
    "stored in this node, so a camera configuration belongs to the pipeline branch instead of the "
    "global view / script variable. Properties are newline-separated rows: CameraNames, "
    "CameraPositions (x,y,z), CameraFocalPoints, CameraViewUps, CameraLens "
    "(viewAngle,parallelScale,parallelProjection 0/1). The Cameras table does Add (capture the "
    "active RenderView), Update selected, Delete selected, and Apply to view (sets Position / "
    "FocalPoint / ViewUp / ViewAngle / ParallelScale / ParallelProjection on the active RenderView "
    "and renders); double-click a non-Name cell applies that row. Output is one vertex per preset "
    "at CameraPositions with point arrays CameraIndex / CameraName (Point Gaussian), plus field "
    "data CameraPositions / CameraFocalPoints / CameraViewUps / CameraLens. The output alone never "
    "moves a camera. Call describe_proxy('camera adjust')." },
  { "SHYXAIAssistant",
    "Deprecated. The assistant is View → SHYX AI Assistant, not a pipeline filter. Do not create this node." },
};

QString extraNoteForXml(const QString& xml)
{
  for (const ShyxExtra& e : kShyxExtra)
  {
    if (xml == QLatin1String(e.xml))
    {
      return QString::fromUtf8(e.note);
    }
  }
  return {};
}

QString lookupShyxDocs(const QString& queryRaw)
{
  const QString query = isCatalogQuery(queryRaw) ? QString() : queryRaw.trimmed();
  QString out;
  if (query.isEmpty() || query.contains(QLatin1String("vascular"), Qt::CaseInsensitive) ||
    query.contains(QLatin1String("pipeline"), Qt::CaseInsensitive))
  {
    out += QStringLiteral(
      "Vascular menu order: SHYXSkeletonExtraction -> SHYXVesselEndClipper -> "
      "SHYXSelectionPlaneClipper -> SHYXRemeshWithEndpoint -> "
      "SHYXTetGen -> SHYXDataSetToPartitionedCollection -> "
      "SHYXPartitionedCollectionBoundaryAssignment\n");
  }
  if (query.isEmpty())
  {
    out += QStringLiteral(
      "SHYX is not only Filters → SHYX. Capability groups:\n"
      "1) Pipeline filters/sources: Filters → SHYX and Vascular toolbar. "
      "Live XML names: list_filters with empty query. "
      "SHYXCameraAdjust is a source that stores named camera presets in the node itself "
      "(Cameras table: Add / Update / Delete / Apply; not a filter and not a Display type).\n"
      "2) Display representations (Display panel Representation dropdown; NOT pipeline nodes):\n"
      "   Pulse Glyphs, Animated Streamline, Point Label, Hologram. "
      "Python: GetDisplayProperties().Representation = 'Pulse Glyphs'.\n"
      "3) RenderView title-bar selection tools (client Qt; no SM proxy / no Python constructor):\n"
      "   Sphere cell selection; Polygon cell selection (click vertices; not the freehand lasso); "
      "Grow selection with similar normals; Proximity gap selection. "
      "After the user uses them, call inspect_selection.\n"
      "   Also: right-click a composite block → Select Block "
      "(clear current selection, then select all cells in that part).\n"
      "   Also: right-click in the 3D view → Pick Center "
      "(center of rotation at that right-click; no second left-click). "
      "describe_proxy('pick center').\n"
      "   Also: right-click an active point or cell selection → Select Connected "
      "(connected region of that entity: points/lines/faces/volumes; not the whole mesh); "
      "Delete Selected Cells (the Delete key in the RenderView does the same: create "
      "SHYXDeleteSelectedCellsFilter and Apply); "
      "with a cell selection also Invert Selection; Select Similar → By Normal "
      "(grow all similar-normal rings in one shot); Fill Interior "
      "(add unselected faces enclosed by the current selection).\n"
      "   Also: hold ` (the · key) over the 3D view → quick filter wheel under the cursor "
      "(empty until filters are checked via its \"+\" sector); move over a sector and release the "
      "key to run it; releasing over the center hole or outside the ring, or Esc, cancels. "
      "The trailing \"+\" sector opens a checklist of every SHYX "
      "and ParaView / VESPA built-in filter that takes an Input, and the checked set persists. "
      "describe_proxy('quick filter wheel').\n"
      "4) Widget representations (stent placement 3D widgets, not Display dropdown): "
      "SHYXImplicitCylinderWidgetRepresentation, SHYXEndpointStentWidgetRepresentation.\n"
      "5) File → Recovery (client Qt; not a pipeline filter): submenu listing *.pvsm in the OS "
      "temp folder, e.g. shyx_checkpoint.pvsm (rewritten automatically before an Apply cycle's "
      "pending change runs, after a source/filter/reader is created, after an Apply finishes, and "
      "after a state load). "
      "describe_proxy('recovery').\n"
      "6) View → SHYX AI Assistant: this dock, not a pipeline filter. "
      "Memory stores Agent.md and skill files under %APPDATA%/SHYX/AIAssistant/memory "
      "(not the registry). describe_proxy('memory').\n"
      "Pass a name/topic (remesh, clip, representation, selection, sphere, glyph, ...) to filter notes.\n"
      "For parameter names/defaults: describe_proxy('Pulse Glyphs'), describe_proxy('Animated Streamline'), "
      "describe_proxy('Point Label'), describe_proxy('Hologram'), describe_proxy('sphere'), "
      "describe_proxy('polygon'), describe_proxy('grow'), "
      "describe_proxy('proximity gap'), describe_proxy('select block'), describe_proxy('select similar'), "
      "describe_proxy('fill interior'), describe_proxy('select connected'), "
      "describe_proxy('invert'), describe_proxy('pick center'), describe_proxy('recovery'), "
      "describe_proxy('quick filter wheel'), describe_proxy('camera adjust').\n");
  }

  for (const ShyxExtra& e : kShyxExtra)
  {
    const QString xml = QString::fromUtf8(e.xml);
    const QString display = representationDisplayName(xml);
    if (query.isEmpty() ||
      queryHits(query, { xml, display, spacedIdentifier(xml), QString::fromUtf8(e.note) }))
    {
      out += QStringLiteral("- %1: %2\n").arg(xml, QString::fromUtf8(e.note));
    }
  }

  vtkSMSessionProxyManager* pxm = sessionPxm();
  vtkSMProxyDefinitionManager* defs = pxm ? pxm->GetProxyDefinitionManager() : nullptr;
  if (defs && !query.isEmpty())
  {
    vtkSmartPointer<vtkPVProxyDefinitionIterator> it;
    it.TakeReference(defs->NewIterator());
    if (it)
    {
      it->AddTraversalGroupName("filters");
      it->AddTraversalGroupName("representations");
      int shown = 0;
      out += QStringLiteral("Live XML documentation:\n");
      for (it->GoToFirstItem(); !it->IsDoneWithTraversal() && shown < 8; it->GoToNextItem())
      {
        const char* xmlc = it->GetProxyName();
        const char* group = it->GetGroupName();
        if (!xmlc)
        {
          continue;
        }
        const QString xml = QString::fromUtf8(xmlc);
        vtkPVXMLElement* def = it->GetProxyDefinition();
        const char* labelc = def ? def->GetAttribute("label") : nullptr;
        const QString display = representationDisplayName(xml);
        const QString label = labelc ? QString::fromUtf8(labelc) : (display.isEmpty() ? xml : display);
        const QString doc = xmlDocSummary(def, 280);
        const QString menu = menuFromHints(it->GetProxyHints());
        if (!isShyxishProxy(xml, label))
        {
          continue;
        }
        if (!queryHits(query, { xml, label, menu, doc, display, spacedIdentifier(xml),
              spacedIdentifier(label) }))
        {
          continue;
        }
        out += QStringLiteral("- %1  label=%2").arg(xml, label);
        if (group && group[0])
        {
          out += QStringLiteral("  group=%1").arg(QString::fromUtf8(group));
        }
        if (!display.isEmpty())
        {
          out += QStringLiteral("  display='%1'").arg(display);
        }
        if (!menu.isEmpty())
        {
          out += QStringLiteral("  menu=%1").arg(menu);
        }
        out += QLatin1Char('\n');
        if (!doc.isEmpty())
        {
          out += QStringLiteral("  %1\n").arg(doc);
        }
        ++shown;
      }
      if (shown == 0)
      {
        out += QStringLiteral("(no live SHYX/VESPA XML match; try list_filters)\n");
      }
    }
  }
  out += QStringLiteral("For property names/enums/defaults call describe_proxy with the XML/python name "
                        "or display type (Pulse Glyphs, sphere, grow, select block, select similar, fill interior, select connected, invert, pick center, recovery, camera adjust).\n");
  if (!query.isEmpty())
  {
    const QString client = describeClientTool(query);
    if (!client.isEmpty())
    {
      out += QStringLiteral("\nParameter details:\n");
      out += client;
    }
    else
    {
      QString g;
      QString x;
      vtkSMProxy* proto = findPrototype(query, g, x);
      if (proto && g == QLatin1String("representations") && isShyxDisplayRepresentation(x))
      {
        out += QStringLiteral("\nParameter details:\n");
        out += describeProxy(query);
      }
    }
  }
  return out;
}

QString colorMapInfo()
{
  pqDataRepresentation* repr = pqActiveObjects::instance().activeRepresentation();
  if (!repr || !repr->getProxy())
  {
    return QStringLiteral("No active representation.");
  }
  QString out = QStringLiteral("Color map for active representation:\n");
  addStr(out, repr->getProxy(), "ColorArrayName");
  addNum(out, repr->getProxy(), "ScalarVisibility");
  pqScalarsToColors* lut = repr->getLookupTable();
  vtkSMProxy* lutp = repr->getLookupTableProxy();
  if (!lut && !lutp)
  {
    return out + QStringLiteral("No lookup table (solid color / not mapping scalars).\n");
  }
  if (lutp)
  {
    out += QStringLiteral("  LUT xml=%1\n").arg(QString::fromUtf8(lutp->GetXMLName()));
    addNum(out, lutp, "UseLogScale");
    addNum(out, lutp, "NumberOfTableValues");
    addNum(out, lutp, "IndexedLookup");
    addStr(out, lutp, "ColorSpace");
    addNum(out, lutp, "VectorMode");
    addNum(out, lutp, "VectorComponent");
    addNum(out, lutp, "ScalarRangeInitialized");
  }
  if (lut)
  {
    const QPair<double, double> r = lut->getScalarRange();
    out += QStringLiteral("  LUT range=[%1, %2]\n").arg(r.first).arg(r.second);
    out += QStringLiteral("  log=%1  vectorMode=%2  component=%3  rangeLock=%4\n")
             .arg(lut->getUseLogScale() ? QStringLiteral("on") : QStringLiteral("off"))
             .arg(lut->getVectorMode() == pqScalarsToColors::MAGNITUDE ? QStringLiteral("magnitude")
                                                                      : QStringLiteral("component"))
             .arg(lut->getVectorComponent())
             .arg(lut->getScalarRangeLock() ? QStringLiteral("on") : QStringLiteral("off"));
  }
  return out;
}

QString timeInfo()
{
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqAnimationManager* mgr = core ? core->animationManager() : nullptr;
  pqAnimationScene* scene = mgr ? mgr->getActiveScene() : nullptr;
  if (!scene)
  {
    return QStringLiteral("No animation scene.");
  }
  const QPair<double, double> clock = scene->getClockTimeRange();
  const QList<double> steps = scene->getTimeSteps();
  QString out = QStringLiteral("Animation time=%1  playing=%2\n")
                  .arg(scene->getAnimationTime())
                  .arg(mgr->animationPlaying() ? QStringLiteral("yes") : QStringLiteral("no"));
  out += QStringLiteral("Clock range=[%1, %2]\n").arg(clock.first).arg(clock.second);
  out += QStringLiteral("Timesteps (%1):\n").arg(steps.size());
  const int n = std::min(static_cast<int>(steps.size()), kMaxTimeSteps);
  QStringList vals;
  for (int i = 0; i < n; ++i)
  {
    vals << QString::number(steps[i]);
  }
  out += QStringLiteral("  %1").arg(vals.join(QStringLiteral(", ")));
  if (steps.size() > n)
  {
    out += QStringLiteral(" ...");
  }
  out += QLatin1Char('\n');
  return out;
}

QString pickOneWorldPoint(vtkSMRenderViewProxy* rvp, int vw, int vh, double x, double y, int imgW,
  int imgH, const QString& origin, bool snap, int index, int total)
{
  double nx = x;
  double ny = y;
  QString space = QStringLiteral("display-pixels");
  if (imgW > 1 && imgH > 1)
  {
    nx = x * static_cast<double>(vw) / static_cast<double>(imgW);
    ny = y * static_cast<double>(vh) / static_cast<double>(imgH);
    space = QStringLiteral("screenshot %1x%2 -> view %3x%4").arg(imgW).arg(imgH).arg(vw).arg(vh);
  }
  else if (x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0)
  {
    nx = x * (vw - 1);
    ny = y * (vh - 1);
    space = QStringLiteral("normalized");
  }

  int dx = static_cast<int>(std::lround(nx));
  int dyFromTop = static_cast<int>(std::lround(ny));
  int dy = dyFromTop;
  if (origin.compare(QLatin1String("vtk"), Qt::CaseInsensitive) != 0)
  {
    dy = vh - 1 - dyFromTop;
  }
  dx = std::max(0, std::min(vw - 1, dx));
  dy = std::max(0, std::min(vh - 1, dy));
  const int display[2] = { dx, dy };
  double world[3] = { 0, 0, 0 };
  double normal[3] = { 0, 0, 1 };
  const bool onSurface = rvp->ConvertDisplayToPointOnSurface(display, world, normal, snap);
  vtkSMRepresentationProxy* picked = rvp->Pick(dx, dy);

  QString out;
  if (total > 1)
  {
    out += QStringLiteral("[%1/%2] input=(%3, %4)\n").arg(index + 1).arg(total).arg(x).arg(y);
  }
  out += QStringLiteral("Pick display=(%1,%2) vtk-origin  (%3) origin=%4\n")
           .arg(dx)
           .arg(dy)
           .arg(space)
           .arg(origin);
  out += QStringLiteral("world=[%1, %2, %3]  on_surface=%4\n")
           .arg(world[0])
           .arg(world[1])
           .arg(world[2])
           .arg(onSurface ? QStringLiteral("true") : QStringLiteral("false (focal-plane fallback)"));
  if (!std::isnan(normal[0]))
  {
    out += QStringLiteral("normal=[%1, %2, %3]\n").arg(normal[0]).arg(normal[1]).arg(normal[2]);
  }
  if (!picked)
  {
    return out + QStringLiteral("hit source: (none)\n");
  }
  auto* sm = smModel();
  pqDataRepresentation* prepr = sm ? sm->findItem<pqDataRepresentation*>(picked) : nullptr;
  if (prepr && prepr->getInput())
  {
    pqOutputPort* op = prepr->getOutputPortFromInput();
    out += QStringLiteral("hit source=%1  port=%2  repr=%3\n")
             .arg(prepr->getInput()->getSMName())
             .arg(op ? op->getPortNumber() : 0)
             .arg(QString::fromUtf8(picked->GetXMLName() ? picked->GetXMLName() : "?"));
  }
  else
  {
    out += QStringLiteral("hit repr xml=%1\n")
             .arg(QString::fromUtf8(picked->GetXMLName() ? picked->GetXMLName() : "?"));
  }
  return out;
}

bool parsePickXY(const QJsonValue& v, double& x, double& y)
{
  if (v.isArray())
  {
    const QJsonArray arr = v.toArray();
    if (arr.size() < 2)
    {
      return false;
    }
    x = arr.at(0).toDouble();
    y = arr.at(1).toDouble();
    return true;
  }
  if (v.isObject())
  {
    const QJsonObject o = v.toObject();
    if (!o.contains(QStringLiteral("x")) || !o.contains(QStringLiteral("y")))
    {
      return false;
    }
    x = jsonDouble(o, "x");
    y = jsonDouble(o, "y");
    return true;
  }
  return false;
}

QString pickWorldPoint(const QJsonObject& args)
{
  pqView* view = pqActiveObjects::instance().activeView();
  if (!view || !view->getViewProxy())
  {
    return QStringLiteral("No active view.");
  }
  auto* rvp = vtkSMRenderViewProxy::SafeDownCast(view->getViewProxy());
  if (!rvp)
  {
    return QStringLiteral("Active view is not a RenderView.");
  }
  const int imgW = jsonInt(args, "image_width", 0);
  const int imgH = jsonInt(args, "image_height", 0);
  const QString origin = args.value(QStringLiteral("origin")).toString(QStringLiteral("top_left"));
  const bool snap = args.value(QStringLiteral("snap_mesh")).toBool(false);
  const QSize sz = view->getSize();
  const int vw = sz.width();
  const int vh = sz.height();
  if (vw <= 1 || vh <= 1)
  {
    return QStringLiteral("View size is invalid.");
  }

  QList<QPair<double, double>> pts;
  const QJsonValue pointsVal = args.value(QStringLiteral("points"));
  if (pointsVal.isArray())
  {
    const QJsonArray arr = pointsVal.toArray();
    for (const QJsonValue& v : arr)
    {
      double x = 0;
      double y = 0;
      if (parsePickXY(v, x, y))
      {
        pts.append(qMakePair(x, y));
      }
    }
  }
  if (pts.isEmpty() && args.contains(QStringLiteral("x")) && args.contains(QStringLiteral("y")))
  {
    pts.append(qMakePair(jsonDouble(args, "x"), jsonDouble(args, "y")));
  }
  if (pts.isEmpty())
  {
    return QStringLiteral(
      "Provide points=[{x,y}, ...] (preferred) or a single x and y. "
      "Put every brush mark in one call.");
  }
  if (pts.size() > kMaxPickPoints)
  {
    pts = pts.mid(0, kMaxPickPoints);
  }

  QString out;
  if (pts.size() > 1)
  {
    out += QStringLiteral("Picked %1 points in one call:\n").arg(pts.size());
  }
  for (int i = 0; i < pts.size(); ++i)
  {
    if (i > 0)
    {
      out += QLatin1Char('\n');
    }
    out += pickOneWorldPoint(
      rvp, vw, vh, pts[i].first, pts[i].second, imgW, imgH, origin, snap, i, pts.size());
  }
  return out;
}

QString inspectPipeline(const QJsonObject& args)
{
  const QString name = args.value(QStringLiteral("name")).toString();
  if (name.trimmed().isEmpty())
  {
    return pipelineTree();
  }
  const int port = jsonInt(args, "port", 0);
  const QString data = sourceData(name, port);
  if (data.startsWith(QLatin1String("Source not found")) ||
    data.startsWith(QLatin1String("Invalid port")) ||
    data.startsWith(QLatin1String("No active source")))
  {
    return data;
  }
  QString out = data;
  out += QLatin1Char('\n');
  out += sourceProperties(name);
  out += QLatin1Char('\n');
  out += blockStructure(name, port);
  return out;
}

QString inspectView()
{
  QString out = displayInfo();
  out += QLatin1Char('\n');
  out += colorMapInfo();
  out += QLatin1Char('\n');
  out += cameraInfo();
  out += QLatin1Char('\n');
  out += timeInfo();
  return out;
}

bool jsonNumberValue(const QJsonValue& v, double& out)
{
  if (v.isDouble())
  {
    out = v.toDouble();
    return std::isfinite(out);
  }
  if (v.isString())
  {
    bool ok = false;
    out = v.toString().trimmed().toDouble(&ok);
    return ok && std::isfinite(out);
  }
  return false;
}

bool readVec3(const QJsonValue& v, double out[3])
{
  if (v.isArray())
  {
    const QJsonArray a = v.toArray();
    if (a.size() < 3)
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      if (!jsonNumberValue(a.at(i), out[i]))
      {
        return false;
      }
    }
    return true;
  }
  if (v.isObject())
  {
    const QJsonObject o = v.toObject();
    return jsonNumberValue(o.value(QStringLiteral("x")), out[0]) &&
      jsonNumberValue(o.value(QStringLiteral("y")), out[1]) &&
      jsonNumberValue(o.value(QStringLiteral("z")), out[2]);
  }
  if (v.isString())
  {
    QString s = v.toString();
    s.replace(QLatin1Char(','), QLatin1Char(' '));
    const QStringList parts = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 3)
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      bool ok = false;
      out[i] = parts.at(i).toDouble(&ok);
      if (!ok || !std::isfinite(out[i]))
      {
        return false;
      }
    }
    return true;
  }
  return false;
}

bool readProp3(vtkSMProxy* proxy, const char* name, double out[3])
{
  vtkSMProperty* prop = proxy ? proxy->GetProperty(name) : nullptr;
  if (!prop)
  {
    return false;
  }
  vtkSMPropertyHelper h(prop);
  if (h.GetNumberOfElements() < 3)
  {
    return false;
  }
  for (int i = 0; i < 3; ++i)
  {
    out[i] = h.GetAsDouble(static_cast<unsigned int>(i));
  }
  return true;
}

bool setProp3(vtkSMProxy* proxy, const char* name, const double v[3])
{
  vtkSMProperty* prop = proxy ? proxy->GetProperty(name) : nullptr;
  if (!prop)
  {
    return false;
  }
  vtkSMPropertyHelper(prop).Set(v, 3);
  return true;
}

QString fmt3(const double v[3])
{
  return QStringLiteral("[%1, %2, %3]")
    .arg(v[0], 0, 'g', 6)
    .arg(v[1], 0, 'g', 6)
    .arg(v[2], 0, 'g', 6);
}

bool jsonBoolValue(const QJsonObject& args, const char* key, bool def)
{
  if (!args.contains(QLatin1String(key)))
  {
    return def;
  }
  const QJsonValue v = args.value(QLatin1String(key));
  if (v.isBool())
  {
    return v.toBool();
  }
  if (v.isDouble())
  {
    return v.toInt() != 0;
  }
  if (v.isString())
  {
    const QString s = v.toString().trimmed().toLower();
    if (s == QLatin1String("false") || s == QLatin1String("0") || s == QLatin1String("no") ||
      s == QLatin1String("off"))
    {
      return false;
    }
    if (s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes") ||
      s == QLatin1String("on"))
    {
      return true;
    }
  }
  return def;
}

bool boundsCenter(vtkPVDataInformation* di, double center[3])
{
  if (!di)
  {
    return false;
  }
  double b[6] = { 0, 0, 0, 0, 0, 0 };
  di->GetBounds(b);
  if (!(b[0] <= b[1] && b[2] <= b[3] && b[4] <= b[5]))
  {
    return false;
  }
  if (!std::isfinite(b[0]) || std::abs(b[0]) > 1e299)
  {
    return false;
  }
  center[0] = 0.5 * (b[0] + b[1]);
  center[1] = 0.5 * (b[2] + b[3]);
  center[2] = 0.5 * (b[4] + b[5]);
  return std::isfinite(center[0]) && std::isfinite(center[1]) && std::isfinite(center[2]);
}

QString applyScreenshotCameraImpl(const QJsonObject& args)
{
  if (!args.contains(QStringLiteral("camera")) || args.value(QStringLiteral("camera")).isNull())
  {
    return {};
  }
  if (!args.value(QStringLiteral("camera")).isObject())
  {
    return QStringLiteral(
      "Error: camera must be {focal_point:[x,y,z], position:[x,y,z], view_angle:degrees}.");
  }
  const QJsonObject cam = args.value(QStringLiteral("camera")).toObject();
  if (cam.isEmpty())
  {
    return {};
  }

  const bool hasFocal = cam.contains(QStringLiteral("focal_point"));
  const bool hasPos = cam.contains(QStringLiteral("position"));
  const bool hasAngle = cam.contains(QStringLiteral("view_angle"));
  if (!hasFocal && !hasPos && !hasAngle)
  {
    return QStringLiteral("Error: camera needs focal_point, position, and/or view_angle.");
  }

  pqView* view = pqActiveObjects::instance().activeView();
  vtkSMProxy* proxy = view ? view->getProxy() : nullptr;
  if (!proxy || !proxy->GetProperty("CameraPosition") || !proxy->GetProperty("CameraFocalPoint"))
  {
    return QStringLiteral("Error: no active RenderView camera.");
  }

  double focal[3] = { 0, 0, 0 };
  double pos[3] = { 0, 0, 0 };
  if (hasFocal || hasPos)
  {
    if (!readProp3(proxy, "CameraFocalPoint", focal) || !readProp3(proxy, "CameraPosition", pos))
    {
      return QStringLiteral("Error: the active view did not report a camera.");
    }
    if (hasFocal && !readVec3(cam.value(QStringLiteral("focal_point")), focal))
    {
      return QStringLiteral("Error: camera.focal_point must be [x, y, z].");
    }
    if (hasPos && !readVec3(cam.value(QStringLiteral("position")), pos))
    {
      return QStringLiteral("Error: camera.position must be [x, y, z].");
    }
    const double dx = focal[0] - pos[0];
    const double dy = focal[1] - pos[1];
    const double dz = focal[2] - pos[2];
    if ((dx * dx + dy * dy + dz * dz) < 1e-24)
    {
      return QStringLiteral("Error: camera.position and camera.focal_point are the same point.");
    }
  }

  double angle = 30.0;
  if (hasAngle)
  {
    if (!jsonNumberValue(cam.value(QStringLiteral("view_angle")), angle) || angle <= 0.0 ||
      angle >= 180.0)
    {
      return QStringLiteral(
        "Error: camera.view_angle must be degrees, greater than 0 and less than 180.");
    }
    if (!proxy->GetProperty("CameraViewAngle"))
    {
      return QStringLiteral("Error: this view has no CameraViewAngle.");
    }
  }

  bool adjustedUp = false;
  double up[3] = { 0, 1, 0 };
  if ((hasFocal || hasPos) && readProp3(proxy, "CameraViewUp", up))
  {
    double dir[3] = { focal[0] - pos[0], focal[1] - pos[1], focal[2] - pos[2] };
    if (vtkMath::Normalize(dir) > 0.0)
    {
      double upn[3] = { up[0], up[1], up[2] };
      if (vtkMath::Normalize(upn) > 0.0 && std::fabs(vtkMath::Dot(dir, upn)) > 0.99)
      {
        double alt[3] = { 0.0, 0.0, 1.0 };
        if (std::fabs(dir[2]) > 0.9)
        {
          alt[1] = 1.0;
          alt[2] = 0.0;
        }
        const double d = vtkMath::Dot(alt, dir);
        up[0] = alt[0] - d * dir[0];
        up[1] = alt[1] - d * dir[1];
        up[2] = alt[2] - d * dir[2];
        vtkMath::Normalize(up);
        adjustedUp = true;
      }
    }
  }

  if (hasFocal)
  {
    setProp3(proxy, "CameraFocalPoint", focal);
  }
  if (hasPos)
  {
    setProp3(proxy, "CameraPosition", pos);
  }
  if (adjustedUp)
  {
    setProp3(proxy, "CameraViewUp", up);
  }
  bool clearedParallel = false;
  if (hasAngle)
  {
    vtkSMPropertyHelper(proxy->GetProperty("CameraViewAngle")).Set(angle);
    if (vtkSMProperty* par = proxy->GetProperty("CameraParallelProjection"))
    {
      vtkSMPropertyHelper h(par);
      if (h.GetNumberOfElements() > 0 && h.GetAsInt() != 0)
      {
        h.Set(0);
        clearedParallel = true;
      }
    }
  }

  proxy->UpdateVTKObjects();
  view->render();

  QString note = QStringLiteral("Camera");
  if (hasFocal)
  {
    note += QStringLiteral(" focal_point=%1").arg(fmt3(focal));
  }
  if (hasPos)
  {
    note += QStringLiteral(" position=%1").arg(fmt3(pos));
  }
  if (hasAngle)
  {
    note += QStringLiteral(" view_angle=%1").arg(angle, 0, 'g', 6);
  }
  if (adjustedUp)
  {
    note += QStringLiteral(" (view-up adjusted; it was parallel to the view direction)");
  }
  if (clearedParallel)
  {
    note += QStringLiteral(" (parallel projection turned off so view_angle applies)");
  }
  return note;
}

struct ScreenshotMark
{
  QString text;
  double x = 0;
  double y = 0;
};

QString annotateScreenshotTargetsImpl(QImage& image, const QJsonObject& args)
{
  if (!args.contains(QStringLiteral("targets")) || args.value(QStringLiteral("targets")).isNull())
  {
    return {};
  }
  QJsonArray items;
  const QJsonValue raw = args.value(QStringLiteral("targets"));
  if (raw.isArray())
  {
    items = raw.toArray();
  }
  else if (raw.isString() || raw.isObject())
  {
    items.append(raw);
  }
  else
  {
    return QStringLiteral("Targets: expected a list of names or {name, position}.");
  }
  if (items.isEmpty())
  {
    return {};
  }

  const bool drawLabels = jsonBoolValue(args, "label", true);
  constexpr int kMaxTargets = 24;
  const int n = std::min(static_cast<int>(items.size()), kMaxTargets);

  pqView* view = pqActiveObjects::instance().activeView();
  auto* rvp = view ? vtkSMRenderViewProxy::SafeDownCast(view->getProxy()) : nullptr;
  vtkRenderer* ren = rvp ? rvp->GetRenderer() : nullptr;
  vtkCamera* camera = ren ? ren->GetActiveCamera() : nullptr;
  const QSize viewSize = view ? view->getSize() : QSize();
  if (!ren || !camera || viewSize.width() < 1 || viewSize.height() < 1 || image.isNull())
  {
    return QStringLiteral("Targets: could not project onto the view.");
  }

  const int* renSize = ren->GetSize();
  const double vw = viewSize.width();
  const double vh = viewSize.height();
  const double scaleX =
    (renSize && renSize[0] > 0 && renSize[0] != viewSize.width()) ? vw / renSize[0] : 1.0;
  const double scaleY =
    (renSize && renSize[1] > 0 && renSize[1] != viewSize.height()) ? vh / renSize[1] : 1.0;

  double camPos[3] = { 0, 0, 0 };
  double camFp[3] = { 0, 0, 0 };
  camera->GetPosition(camPos);
  camera->GetFocalPoint(camFp);
  const double forward[3] = { camFp[0] - camPos[0], camFp[1] - camPos[1], camFp[2] - camPos[2] };

  QStringList lines;
  QList<ScreenshotMark> marks;
  for (int i = 0; i < n; ++i)
  {
    const QJsonValue item = items.at(i);
    QString text;
    bool hasPoint = false;
    double world[3] = { 0, 0, 0 };
    if (item.isString())
    {
      text = item.toString().trimmed();
    }
    else if (item.isObject())
    {
      const QJsonObject o = item.toObject();
      text = o.value(QStringLiteral("name")).toString().trimmed();
      if (text.isEmpty())
      {
        text = o.value(QStringLiteral("label")).toString().trimmed();
      }
      if (o.contains(QStringLiteral("position")))
      {
        if (!readVec3(o.value(QStringLiteral("position")), world))
        {
          lines << QStringLiteral("- %1: position must be [x, y, z]")
                     .arg(text.isEmpty() ? QStringLiteral("(no name)") : text);
          continue;
        }
        hasPoint = true;
      }
    }
    else
    {
      lines << QStringLiteral("- target %1: expected a name or {name, position}").arg(i + 1);
      continue;
    }
    if (text.isEmpty())
    {
      lines << QStringLiteral("- target %1: missing name").arg(i + 1);
      continue;
    }
    if (!hasPoint)
    {
      pqPipelineSource* src = findSourceByName(text);
      if (!src)
      {
        lines << QStringLiteral("- %1: source not found").arg(text);
        continue;
      }
      pqOutputPort* op = src->getOutputPort(0);
      if (!boundsCenter(op ? op->getDataInformation() : nullptr, world))
      {
        lines << QStringLiteral("- %1: no data bounds").arg(src->getSMName());
        continue;
      }
      text = src->getSMName();
    }

    const double toPt[3] = { world[0] - camPos[0], world[1] - camPos[1], world[2] - camPos[2] };
    if (vtkMath::Dot(forward, toPt) <= 1e-8)
    {
      lines << QStringLiteral("- %1 world=%2 behind the camera").arg(text, fmt3(world));
      continue;
    }
    ren->SetWorldPoint(world[0], world[1], world[2], 1.0);
    ren->WorldToDisplay();
    const double* disp = ren->GetDisplayPoint();
    const double dx = disp[0] * scaleX;
    const double dy = disp[1] * scaleY;
    const double jpegX = dx * static_cast<double>(image.width()) / vw;
    const double jpegY = (vh - 1.0 - dy) * static_cast<double>(image.height()) / vh;
    const bool inside =
      jpegX >= 0.0 && jpegY >= 0.0 && jpegX < image.width() && jpegY < image.height();
    QString where = inside ? (drawLabels ? QStringLiteral(" labeled") : QStringLiteral(" label=off"))
                           : QStringLiteral(" outside the image");
    lines << QStringLiteral("- %1 world=%2 pixel=(%3, %4)%5")
               .arg(text, fmt3(world))
               .arg(static_cast<int>(std::lround(jpegX)))
               .arg(static_cast<int>(std::lround(jpegY)))
               .arg(where);
    if (drawLabels && inside)
    {
      ScreenshotMark mark;
      mark.text = text;
      mark.x = jpegX;
      mark.y = jpegY;
      marks.append(mark);
    }
  }
  if (items.size() > n)
  {
    lines << QStringLiteral("- %1 more targets skipped").arg(items.size() - n);
  }

  if (!marks.isEmpty())
  {
    QImage painted = image.convertToFormat(QImage::Format_RGB32);
    QPainter painter(&painted);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setPixelSize(14);
    painter.setFont(font);
    const QFontMetrics fm(font);
    QList<QRect> placed;
    for (const ScreenshotMark& mark : marks)
    {
      const QString shown =
        mark.text.size() > 48 ? mark.text.left(47) + QStringLiteral("...") : mark.text;
      const int textW = fm.horizontalAdvance(shown);
      const int textH = fm.height();
      QRect box(static_cast<int>(std::lround(mark.x)) + 8,
        static_cast<int>(std::lround(mark.y)) - textH - 10, textW + 8, textH + 4);
      if (box.right() >= painted.width() - 1)
      {
        box.moveRight(painted.width() - 2);
      }
      if (box.left() < 2)
      {
        box.moveLeft(2);
      }
      if (box.top() < 2)
      {
        box.moveTop(static_cast<int>(std::lround(mark.y)) + 8);
      }
      if (box.bottom() >= painted.height() - 1)
      {
        box.moveBottom(painted.height() - 2);
      }
      for (int nudge = 0; nudge < 6; ++nudge)
      {
        bool hit = false;
        for (const QRect& prev : placed)
        {
          if (prev.intersects(box))
          {
            hit = true;
            break;
          }
        }
        if (!hit)
        {
          break;
        }
        box.translate(0, box.height() + 2);
        if (box.bottom() >= painted.height() - 1)
        {
          box.moveTop(2);
        }
      }
      placed.append(box);
      painter.setPen(QPen(Qt::black, 2));
      painter.setBrush(QColor(255, 213, 74));
      painter.drawEllipse(QPointF(mark.x, mark.y), 4.0, 4.0);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0, 0, 0, 180));
      painter.drawRoundedRect(box, 3, 3);
      painter.setPen(Qt::white);
      painter.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, shown);
    }
    painter.end();
    image = painted;
  }

  return QStringLiteral("Targets (JPEG pixels, origin=top_left):\n%1").arg(lines.join(QLatin1Char('\n')));
}
}

QString pqSHYXAIAgentTools::applyScreenshotCamera(const QJsonObject& args)
{
  return applyScreenshotCameraImpl(args);
}

QString pqSHYXAIAgentTools::annotateScreenshotTargets(QImage& image, const QJsonObject& args)
{
  return annotateScreenshotTargetsImpl(image, args);
}

QJsonArray pqSHYXAIAgentTools::schema(bool allowCaptureScreenshot)
{
  QJsonArray tools;
  tools.append(fn("inspect_pipeline",
    "Live pipeline. Omit name for the tree (FindSource names, XML types, inputs, ports, "
    "point/cell counts; the active source is marked). Pass name for that node's arrays/bounds, "
    "property values including nested proxies (ClipType.Origin/Normal), and PDC/multiblock "
    "hierarchy. Optional port for extra outputs (Mesh Checker / Remesher / End Clipper). "
    "Use before FindSource, ExtractBlock, or editing an existing filter.",
    QJsonObject{ { QStringLiteral("name"),
        strArg("Pipeline object name (FindSource name). Omit for the tree.") },
      { QStringLiteral("port"), intArg("Output port index when name is set. Default 0.") } }));
  tools.append(fn("inspect_selection",
    "Current 3D/spreadsheet selection: source, port, counts, bounds, IDs/CompositeIDs "
    "(truncated), field type. Use after title-bar/context-menu selection tools and before "
    "SHYX Delete/Extrude/Flip/Fill/Plane Clipper scripts."));
  tools.append(fn("inspect_view",
    "Active representation (type, visibility, opacity, ColorArrayName, PG_/AS_/PL_/HG_ values), "
    "lookup table (range, log, vector mode), camera, and animation time/timesteps. "
    "Use before Display / LUT / camera / FTLE / streamline edits. Prefer this over a screenshot "
    "when pixels are not needed."));
  tools.append(fn("get_output_window",
    "Recent ParaView Output Window errors and warnings. Call this when diagnosing a failed "
    "script, a missing array, or unexpected filter output. Do not call it on every turn."));
  if (allowCaptureScreenshot)
  {
    QJsonObject vec3Items{ { QStringLiteral("type"), QStringLiteral("number") } };
    auto vec3Arg = [&vec3Items](const char* desc) {
      return QJsonObject{ { QStringLiteral("type"), QStringLiteral("array") },
        { QStringLiteral("description"), QString::fromUtf8(desc) },
        { QStringLiteral("items"), vec3Items }, { QStringLiteral("minItems"), 3 },
        { QStringLiteral("maxItems"), 3 } };
    };
    QJsonObject camProps{ { QStringLiteral("focal_point"),
                            vec3Arg("[x, y, z] CameraFocalPoint, the look-at point.") },
      { QStringLiteral("position"), vec3Arg("[x, y, z] CameraPosition.") },
      { QStringLiteral("view_angle"),
        numArg("CameraViewAngle in degrees, greater than 0 and less than 180. "
               "If the view is in parallel projection, it is switched to perspective so this "
               "angle changes the framing.") } };
    QJsonObject targetProps{ { QStringLiteral("name"),
                               strArg("Label text. Without position, a pipeline source name; the "
                                      "label sits at that source's bounds center.") },
      { QStringLiteral("position"),
        vec3Arg("Optional world point [x, y, z]. The name is then only the label text.") } };
    QJsonObject props{ { QStringLiteral("camera"),
                         QJsonObject{ { QStringLiteral("type"), QStringLiteral("object") },
                           { QStringLiteral("description"),
                             QStringLiteral(
                               "Camera applied on the active RenderView before the JPEG. "
                               "Omit to keep the current camera. Set only the fields to change.") },
                           { QStringLiteral("properties"), camProps } } },
      { QStringLiteral("targets"),
        QJsonObject{ { QStringLiteral("type"), QStringLiteral("array") },
          { QStringLiteral("description"),
            QStringLiteral(
              "Marks on the JPEG. Each item is a pipeline source name, or "
              "{name, position:[x,y,z]}. label=true draws the names. This does not hide other objects.") },
          { QStringLiteral("items"),
            QJsonObject{ { QStringLiteral("anyOf"),
              QJsonArray{ QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } },
                QJsonObject{ { QStringLiteral("type"), QStringLiteral("object") },
                  { QStringLiteral("properties"), targetProps } } } } } } } },
      { QStringLiteral("label"),
        boolArg("Draw target names on the JPEG. Default true. false still returns each target's "
                "JPEG pixel.") } };
    tools.append(fn("capture_screenshot",
      "Capture the active RenderView as a JPEG. The image is attached on this turn. "
      "Pass the camera in this call: camera={focal_point:[x,y,z], position:[x,y,z], view_angle:degrees}. "
      "Omit camera to keep the current view. Any subset of the three fields is applied. "
      "targets lists pipeline source names, or {name, position:[x,y,z]}. "
      "label=true (default) draws each target name on the JPEG and does not hide other objects. "
      "The result includes JPEG width/height for pick_world_point.",
      props));
  }
  tools.append(fn("pick_world_point",
    "Convert 2D screenshot clicks (brush marks) to 3D world points. Call this when the user "
    "attached a screenshot. Put every mark in ONE call: points=[{x,y}, {x,y}, ...], with "
    "image_width/image_height from the JPEG (origin=top_left).",
    QJsonObject{ { QStringLiteral("points"),
        QJsonObject{ { QStringLiteral("type"), QStringLiteral("array") },
          { QStringLiteral("description"),
            QStringLiteral("All clicks in one call. Each item is {x,y} or [x,y] in image pixels "
                           "(with image_width/height) or 0-1 normalized.") },
          { QStringLiteral("items"),
            QJsonObject{ { QStringLiteral("type"), QStringLiteral("object") },
              { QStringLiteral("properties"),
                QJsonObject{ { QStringLiteral("x"), numArg("X") },
                  { QStringLiteral("y"), numArg("Y") } } },
              { QStringLiteral("required"),
                QJsonArray{ QStringLiteral("x"), QStringLiteral("y") } } } } } },
      { QStringLiteral("x"),
        numArg("Single-point X. Prefer points[] when there is more than one click.") },
      { QStringLiteral("y"), numArg("Single-point Y. Prefer points[] for multiple clicks.") },
      { QStringLiteral("image_width"),
        intArg("JPEG/screenshot width. Maps x into the view. Shared by all points.") },
      { QStringLiteral("image_height"), intArg("JPEG/screenshot height. Shared by all points.") },
      { QStringLiteral("origin"),
        strArg("top_left (default, screenshots) or vtk (bottom-left display coords).") },
      { QStringLiteral("snap_mesh"), boolArg("If true, snap to a mesh point. Default false.") } }));
  tools.append(fn("get_code_script",
    "Return the current contents of the code box (the ParaView Python script)."));
  QJsonObject setCodeProps;
  setCodeProps.insert(QStringLiteral("code"),
    QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") },
      { QStringLiteral("description"),
        QStringLiteral("Full ParaView Python script to store in the code box. No markdown fences.") } });
  tools.append(fn("set_code_script",
    "Replace the code box with a complete ParaView Python script. Always write the full script, "
    "not a patch. After this, you MUST call run_code_script in the same turn (or immediately next) "
    "and read errors before talking to the user.",
    setCodeProps, QJsonArray{ QStringLiteral("code") }));
  QJsonObject runProps;
  QString runDesc =
    QStringLiteral("Execute the current code box (same as the Run script button). Always call this "
                   "after set_code_script so you can see whether the script actually works. Returns "
                   "new Output Window lines, active-source data, and the pipeline tree after the "
                   "run. If the run errors, data looks wrong, or the pipeline grew duplicate "
                   "filters (two Clips on the same input), fix with set_code_script: FindSource "
                   "the existing node and set properties, or Delete the extra node, then run again.");
  if (allowCaptureScreenshot)
  {
    runProps.insert(QStringLiteral("capture"),
      QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") },
        { QStringLiteral("description"),
          QStringLiteral("If true, attach a RenderView JPEG after the script runs.") } });
    runDesc += QStringLiteral(" capture=true attaches a screenshot after the run.");
  }
  const QByteArray runDescUtf8 = runDesc.toUtf8();
  tools.append(fn("run_code_script", runDescUtf8.constData(), runProps));
  tools.append(fn("list_filters",
    "Search registered ParaView proxies. Empty query (or features/catalog) lists SHYX/VESPA "
    "filters/sources AND Display representations (Pulse Glyphs, Animated Streamline, Point Label, Hologram) "
    "plus a reminder of title-bar selection tools. Non-empty query: several keywords separated by "
    "spaces or commas; ALL keywords must match (AND) across XML name, label, menu, short docs, "
    "display name, and SHYX usage notes. Representations are not python constructors.",
    QJsonObject{ { QStringLiteral("query"),
      strArg("Keywords (AND): remesh clip, mesh repair, Point Label, SHYXMeshChecker. Empty = catalog.") } }));
  tools.append(fn("describe_proxy",
    "Property schema: types, defaults, enums, docs. For filters: XML/python name. "
    "For SHYX Display types pass 'Pulse Glyphs', 'Animated Streamline', 'Point Label', or 'Hologram' "
    "(returns exposed Python names PG_*/AS_*/PL_*/HG_*). "
    "For title-bar tools pass 'sphere', 'polygon', 'grow', or 'proximity gap' (no SM proxy). "
    "For the block context-menu action pass 'select block'. "
    "For the right-click Pick Center (center of rotation at that click, no second left-click) pass 'pick center'. "
    "For Select Similar / By Normal pass 'select similar'. "
    "For Fill Interior pass 'fill interior'. "
    "For Select Connected (connected points/lines/faces/volumes) pass 'select connected'. "
    "For Invert Selection pass 'invert'. "
    "For the File → Recovery submenu (temp-folder .pvsm list, including the automatic "
    "shyx_checkpoint.pvsm) pass 'recovery' or 'checkpoint'. "
    "For the RenderView Delete-key shortcut that creates and applies SHYX Delete Selected Cells, pass 'delete shortcut' or 'delete key'. "
    "For the quick-filter wheel (radial menu in the RenderView, opened by holding ` / ·; its trailing \"+\" sector opens a checklist of every SHYX and ParaView / VESPA built-in filter with an Input, and the checked set persists), pass 'quick filter wheel'. "
    "For the assistant Memory button (Agent.md and skill files) pass 'memory'.",
    QJsonObject{ { QStringLiteral("name"), strArg("XML name, display type (Pulse Glyphs), sphere/polygon/grow/proximity gap, select block, select similar, fill interior, select connected, invert, pick center, recovery/checkpoint, delete shortcut, quick filter wheel, or memory.") } },
    QJsonArray{ QStringLiteral("name") }));
  tools.append(fn("lookup_shyx_docs",
    "SHYX/VESPA usage notes: capability catalog (filters + Display representations + "
    "title-bar selection tools + File → Recovery), vascular pipeline order, which filter to prefer, multi-port hints, "
    "and live XML short help. Empty query is the full catalog. Keywords are AND across name/docs/"
    "notes (spaces or commas). Then call describe_proxy for property names.",
    QJsonObject{ { QStringLiteral("query"),
      strArg("Empty for catalog, or keywords: vascular remesh, representation, selection, ...") } }));
  tools.append(fn("read_skill",
    "Read one saved skill (frontmatter plus body). Call this before following a workflow in the "
    "skill catalog. Pass the catalog id.",
    QJsonObject{ { QStringLiteral("name"),
      strArg("Skill id from the catalog, e.g. vascular-cfd-mesh.") } },
    QJsonArray{ QStringLiteral("name") }));
  QJsonObject saveSkillProps;
  saveSkillProps.insert(QStringLiteral("name"),
    strArg("Short skill name, e.g. vascular-cfd-mesh. A name that already exists is saved as a new file."));
  saveSkillProps.insert(QStringLiteral("description"),
    strArg("One line: when to read this skill. This is the only skill text shown in the catalog."));
  saveSkillProps.insert(QStringLiteral("body"),
    strArg("Markdown workflow. This user's array names, skipped steps, and parameters that worked. "
           "Do not paste lookup_shyx_docs."));
  tools.append(fn("save_skill",
    "Save a new skill file. Call only when the user asks to remember a workflow. Never overwrites: "
    "a name that already exists is saved under a new name. To change an existing skill, call "
    "update_skill. One skill is one workflow. Filter "
    "parameters stay in describe_proxy / lookup_shyx_docs.",
    saveSkillProps,
    QJsonArray{ QStringLiteral("name"), QStringLiteral("description"), QStringLiteral("body") }));
  QJsonObject updateSkillProps;
  updateSkillProps.insert(QStringLiteral("name"),
    strArg("Existing skill id from the catalog, e.g. vascular-cfd-mesh. Does not create or rename."));
  updateSkillProps.insert(QStringLiteral("description"),
    strArg("Replacement catalog line. Omit to keep the current description."));
  updateSkillProps.insert(QStringLiteral("body"),
    strArg("Full revised markdown workflow, not a patch. Call read_skill first. "
           "Omit to change only the description. Do not paste lookup_shyx_docs."));
  tools.append(fn("update_skill",
    "Overwrite an existing skill. Call only when the user asks to change a saved workflow. "
    "Read it with read_skill first, then pass the full revised body. Does not create a file and "
    "does not rename it. Omit description to keep the catalog line, or omit body to change only "
    "that line. Filter parameters stay in describe_proxy / lookup_shyx_docs.",
    updateSkillProps,
    QJsonArray{ QStringLiteral("name") }));
  tools.append(fn("write_agent_memory",
    "Replace Agent.md, the short notes included in every request. Call only when the user asks to "
    "remember a preference that applies to every task. Pass the full new text, including notes to "
    "keep. Max 4000 characters. Do not store a pipeline procedure here; use save_skill.",
    QJsonObject{ { QStringLiteral("text"),
      strArg("Full replacement text for Agent.md. Max 4000 characters.") } },
    QJsonArray{ QStringLiteral("text") }));
  return tools;
}

QString pqSHYXAIAgentTools::run(const QString& name, const QJsonObject& args)
{
  if (name == QLatin1String("inspect_pipeline"))
  {
    return inspectPipeline(args);
  }
  if (name == QLatin1String("get_pipeline_tree"))
  {
    return pipelineTree();
  }
  if (name == QLatin1String("get_active_data") || name == QLatin1String("get_source_data"))
  {
    return sourceData(args.value(QStringLiteral("name")).toString(), jsonInt(args, "port", 0));
  }
  if (name == QLatin1String("inspect_selection") || name == QLatin1String("get_selection_ids"))
  {
    return selectionIds();
  }
  if (name == QLatin1String("get_selection"))
  {
    return selectionInfo();
  }
  if (name == QLatin1String("inspect_view"))
  {
    return inspectView();
  }
  if (name == QLatin1String("get_display"))
  {
    return displayInfo();
  }
  if (name == QLatin1String("get_color_map"))
  {
    return colorMapInfo();
  }
  if (name == QLatin1String("get_camera"))
  {
    return cameraInfo();
  }
  if (name == QLatin1String("get_time"))
  {
    return timeInfo();
  }
  if (name == QLatin1String("get_output_window"))
  {
    const QString errors = pqSHYXAIOutputLog::instance()->recentErrors();
    return errors.isEmpty() ? QStringLiteral("(none)") : errors;
  }
  if (name == QLatin1String("get_source_properties"))
  {
    return sourceProperties(args.value(QStringLiteral("name")).toString());
  }
  if (name == QLatin1String("get_blocks"))
  {
    return blockStructure(args.value(QStringLiteral("name")).toString(), jsonInt(args, "port", 0));
  }
  if (name == QLatin1String("list_filters"))
  {
    return listFilters(args.value(QStringLiteral("query")).toString());
  }
  if (name == QLatin1String("describe_proxy"))
  {
    return describeProxy(args.value(QStringLiteral("name")).toString());
  }
  if (name == QLatin1String("lookup_shyx_docs"))
  {
    return lookupShyxDocs(args.value(QStringLiteral("query")).toString());
  }
  if (name == QLatin1String("read_skill"))
  {
    return pqSHYXAIMemory::readSkill(args.value(QStringLiteral("name")).toString());
  }
  if (name == QLatin1String("save_skill"))
  {
    return pqSHYXAIMemory::saveSkill(args.value(QStringLiteral("name")).toString(),
      args.value(QStringLiteral("description")).toString(), args.value(QStringLiteral("body")).toString());
  }
  if (name == QLatin1String("update_skill"))
  {
    return pqSHYXAIMemory::updateSkill(args.value(QStringLiteral("name")).toString(),
      args.value(QStringLiteral("description")).toString(), args.value(QStringLiteral("body")).toString());
  }
  if (name == QLatin1String("write_agent_memory"))
  {
    return pqSHYXAIMemory::writeAgentMemory(args.value(QStringLiteral("text")).toString());
  }
  if (name == QLatin1String("pick_world_point"))
  {
    return pickWorldPoint(args);
  }
  if (name == QLatin1String("capture_screenshot"))
  {
    return QStringLiteral("__CAPTURE_SCREENSHOT__");
  }
  return QStringLiteral("Unknown tool: %1").arg(name);
}
