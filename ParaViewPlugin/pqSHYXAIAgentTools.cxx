#include "pqSHYXAIAgentTools.h"

#include "pqSHYXAIOutputLog.h"
#include "pqSHYXGrowSelectionWithSimilarController.h"

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
        !k.startsWith(QLatin1String("PL_")) && !k.startsWith(QLatin1String("PulseGlyph_")))
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
  out += QStringLiteral("  (LUT details: call get_color_map; schema: describe_proxy on the display type)\n");
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
    s.contains(QLatin1String("Point Label"), Qt::CaseInsensitive);
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
    xml == QLatin1String("PointLabelRepresentation");
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

int hitScore(const QString& query, const QString& xml, const QString& label)
{
  const QString display = representationDisplayName(xml);
  if (query.isEmpty())
  {
    int s = xml.startsWith(QLatin1String("SHYX")) ? 20 : 5;
    if (!display.isEmpty())
    {
      s += 20;
    }
    return s;
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
  if (!display.isEmpty())
  {
    if (display.compare(query, Qt::CaseInsensitive) == 0)
    {
      s += 80;
    }
    else if (display.contains(query, Qt::CaseInsensitive) ||
      query.contains(display, Qt::CaseInsensitive))
    {
      s += 70;
    }
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
    const QString doc = xmlDocSummary(def, 90);
    const bool shyxish = isShyxishProxy(xml, label);
    if (groupStr == QLatin1String("representations") && !shyxish)
    {
      continue;
    }
    if (query.isEmpty() && !shyxish)
    {
      continue;
    }
    if (!queryHits(query, { xml, label, menu, doc, groupStr, display }))
    {
      continue;
    }
    const int score = hitScore(query, xml, label);
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
                               "then describe_proxy for PG_/AS_/PL_ names")
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
      "Pass a query to search all ParaView filters/sources (SHYX representations always).\n");
  }
  else
  {
    out += QStringLiteral("Filters/sources/representations matching %1:\n").arg(query);
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
      "Representations: (none loaded; expect Pulse Glyphs / Animated Streamline / Point Label)\n");
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
      "Grow selection with similar normals. Block context menu: Select Block. "
      "Selection context menu: Select All (connected region); "
      "Invert Selection; "
      "Select Similar → By Normal (grow to completion); "
      "Fill Interior (enclosed unselected faces). "
      "See lookup_shyx_docs. After the user uses them, call get_selection_ids.\n");
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
  const bool selectAll = ql.contains(QLatin1String("select all")) ||
    ql.contains(QLatin1String("selectall")) ||
    ql.contains(QLatin1String("shyxselectall")) ||
    ql.contains(QLatin1String("select connected")) ||
    ql.contains(QLatin1String("connected region"));
  const bool invertSel = ql.contains(QLatin1String("invert")) ||
    ql.contains(QLatin1String("shyxinvert"));
  if (sphere && !grow)
  {
    return QStringLiteral(
      "SHYX Sphere cell selection (RenderView title-bar; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call get_selection_ids.\n"
      "Interaction:\n"
      "  Toggle: title-bar sphere button\n"
      "  On enable: snap center to nearest vertex at the view center\n"
      "  Initial radius: ~15% of the viewport short edge in world units\n"
      "  Left-drag: move center (grab point stays under the cursor)\n"
      "  Hover + mouse wheel: scale radius (min 1e-12)\n"
      "  Selects cells whose vertices lie inside the sphere\n"
      "  Selection modifiers: ParaView add/subtract/toggle (Shift/Ctrl as usual)\n"
      "  Right-click the button: 'Apply selection on release only' (DeferSelectionUntilRelease)\n");
  }
  if (grow && !sphere && !selectBlock)
  {
    const double deg = pqSHYXGrowSelectionWithSimilarController::DihedralThresholdDegrees();
    return QStringLiteral(
      "SHYX Grow / Select Similar by normal (client Qt; not a Server Manager proxy)\n"
      "No Python constructor. After the user uses it, call get_selection_ids.\n"
      "Parameters:\n"
      "  DihedralThresholdDegrees  type=double  current=%1  range=[0, 180]  default=15\n"
      "    Angle between face normals. Shared by the title-bar button and the context menu.\n"
      "    Right-click the title-bar Grow button to edit. Not a paraview.simple property.\n"
      "Title-bar Grow selection with similar normals:\n"
      "  Requires an existing cell selection\n"
      "  Grows by one ring of edge-adjacent faces with normal-normal angle <= threshold\n"
      "  Click once: one ring; press-and-hold: keep growing until no more similar neighbors\n"
      "  Reports a warning to the Output Window if the selection does not grow\n"
      "RenderView context menu (when a cell selection is active):\n"
      "  Select Similar → By Normal: grow ALL similar rings in one action (not one click per ring)\n")
      .arg(deg, 0, 'g', 4);
  }
  if (fillInterior && !sphere && !grow && !selectBlock && !selectAll && !invertSel)
  {
    return QStringLiteral(
      "SHYX Fill Interior (RenderView selection context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call get_selection_ids.\n"
      "Interaction:\n"
      "  Right-click in the 3D view when a cell selection is active → Fill Interior\n"
      "  Adds unselected faces that form holes completely enclosed by the current selection\n"
      "  Requires vtkPolyData. Needs a closed loop of selected faces around the interior\n"
      "  On an open surface, unselected regions that still reach a mesh opening are left unselected\n"
      "  On a closed surface, the largest enclosed complement is treated as the exterior "
      "unless it is no larger than the current selection\n");
  }
  if (selectAll && !sphere && !grow && !selectBlock && !invertSel)
  {
    return QStringLiteral(
      "SHYX Select All (RenderView selection context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call get_selection_ids.\n"
      "Interaction:\n"
      "  Right-click in the 3D view when a cell selection is active → Select All\n"
      "  Selects every face in the edge-connected region(s) that contain the current selection\n"
      "  Requires vtkPolyData. No dihedral threshold (unlike Select Similar / By Normal)\n"
      "  Disconnected shells that do not touch the selection are left unselected\n"
      "  If the mesh is a single connected component, this selects the whole surface\n");
  }
  if (invertSel && !sphere && !grow && !selectBlock)
  {
    return QStringLiteral(
      "SHYX Invert Selection (RenderView selection context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call get_selection_ids.\n"
      "Interaction:\n"
      "  Right-click in the 3D view when a cell selection is active → Invert Selection\n"
      "  Selects currently unselected cells and deselects the current selection\n"
      "  Operates on the active dataset (not only vtkPolyData). Complement of all cells\n"
      "  If every cell was selected, the result is an empty selection\n");
  }
  if (selectBlock && !sphere && !grow)
  {
    return QStringLiteral(
      "SHYX Select Block (RenderView block context menu; not a Server Manager proxy)\n"
      "No Python constructor and no SM properties. After the user uses it, call get_selection_ids.\n"
      "Interaction:\n"
      "  Right-click a composite block in the 3D view (menu titled Block 'Part_1')\n"
      "  Choose Select Block to clear the current selection and select every cell "
      "in that block (BLOCK_SELECTORS, CELL)\n"
      "  Works with or without an existing cell selection; then use Extract Selection / "
      "SHYX selection filters\n");
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
                          "describe_proxy('Pulse Glyphs').")
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
  { "SHYXSelectionAppendPatches",
    "Add from selection snapshots the 3D-view cell selection into the Patches table (geo_N); "
    "Copy Active Selection is not required. Rename only (any name); the table keeps every row. "
    "Apply merges same names into one patch and reuses the earlier mark. Unique names are marked "
    "0, 1, 2, ... in table order. Port 0 = added patches (PDC); port 1 = Input minus the union of "
    "added cells. Apply on Add (default checkbox) Applies after each Add or Remove so both "
    "ports refresh; after Remove, dropped cells reappear on port 1 and can be added again; "
    "select remaining cells on port 1 to avoid re-picking added cells. Uncheck it to skip Apply "
    "and allow overlapping picks from a stale remainder or the original Input. Apply "
    "ExtractSelection-appends each unique name as a PDC block. Unselected cells are not kept on "
    "port 0. Not the IOSS DataSetToPartitionedCollection path." },
  { "SHYXDeleteSelectedCellsFilter",
    "Needs an active cell selection. Creating the filter copies the Input's "
    "active selection into the Selection widget (Extract Selection-style); "
    "Copy Active Selection is only needed if the selection changes afterwards." },
  { "SHYXFlipSelectedCellsWindingFilter", "Needs an active cell selection." },
  { "SHYXSelectionFillAlphaReunionFilter", "Selection -> fill / alpha wrap / union (CGAL>=5.5)." },
  { "SHYXPointCloudSurfaceSDF", "Point cloud to surface SDF (VTK). Not CGAL vtkCGALSignedDistanceFunction." },
  { "SHYXSurfaceToVolumeMesh", "CGAL Mesh_3 tets from closed surface (alternative to TetGen)." },
  { "SHYXSnappyHexMesh",
    "Hex-dominant volume mesh. Input is vtkPartitionedDataSetCollection (each partition = one STL "
    "triSurfaceMesh / patch, no firstSolid/secondSolid) or a single vtkPolyData (wrapped as "
    "geometry). Optional FeatureEdges is a Properties-panel pipeline dropdown (not a second "
    "required input); polydata lines become features.eMesh. "
    "Add partitions in Surface patches / Region patches (inside Castellated) and Layer patches "
    "(level, patchInfo type, region mode). Empty surfaces table = all partitions at Default surface level. "
    "Case is a unique %TEMP%/shyx-snappy-*/case folder each Apply (STLs in constant/triSurface). "
    "Output is vtkOpenFOAMReader's vtkMultiBlockDataSet (internalMesh plus patches), not a "
    "standalone unstructured grid. Requires VESPA_USE_SNAPPYHEXMESH." },
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
  { "SHYXSphereSelection",
    "Not a filter and not a proxy. RenderView title-bar sphere button. "
    "Call describe_proxy('sphere') for interaction parameters. After the user uses it, call get_selection_ids." },
  { "SHYXGrowSelectionWithSimilar",
    "Not a filter and not a proxy. RenderView title-bar button: one ring per click, hold to keep growing. "
    "Call describe_proxy('grow') for DihedralThresholdDegrees (default 15). After use, call get_selection_ids." },
  { "SHYXSelectSimilar",
    "Not a filter and not a proxy. RenderView right-click when a cell selection is active: "
    "Select Similar → By Normal grows all similar-normal rings in one shot (same dihedral threshold as Grow). "
    "Call describe_proxy('select similar'). After use, call get_selection_ids." },
  { "SHYXFillInterior",
    "Not a filter and not a proxy. RenderView right-click when a cell selection is active: "
    "Fill Interior adds unselected faces enclosed by the current selection (holes inside a closed loop). "
    "Call describe_proxy('fill interior'). After use, call get_selection_ids." },
  { "SHYXSelectAll",
    "Not a filter and not a proxy. RenderView right-click when a cell selection is active: "
    "Select All selects every face in the connected region(s) that contain the current selection. "
    "Call describe_proxy('select all'). After use, call get_selection_ids." },
  { "SHYXInvertSelection",
    "Not a filter and not a proxy. RenderView right-click when a cell selection is active: "
    "Invert Selection selects currently unselected cells and deselects the current selection. "
    "Call describe_proxy('invert'). After use, call get_selection_ids." },
  { "SHYXSelectBlock",
    "Not a filter and not a proxy. RenderView right-click on a composite block (Block 'Part_1' menu). "
    "Select Block clears the current selection then selects all cells in that block (BLOCK_SELECTORS). "
    "Call describe_proxy('select block'). "
    "After use, call get_selection_ids." },
  { "SHYXAIAssistant",
    "Deprecated. The assistant is View → SHYX AI Assistant, not a pipeline filter. Do not create this node." },
};

QString lookupShyxDocs(const QString& queryRaw)
{
  const QString query = isCatalogQuery(queryRaw) ? QString() : queryRaw.trimmed();
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
    out += QStringLiteral(
      "SHYX is not only Filters → SHYX. Capability groups:\n"
      "1) Pipeline filters/sources: Filters → SHYX and Vascular toolbar. "
      "Live XML names: list_filters with empty query.\n"
      "2) Display representations (Display panel Representation dropdown; NOT pipeline nodes):\n"
      "   Pulse Glyphs, Animated Streamline, Point Label. "
      "Python: GetDisplayProperties().Representation = 'Pulse Glyphs'.\n"
      "3) RenderView title-bar selection tools (client Qt; no SM proxy / no Python constructor):\n"
      "   Sphere cell selection; Grow selection with similar normals. "
      "After the user uses them, call get_selection_ids.\n"
      "   Also: right-click a composite block → Select Block "
      "(clear current selection, then select all cells in that part).\n"
      "   Also: right-click an active cell selection → Select All "
      "(whole connected region); Invert Selection; Select Similar → By Normal "
      "(grow all similar-normal rings in one shot); Fill Interior "
      "(add unselected faces enclosed by the current selection).\n"
      "4) Widget representations (stent placement 3D widgets, not Display dropdown): "
      "SHYXImplicitCylinderWidgetRepresentation, SHYXEndpointStentWidgetRepresentation.\n"
      "5) View → SHYX AI Assistant: this dock, not a pipeline filter.\n"
      "Pass a name/topic (remesh, clip, representation, selection, sphere, glyph, ...) to filter notes.\n"
      "For parameter names/defaults: describe_proxy('Pulse Glyphs'), describe_proxy('Animated Streamline'), "
      "describe_proxy('Point Label'), describe_proxy('sphere'), describe_proxy('grow'), "
      "describe_proxy('select block'), describe_proxy('select similar'), "
      "describe_proxy('fill interior'), describe_proxy('select all'), "
      "describe_proxy('invert').\n");
  }

  for (const ShyxExtra& e : kShyxExtra)
  {
    const QString xml = QString::fromUtf8(e.xml);
    const QString display = representationDisplayName(xml);
    if (query.isEmpty() || xml.contains(query, Qt::CaseInsensitive) ||
      QString::fromUtf8(e.note).contains(query, Qt::CaseInsensitive) ||
      query.contains(xml, Qt::CaseInsensitive) ||
      (!display.isEmpty() && query.contains(display, Qt::CaseInsensitive)))
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
        if (!queryHits(query, { xml, label, menu, doc, display, QString::fromUtf8(group ? group : "") }))
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
                        "or display type (Pulse Glyphs, sphere, grow, select block, select similar, fill interior, select all, invert).\n");
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
    "Convert 2D screenshot/view clicks to 3D world points. Put EVERY brush mark / click in ONE "
    "call using points=[{x,y}, {x,y}, ...]. Do not call this once per mark. "
    "Default origin is top_left (screenshots). Pass image_width/image_height from the JPEG. "
    "If x,y are both in [0,1] and no image size is given, they are treated as normalized. "
    "Do not grid-sample or re-pick the same pixel with different origin/normalized/pixel conventions.",
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
  runProps.insert(QStringLiteral("capture"),
    QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") },
      { QStringLiteral("description"),
        QStringLiteral("If true, attach a screenshot after the script runs. Ignored unless the user "
                       "enabled render-view screenshots. Default is false when screenshots are off.") } });
  tools.append(fn("run_code_script",
    "Execute the current code box (same as the Run script button). Always call this after "
    "set_code_script so you can see whether the script actually works. Returns new "
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
    "Search registered ParaView proxies. Empty query (or features/catalog) lists SHYX/VESPA "
    "filters/sources AND Display representations (Pulse Glyphs, Animated Streamline, Point Label) "
    "plus a reminder of title-bar selection tools. Non-empty query searches all ParaView "
    "filters/sources and SHYX representations. Representations are not python constructors.",
    QJsonObject{ { QStringLiteral("query"), strArg("Substring: remesh, glyph, Point Label, SHYXMeshChecker, ...") } }));
  tools.append(fn("describe_proxy",
    "Property schema: types, defaults, enums, docs. For filters: XML/python name. "
    "For SHYX Display types pass 'Pulse Glyphs', 'Animated Streamline', or 'Point Label' "
    "(returns exposed Python names PG_*/AS_*/PL_*). "
    "For title-bar tools pass 'sphere' or 'grow' (no SM proxy). "
    "For the block context-menu action pass 'select block'. "
    "For Select Similar / By Normal pass 'select similar'. "
    "For Fill Interior pass 'fill interior'. "
    "For Select All (connected region) pass 'select all'. "
    "For Invert Selection pass 'invert'.",
    QJsonObject{ { QStringLiteral("name"), strArg("XML name, display type (Pulse Glyphs), sphere/grow, select block, select similar, fill interior, select all, or invert.") } },
    QJsonArray{ QStringLiteral("name") }));
  tools.append(fn("lookup_shyx_docs",
    "SHYX/VESPA usage notes: capability catalog (filters + Display representations + "
    "title-bar selection tools), vascular pipeline order, which filter to prefer, multi-port hints, "
    "and live XML short help. Empty query is the full catalog. Then call describe_proxy for property names.",
    QJsonObject{ { QStringLiteral("query"), strArg("Empty for catalog, or topic: vascular, representation, selection, remesh, ...") } }));
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
