#include "pqSHYXAIAgentTools.h"

#include "pqSHYXAIOutputLog.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineFilter.h"
#include "pqPipelineSource.h"
#include "pqSelectionManager.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkPVArrayInformation.h"
#include "vtkPVDataInformation.h"
#include "vtkPVDataSetAttributesInformation.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMPropertyIterator.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSmartPointer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace
{
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
    out += QStringLiteral("    %1 comps=%2").arg(QString::fromUtf8(ai->GetName())).arg(ai->GetNumberOfComponents());
    double r[2] = { 0, 0 };
    ai->GetComponentRange(0, r);
    out += QStringLiteral(" range0=[%1, %2]\n").arg(r[0]).arg(r[1]);
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
  out += QStringLiteral("%1 (%2)\n")
           .arg(QString::fromUtf8(label && label[0] ? label : src->GetXMLName()))
           .arg(QString::fromUtf8(src->GetXMLName()));
  vtkPVDataInformation* di = src->GetDataInformation(port);
  if (!di)
  {
    return out + QStringLiteral("  (no data info)\n");
  }
  const char* cls = di->GetDataClassName();
  out += QStringLiteral("  type=%1  points=%2  cells=%3\n")
           .arg(QString::fromUtf8(cls ? cls : "?"))
           .arg(di->GetNumberOfPoints())
           .arg(di->GetNumberOfCells());
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
  return out;
}

QString pipelineTree(vtkSMProxy* selfProxy)
{
  auto* sm = pqApplicationCore::instance() ? pqApplicationCore::instance()->getServerManagerModel()
                                           : nullptr;
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
    const bool self = selfProxy && src->getProxy() == selfProxy;
    out += QStringLiteral("- %1  xml=%2%3\n")
             .arg(src->getSMName())
             .arg(QString::fromUtf8(src->getProxy()->GetXMLName()))
             .arg(self ? QStringLiteral("  [this AI node]") : QString());
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
  }
  return out;
}

QString activeData()
{
  pqPipelineSource* active = pqActiveObjects::instance().activeSource();
  if (!active || !active->getSourceProxy())
  {
    return QStringLiteral("No active source.");
  }
  QString out = QStringLiteral("Active source: %1\n").arg(active->getSMName());
  out += describePort(active->getSourceProxy(), 0);
  const QString fn = fileNameOf(active->getProxy());
  if (!fn.isEmpty())
  {
    out += QStringLiteral("  file=%1\n").arg(fn);
  }
  return out;
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
  selIn->UpdatePipeline();
  vtkPVDataInformation* di = selIn->GetDataInformation(0);
  if (di)
  {
    out += QStringLiteral("Selection points=%1 cells=%2 type=%3\n")
             .arg(di->GetNumberOfPoints())
             .arg(di->GetNumberOfCells())
             .arg(QString::fromUtf8(di->GetDataClassName() ? di->GetDataClassName() : "?"));
  }
  return out;
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
  auto addStr = [&out, p](const char* name) {
    vtkSMProperty* prop = p->GetProperty(name);
    if (!prop)
    {
      return;
    }
    vtkSMPropertyHelper h(prop);
    const unsigned int n = h.GetNumberOfElements();
    QStringList vals;
    for (unsigned int i = 0; i < n && i < 6; ++i)
    {
      const char* s = h.GetAsString(i);
      vals << (s ? QString::fromUtf8(s) : QString());
    }
    if (!vals.isEmpty())
    {
      out += QStringLiteral("  %1=%2\n").arg(QString::fromUtf8(name), vals.join(QLatin1Char(',')));
    }
  };
  auto addNum = [&out, p](const char* name) {
    vtkSMProperty* prop = p->GetProperty(name);
    if (!prop)
    {
      return;
    }
    vtkSMPropertyHelper h(prop);
    if (h.GetNumberOfElements() > 0)
    {
      out += QStringLiteral("  %1=%2\n").arg(QString::fromUtf8(name)).arg(h.GetAsDouble());
    }
  };
  addStr("Representation");
  addNum("Visibility");
  addNum("Opacity");
  addStr("ColorArrayName");
  addStr("ColorArray");
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

QString sourceProperties(const QString& name)
{
  pqPipelineSource* src = pqActiveObjects::instance().activeSource();
  auto* sm = pqApplicationCore::instance() ? pqApplicationCore::instance()->getServerManagerModel()
                                           : nullptr;
  if (sm && !name.trimmed().isEmpty())
  {
    const QList<pqPipelineSource*> all = sm->findItems<pqPipelineSource*>();
    src = nullptr;
    for (pqPipelineSource* s : all)
    {
      if (s && s->getSMName() == name.trimmed())
      {
        src = s;
        break;
      }
    }
  }
  if (!src || !src->getProxy())
  {
    return QStringLiteral("Source not found.");
  }
  vtkSMProxy* proxy = src->getProxy();
  QString out = QStringLiteral("Properties of %1 (xml=%2):\n")
                  .arg(src->getSMName())
                  .arg(QString::fromUtf8(proxy->GetXMLName()));
  vtkSmartPointer<vtkSMPropertyIterator> it;
  it.TakeReference(proxy->NewPropertyIterator());
  int count = 0;
  for (it->Begin(); !it->IsAtEnd() && count < 40; it->Next())
  {
    vtkSMProperty* prop = it->GetProperty();
    const char* pname = it->GetKey();
    if (!prop || !pname || prop->GetInformationOnly() || prop->GetIsInternal())
    {
      continue;
    }
    if (prop->IsA("vtkSMInputProperty") || prop->IsA("vtkSMProxyProperty"))
    {
      continue;
    }
    vtkSMPropertyHelper h(prop);
    const unsigned int n = h.GetNumberOfElements();
    if (n == 0)
    {
      continue;
    }
    QStringList vals;
    for (unsigned int i = 0; i < n && i < 6; ++i)
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
    out += QStringLiteral("  %1=%2\n").arg(QString::fromUtf8(pname), vals.join(QLatin1Char(',')));
    ++count;
  }
  return out;
}
}

QJsonArray pqSHYXAIAgentTools::schema()
{
  QJsonArray tools;
  tools.append(fn("get_pipeline_tree",
    "List all pipeline sources and filters with names, XML types, inputs, and FileName if any. "
    "Use this before writing FindSource / GetActiveSource scripts."));
  tools.append(fn("get_active_data",
    "Describe the active source: type, point/cell counts, bounds, array names and ranges, file path."));
  tools.append(fn("get_selection",
    "Describe the current 3D/spreadsheet selection: source, port, and selected point/cell counts."));
  tools.append(fn("get_display",
    "Active representation: type (Surface/Wireframe/...), visibility, opacity, ColorArrayName."));
  tools.append(fn("get_camera",
    "Active view camera: position, focal point, view up, view angle, parallel scale/projection."));
  tools.append(fn("get_output_window",
    "Recent ParaView Output Window errors and warnings."));
  tools.append(fn("capture_screenshot",
    "Capture the current RenderView as a JPEG. The image is attached on the next turn. "
    "Use after run_code_script to inspect the visual result, or anytime the view matters."));
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
        QStringLiteral("If true (default), attach a screenshot of the active view after the script runs.") } });
  tools.append(fn("run_code_script",
    "Execute the current code box in this ParaView (same as Apply / Run script). Returns new "
    "Output Window lines, active-source data after the run, and optionally a screenshot. "
    "If the run errors or the view looks wrong, fix with set_code_script and run again.",
    runProps));
  QJsonObject named = fn("get_source_properties",
    "Dump key property values of a pipeline source. Optional argument name is the pipeline name; "
    "omit it to use the active source.",
    QJsonObject{ { QStringLiteral("name"),
      QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") },
        { QStringLiteral("description"), QStringLiteral("Pipeline object name (FindSource name).") } } } });
  tools.append(named);
  return tools;
}

QString pqSHYXAIAgentTools::run(const QString& name, const QJsonObject& args, vtkSMProxy* selfProxy)
{
  if (name == QLatin1String("get_pipeline_tree"))
  {
    return pipelineTree(selfProxy);
  }
  if (name == QLatin1String("get_active_data"))
  {
    return activeData();
  }
  if (name == QLatin1String("get_selection"))
  {
    return selectionInfo();
  }
  if (name == QLatin1String("get_display"))
  {
    return displayInfo();
  }
  if (name == QLatin1String("get_camera"))
  {
    return cameraInfo();
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
  if (name == QLatin1String("capture_screenshot"))
  {
    return QStringLiteral("__CAPTURE_SCREENSHOT__");
  }
  return QStringLiteral("Unknown tool: %1").arg(name);
}
