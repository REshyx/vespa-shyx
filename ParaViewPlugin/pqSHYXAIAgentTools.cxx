#include "pqSHYXAIAgentTools.h"

#include "pqSHYXAIOutputLog.h"

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
#include "vtkSMRenderViewProxy.h"
#include "vtkSMRepresentationProxy.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSmartPointer.h"

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
    out += QStringLiteral("- %1  xml=%2\n")
             .arg(src->getSMName())
             .arg(QString::fromUtf8(src->getProxy()->GetXMLName()));
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
  out += QStringLiteral("  (LUT details: call get_color_map)\n");
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

bool queryHits(const QString& query, const QStringList& fields)
{
  if (query.isEmpty())
  {
    return true;
  }
  for (const QString& f : fields)
  {
    if (f.contains(query, Qt::CaseInsensitive))
    {
      return true;
    }
  }
  return false;
}

int hitScore(const QString& query, const QString& xml, const QString& label)
{
  if (query.isEmpty())
  {
    return xml.startsWith(QLatin1String("SHYX")) ? 20 : 5;
  }
  int s = 0;
  if (xml.compare(query, Qt::CaseInsensitive) == 0)
  {
    s += 100;
  }
  else if (xml.startsWith(query, Qt::CaseInsensitive))
  {
    s += 60;
  }
  else if (xml.contains(query, Qt::CaseInsensitive))
  {
    s += 30;
  }
  if (label.compare(query, Qt::CaseInsensitive) == 0)
  {
    s += 80;
  }
  else if (label.contains(query, Qt::CaseInsensitive))
  {
    s += 25;
  }
  if (xml.startsWith(QLatin1String("SHYX")))
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
  const QString query = queryRaw.trimmed();
  vtkSmartPointer<vtkPVProxyDefinitionIterator> it;
  it.TakeReference(defs->NewIterator());
  if (!it)
  {
    return QStringLiteral("Could not iterate proxy definitions.");
  }
  it->AddTraversalGroupName("filters");
  it->AddTraversalGroupName("sources");

  struct Hit
  {
    int score;
    QString line;
  };
  QList<Hit> hits;
  for (it->GoToFirstItem(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    const char* group = it->GetGroupName();
    const char* xmlc = it->GetProxyName();
    if (!group || !xmlc)
    {
      continue;
    }
    const QString xml = QString::fromUtf8(xmlc);
    vtkPVXMLElement* def = it->GetProxyDefinition();
    const char* labelc = def ? def->GetAttribute("label") : nullptr;
    const QString label = labelc ? QString::fromUtf8(labelc) : xml;
    const QString menu = menuFromHints(it->GetProxyHints() ? it->GetProxyHints()
                                                           : (def ? def->FindNestedElementByName("Hints") : nullptr));
    const QString doc = xmlDocSummary(def, 90);
    const bool shyxish = xml.startsWith(QLatin1String("SHYX")) ||
      xml.startsWith(QLatin1String("CGAL")) || xml.startsWith(QLatin1String("vtkCGAL")) ||
      xml.contains(QLatin1String("VESPA"), Qt::CaseInsensitive) ||
      label.startsWith(QLatin1String("SHYX")) || label.startsWith(QLatin1String("VESPA")) ||
      label.contains(QLatin1String("CGAL"), Qt::CaseInsensitive);
    if (query.isEmpty() && !shyxish)
    {
      continue;
    }
    if (!queryHits(query, { xml, label, menu, doc, QString::fromUtf8(group) }))
    {
      continue;
    }
    const int score = hitScore(query, xml, label);
    if (score <= 0 && !query.isEmpty())
    {
      continue;
    }
    QString line = QStringLiteral("- %1()  label=%2  group=%3")
                     .arg(xml, label, QString::fromUtf8(group));
    if (!menu.isEmpty())
    {
      line += QStringLiteral("  menu=%1").arg(menu);
    }
    if (!doc.isEmpty())
    {
      line += QStringLiteral("\n    %1").arg(doc);
    }
    hits.push_back(Hit{ score, line });
  }
  std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
  QString out;
  if (query.isEmpty())
  {
    out += QStringLiteral("SHYX/VESPA filters and sources (pass query to search all ParaView proxies):\n");
  }
  else
  {
    out += QStringLiteral("Filters/sources matching %1:\n").arg(query);
  }
  const int n = std::min(static_cast<int>(hits.size()), kMaxFilterHits);
  if (n == 0)
  {
    return out + QStringLiteral("(none)\n");
  }
  for (int i = 0; i < n; ++i)
  {
    out += hits[i].line;
    out += QLatin1Char('\n');
  }
  if (hits.size() > n)
  {
    out += QStringLiteral("... %1 more; narrow the query.\n").arg(hits.size() - n);
  }
  out += QStringLiteral("Python: from paraview.simple import *; Name(Input=..., registrationName='...')\n");
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
    const QString label = labelc ? QString::fromUtf8(labelc) : xml;
    const int score = hitScore(q, xml, label);
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

void describeOneProperty(QString& out, vtkSMProperty* prop, const char* pname, int indent)
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
  QString line = QStringLiteral("%1%2  type=%3").arg(pad, QString::fromUtf8(pname), propTypeShort(prop));
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

QString describeProxy(const QString& query)
{
  QString group;
  QString xml;
  vtkSMProxy* proto = findPrototype(query, group, xml);
  if (!proto)
  {
    return QStringLiteral("No proxy definition matching '%1'. Try list_filters.").arg(query.trimmed());
  }
  QString out = QStringLiteral("Proxy %1  group=%2\nPython: %3()\n").arg(xml, group, xml);
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
    "(soup edges / boundary rings / self-intersections)." },
  { "SHYXBooleanOperationFilter", "Relaxed boolean; open meshes OK. Strict watertight meshes can use VESPA Boolean." },
  { "SHYXHoleFillFilter", "SHYX hole fill; new pipelines prefer this over VESPA Hole Filling." },
  { "SHYXShapeSmoothing", "Three algorithms (MCF / Angle&Area / Fair). VESPA Shape Smoothing is MCF only." },
  { "SHYXAdaptiveIsotropicRemesher",
    "Curvature-adaptive remesh (CGAL>=6). Ports: remeshed, sharp features, mask patch, sizing preview. "
    "Uniform target edge length: VESPA Isotropic Remesher." },
  { "SHYXRemeshWithEndpoint", "Vascular step 4: optional endpoint cull then ICC remesh / cap." },
  { "SHYXSkeletonExtraction", "Vascular step 1. Input must be watertight triangle mesh." },
  { "SHYXVesselEndClipper", "Vascular step 2. Port0 clipped mesh, port1 clip planes (Point Label)." },
  { "SHYXSelectionPlaneClipper", "Vascular step 3. Uses current selection / interactive plane." },
  { "SHYXTetGen", "Vascular step 5. Closed triangle surface -> tetrahedra." },
  { "SHYXDataSetToPartitionedCollection", "Vascular step 6. Convert dataset to PDC; then Boundary Assignment." },
  { "SHYXPartitionedCollectionBoundaryAssignment",
    "Vascular step 7. Call get_blocks on the PDC first. Port0 collection, port1 assignment debug." },
  { "SHYXPartitionedCollectionBoundaryFields", "Adds boundary field arrays on an assigned PDC." },
  { "SHYXSelectionExtrudeFilter", "Needs an active 3D selection of cells." },
  { "SHYXDeleteSelectedCellsFilter", "Needs an active cell selection." },
  { "SHYXFlipSelectedCellsWindingFilter", "Needs an active cell selection." },
  { "SHYXSelectionFillAlphaReunionFilter", "Selection -> fill / alpha wrap / union (CGAL>=5.5)." },
  { "SHYXPointCloudSurfaceSDF", "Point cloud to surface SDF (VTK). Not CGAL vtkCGALSignedDistanceFunction." },
  { "SHYXSurfaceToVolumeMesh", "CGAL Mesh_3 tets from closed surface (alternative to TetGen)." },
  { "SHYXAIAssistant",
    "Deprecated. The assistant is View → SHYX AI Assistant, not a pipeline filter. Do not create this node." },
};

QString lookupShyxDocs(const QString& queryRaw)
{
  const QString query = queryRaw.trimmed();
  QString out;
  if (query.isEmpty() || query.contains(QLatin1String("vascular"), Qt::CaseInsensitive) ||
    query.contains(QLatin1String("pipeline"), Qt::CaseInsensitive))
  {
    out += QStringLiteral(
      "Vascular menu order: SHYXSkeletonExtraction -> SHYXVesselEndClipper -> "
      "SHYXSelectionPlaneClipper -> SHYXRemeshWithEndpoint -> SHYXTetGen -> "
      "SHYXDataSetToPartitionedCollection -> SHYXPartitionedCollectionBoundaryAssignment\n");
  }
  if (query.isEmpty())
  {
    out += QStringLiteral("Pass a filter name or topic (remesh, clip, tet, PDC, selection, ...).\n");
  }

  for (const ShyxExtra& e : kShyxExtra)
  {
    const QString xml = QString::fromUtf8(e.xml);
    if (query.isEmpty() || xml.contains(query, Qt::CaseInsensitive) ||
      QString::fromUtf8(e.note).contains(query, Qt::CaseInsensitive))
    {
      out += QStringLiteral("- %1(): %2\n").arg(xml, QString::fromUtf8(e.note));
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
      int shown = 0;
      out += QStringLiteral("Live XML documentation:\n");
      for (it->GoToFirstItem(); !it->IsDoneWithTraversal() && shown < 8; it->GoToNextItem())
      {
        const char* xmlc = it->GetProxyName();
        if (!xmlc)
        {
          continue;
        }
        const QString xml = QString::fromUtf8(xmlc);
        vtkPVXMLElement* def = it->GetProxyDefinition();
        const char* labelc = def ? def->GetAttribute("label") : nullptr;
        const QString label = labelc ? QString::fromUtf8(labelc) : xml;
        const QString doc = xmlDocSummary(def, 280);
        const QString menu = menuFromHints(it->GetProxyHints());
        if (!queryHits(query, { xml, label, menu, doc }))
        {
          continue;
        }
        if (!xml.startsWith(QLatin1String("SHYX")) && !label.startsWith(QLatin1String("SHYX")) &&
          !label.startsWith(QLatin1String("VESPA")) && !xml.startsWith(QLatin1String("CGAL")))
        {
          continue;
        }
        out += QStringLiteral("- %1()  label=%2").arg(xml, label);
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
  out += QStringLiteral("For property names/enums/defaults call describe_proxy with the XML/python name.\n");
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
  if (!args.contains(QStringLiteral("x")) || !args.contains(QStringLiteral("y")))
  {
    return QStringLiteral("x and y are required.");
  }
  const double x = jsonDouble(args, "x");
  const double y = jsonDouble(args, "y");
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
}

QJsonArray pqSHYXAIAgentTools::schema()
{
  QJsonArray tools;
  tools.append(fn("get_pipeline_tree",
    "List all pipeline sources/filters with names, XML types, inputs, FileName, and each output "
    "port: data type, point/cell counts, visibility. Use before FindSource / GetActiveSource."));
  tools.append(fn("get_active_data",
    "Describe a pipeline source (default: active, port 0): type, point/cell counts, bounds, "
    "array names and per-component ranges, file path. Optional name is the pipeline name; "
    "optional port selects an output port (SHYX Mesh Checker / Remesher / End Clipper are multi-port).",
    QJsonObject{ { QStringLiteral("name"), strArg("Pipeline object name (FindSource name). Omit for active.") },
      { QStringLiteral("port"), intArg("Output port index. Default 0.") } }));
  tools.append(fn("get_source_data",
    "Same as get_active_data: describe any pipeline node and output port.",
    QJsonObject{ { QStringLiteral("name"), strArg("Pipeline object name. Omit for active source.") },
      { QStringLiteral("port"), intArg("Output port index. Default 0.") } }));
  tools.append(fn("get_selection",
    "Describe the current 3D/spreadsheet selection: source, port, counts, bounds."));
  tools.append(fn("get_selection_ids",
    "Selection details: type, bounds, IDs/CompositeIDs (truncated), field type. Use before "
    "SHYX Delete/Extrude/Flip/Fill/Plane Clipper scripts."));
  tools.append(fn("get_display",
    "Active representation: type, visibility, opacity, ColorArrayName, scalar visibility, "
    "PointSize, LineWidth, Interpolation, Diffuse/Ambient colors, edges. LUT range is get_color_map."));
  tools.append(fn("get_color_map",
    "Active representation lookup table: mapped array, LUT range, log scale, vector mode/component, "
    "color space, number of table values."));
  tools.append(fn("get_camera",
    "Active view camera: ViewSize, position, focal point, view up, view angle, parallel scale/projection."));
  tools.append(fn("get_time",
    "Animation clock time, range, and timestep list. Use before FTLE / streamlines / temporal filters."));
  tools.append(fn("get_output_window",
    "Recent ParaView Output Window errors and warnings. Call this when diagnosing a failed "
    "script, a missing array, or unexpected filter output. Do not call it on every turn."));
  tools.append(fn("capture_screenshot",
    "Capture the current RenderView as a JPEG. Only works if the user enabled "
    "'Access Auto Render Review'. The image is attached on the next turn. "
    "Prefer get_display / get_camera / get_active_data when screenshots are disabled. "
    "The result includes JPEG width/height for pick_world_point image_width/image_height."));
  tools.append(fn("pick_world_point",
    "Convert a 2D click to a 3D world point and hit source. Prefer one call. Multiple calls are "
    "allowed for distinct locations (several brush marks, or one miss followed by one corrected "
    "click). Do not grid-sample the screenshot or re-pick the same pixel with different "
    "origin/normalized/pixel conventions. "
    "Default origin is top_left (screenshots / brush marks). Pass image_width/image_height from "
    "capture_screenshot when picking on that JPEG. If x,y are both in [0,1] and no image size is "
    "given, they are treated as normalized coordinates.",
    QJsonObject{ { QStringLiteral("x"), numArg("X in image pixels, view pixels, or 0-1 normalized.") },
      { QStringLiteral("y"), numArg("Y in image pixels, view pixels, or 0-1 normalized.") },
      { QStringLiteral("image_width"),
        intArg("JPEG/screenshot width from capture_screenshot. Maps x into the view.") },
      { QStringLiteral("image_height"), intArg("JPEG/screenshot height from capture_screenshot.") },
      { QStringLiteral("origin"),
        strArg("top_left (default, screenshots) or vtk (bottom-left display coords).") },
      { QStringLiteral("snap_mesh"), boolArg("If true, snap to a mesh point. Default false.") } },
    QJsonArray{ QStringLiteral("x"), QStringLiteral("y") }));
  tools.append(fn("get_code_script",
    "Return the current contents of the code box (the ParaView Python script)."));
  QJsonObject setCodeProps;
  setCodeProps.insert(QStringLiteral("code"),
    QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") },
      { QStringLiteral("description"),
        QStringLiteral("Full ParaView Python script to store in the code box. No markdown fences.") } });
  tools.append(fn("set_code_script",
    "Replace the code box with a complete ParaView Python script. Always write the full script, "
    "not a patch. Call this before run_code_script when creating or fixing a script.",
    setCodeProps, QJsonArray{ QStringLiteral("code") }));
  QJsonObject runProps;
  runProps.insert(QStringLiteral("capture"),
    QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") },
      { QStringLiteral("description"),
        QStringLiteral("If true, attach a screenshot after the script runs. Ignored unless the user "
                       "enabled render-view screenshots. Default is false when screenshots are off.") } });
  tools.append(fn("run_code_script",
    "Execute the current code box (same as the Run script button). Returns new "
    "Output Window lines, active-source data, and the pipeline tree after the run. A screenshot is "
    "attached only if the user enabled render-view screenshots. "
    "If the run errors, data looks wrong, or the pipeline grew duplicate filters (two Clips on the "
    "same input), fix with set_code_script: FindSource the existing node and set properties, or "
    "Delete the extra node, then run again.",
    runProps));
  tools.append(fn("get_source_properties",
    "Dump property values of a pipeline source, including nested proxies (ClipType.Origin/Normal, "
    "SliceType, widgets). Optional name is the pipeline name; omit it to use the active source.",
    QJsonObject{ { QStringLiteral("name"), strArg("Pipeline object name (FindSource name).") } }));
  tools.append(fn("get_blocks",
    "PDC / multiblock / partitioned hierarchy and data-assembly names for ExtractBlock selectors. "
    "Optional name and port; omit name for the active source.",
    QJsonObject{ { QStringLiteral("name"), strArg("Pipeline object name. Omit for active source.") },
      { QStringLiteral("port"), intArg("Output port index. Default 0.") } }));
  tools.append(fn("list_filters",
    "Search registered ParaView proxies (filters/sources). Empty query lists SHYX/VESPA only. "
    "Returns python constructor name, label, menu category. Do not invent SHYX XML names.",
    QJsonObject{ { QStringLiteral("query"), strArg("Substring: remesh, clip, tet, SHYXMeshChecker, ...") } }));
  tools.append(fn("describe_proxy",
    "Property schema for a proxy: types, defaults, enums, proxy_list (Plane/Box for ClipType), "
    "output ports. Argument is XML/python name or UI label (SHYXMeshChecker, Clip, ...).",
    QJsonObject{ { QStringLiteral("name"), strArg("XML name, python constructor, or label.") } },
    QJsonArray{ QStringLiteral("name") }));
  tools.append(fn("lookup_shyx_docs",
    "SHYX/VESPA usage notes: vascular pipeline order, which filter to prefer, multi-port hints, "
    "and live XML short help. Then call describe_proxy for property names.",
    QJsonObject{ { QStringLiteral("query"), strArg("Filter name or topic: vascular, remesh, boolean, PDC, ...") } }));
  return tools;
}

QString pqSHYXAIAgentTools::run(const QString& name, const QJsonObject& args)
{
  if (name == QLatin1String("get_pipeline_tree"))
  {
    return pipelineTree();
  }
  if (name == QLatin1String("get_active_data") || name == QLatin1String("get_source_data"))
  {
    return sourceData(args.value(QStringLiteral("name")).toString(), jsonInt(args, "port", 0));
  }
  if (name == QLatin1String("get_selection"))
  {
    return selectionInfo();
  }
  if (name == QLatin1String("get_selection_ids"))
  {
    return selectionIds();
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
