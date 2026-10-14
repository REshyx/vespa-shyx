#include "pqSHYXBoundaryAssignmentInfoWidget.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkCommand.h"
#include "vtkDataAssembly.h"
#include "vtkErrorCode.h"
#include "vtkIOSSWriter.h"
#include "vtkNew.h"
#include "vtkPartitionedDataSetCollection.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#include <limits>
#include <memory>
#include <new>
#include <string>

#include "vtk_zlib.h"

namespace
{

vtkSMProperty* propertyFromGroup(
  vtkSMPropertyGroup* group, vtkSMProxy* proxy, const char* function, const char* fallbackName)
{
  if (group)
  {
    if (auto* p = group->GetProperty(function))
    {
      return p;
    }
    const unsigned int n = group->GetNumberOfProperties();
    for (unsigned int i = 0; i < n; ++i)
    {
      const char* name = group->GetPropertyName(i);
      if (name && fallbackName && std::string(name) == fallbackName)
      {
        return group->GetProperty(i);
      }
    }
  }
  return proxy ? proxy->GetProperty(fallbackName) : nullptr;
}

QTextEdit* makeReadonlyMultiline(QWidget* parent, int minRows)
{
  auto* edit = new QTextEdit(parent);
  edit->setReadOnly(true);
  edit->setAcceptRichText(false);
  edit->setLineWrapMode(QTextEdit::NoWrap);
  edit->setUndoRedoEnabled(false);
  QFont mono = edit->font();
  mono.setStyleHint(QFont::Monospace);
  mono.setFamily(QStringLiteral("Consolas"));
  edit->setFont(mono);
  const int line = edit->fontMetrics().lineSpacing();
  edit->setMinimumHeight(line * minRows + 12);
  return edit;
}

bool writeTextFile(const QString& path, const QString& text, QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
  {
    if (error)
    {
      *error = QObject::tr("Cannot write \"%1\": %2").arg(path, file.errorString());
    }
    return false;
  }
  QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  stream.setEncoding(QStringConverter::Utf8);
#else
  stream.setCodec("UTF-8");
#endif
  stream << text;
  if (stream.status() != QTextStream::Ok)
  {
    if (error)
    {
      *error = QObject::tr("Failed while writing \"%1\".").arg(path);
    }
    return false;
  }
  return true;
}

bool writeExodusIossDefault(vtkPartitionedDataSetCollection* input, const QString& filePath,
  QString* error)
{
  if (!input)
  {
    if (error)
    {
      *error = QObject::tr("Port 0 has no vtkPartitionedDataSetCollection.");
    }
    return false;
  }

  vtkNew<vtkIOSSWriter> writer;
  writer->SetInputData(input);
  writer->SetFileName(filePath.toUtf8().constData());

  vtkDataAssembly* asmTree = input->GetDataAssembly();
  const char* assemblyName =
    (asmTree && asmTree->GetRootNodeName() && asmTree->GetRootNodeName()[0] != '\0')
    ? asmTree->GetRootNodeName()
    : "IOSS";
  writer->SetAssemblyName(assemblyName);
  const std::string asmPrefix = std::string("/") + assemblyName;
  writer->AddElementBlockSelector((asmPrefix + "/element_blocks").c_str());
  writer->AddNodeSetSelector((asmPrefix + "/node_sets").c_str());
  writer->AddSideSetSelector((asmPrefix + "/side_sets").c_str());

  writer->Write();
  if (writer->GetErrorCode() != vtkErrorCode::NoError)
  {
    if (error)
    {
      *error = QObject::tr("vtkIOSSWriter failed for \"%1\" (error code %2).")
                 .arg(filePath)
                 .arg(static_cast<int>(writer->GetErrorCode()));
    }
    return false;
  }
  return true;
}

QString ensureExtension(QString name, const QString& ext)
{
  name = name.trimmed();
  if (name.isEmpty())
  {
    return name;
  }
  if (!name.endsWith(ext, Qt::CaseInsensitive))
  {
    name += ext;
  }
  return name;
}

struct ZipPayload
{
  QString name;
  QByteArray bytes;
};

bool writeExact(QFile& file, const char* data, qint64 size, const QString& path, QString* error)
{
  if (size == 0)
  {
    return true;
  }
  if (size < 0 || data == nullptr || file.write(data, size) != size)
  {
    if (error)
    {
      *error = QObject::tr("Failed while writing \"%1\": %2").arg(path, file.errorString());
    }
    return false;
  }
  return true;
}

bool writeU16(QFile& file, quint16 value, const QString& path, QString* error)
{
  const char bytes[2] = { static_cast<char>(value & 0xff),
    static_cast<char>((value >> 8) & 0xff) };
  return writeExact(file, bytes, 2, path, error);
}

bool writeU32(QFile& file, quint32 value, const QString& path, QString* error)
{
  const char bytes[4] = { static_cast<char>(value & 0xff),
    static_cast<char>((value >> 8) & 0xff), static_cast<char>((value >> 16) & 0xff),
    static_cast<char>((value >> 24) & 0xff) };
  return writeExact(file, bytes, 4, path, error);
}

void dosDateTime(quint16& dosTime, quint16& dosDate)
{
  const QDateTime now = QDateTime::currentDateTime();
  const QTime time = now.time();
  QDate date = now.date();
  int year = date.year();
  if (year < 1980)
  {
    year = 1980;
  }
  else if (year > 2107)
  {
    year = 2107;
  }
  dosTime = static_cast<quint16>((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
  dosDate = static_cast<quint16>(((year - 1980) << 9) | (date.month() << 5) | date.day());
}

uLong crc32Of(const QByteArray& data)
{
  uLong crc = crc32(0L, Z_NULL, 0);
  const char* cursor = data.constData();
  qint64 remaining = data.size();
  while (remaining > 0)
  {
    const int chunk = remaining > (1 << 20) ? (1 << 20) : static_cast<int>(remaining);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(cursor), static_cast<uInt>(chunk));
    cursor += chunk;
    remaining -= chunk;
  }
  return crc;
}

bool deflateRaw(const QByteArray& input, QByteArray* output, QString* error)
{
  z_stream strm{};
  const int init = deflateInit2(
    &strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
  if (init != Z_OK)
  {
    if (error)
    {
      *error = QObject::tr("zlib deflateInit2 failed (%1).").arg(init);
    }
    return false;
  }

  const uLong bound = deflateBound(&strm, static_cast<uLong>(input.size()));
  using SizeType = decltype(QByteArray().size());
  if (bound > static_cast<uLong>(std::numeric_limits<uInt>::max()) ||
    bound > static_cast<uLong>(std::numeric_limits<SizeType>::max()))
  {
    deflateEnd(&strm);
    if (error)
    {
      *error = QObject::tr("Compressed size does not fit in a zip entry.");
    }
    return false;
  }

  try
  {
    output->resize(static_cast<SizeType>(bound));
  }
  catch (const std::bad_alloc&)
  {
    deflateEnd(&strm);
    if (error)
    {
      *error = QObject::tr("Not enough memory to compress the package.");
    }
    return false;
  }
  strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.constData()));
  strm.avail_in = static_cast<uInt>(input.size());
  strm.next_out = reinterpret_cast<Bytef*>(output->data());
  strm.avail_out = static_cast<uInt>(output->size());
  const int ret = deflate(&strm, Z_FINISH);
  const uLong produced = strm.total_out;
  deflateEnd(&strm);
  if (ret != Z_STREAM_END)
  {
    if (error)
    {
      *error = QObject::tr("zlib deflate failed (%1).").arg(ret);
    }
    return false;
  }
  output->resize(static_cast<SizeType>(produced));
  return true;
}

bool readFileBytes(const QString& path, QByteArray* bytes, QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
  {
    if (error)
    {
      *error = QObject::tr("Cannot read \"%1\": %2").arg(path, file.errorString());
    }
    return false;
  }
  try
  {
    *bytes = file.readAll();
  }
  catch (const std::bad_alloc&)
  {
    if (error)
    {
      *error = QObject::tr("Not enough memory to pack \"%1\".").arg(path);
    }
    return false;
  }
  if (file.error() != QFile::NoError)
  {
    if (error)
    {
      *error = QObject::tr("Cannot read \"%1\": %2").arg(path, file.errorString());
    }
    return false;
  }
  return true;
}

// Classic zip (no zip64): UTF-8 names, deflate or store. Four small solver files.
bool writeZipFile(const QString& path, const QList<ZipPayload>& entries, QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    if (error)
    {
      *error = QObject::tr("Cannot write \"%1\": %2").arg(path, file.errorString());
    }
    return false;
  }

  quint16 dosTime = 0;
  quint16 dosDate = 0;
  dosDateTime(dosTime, dosDate);

  struct Written
  {
    QByteArray name;
    quint32 crc = 0;
    quint32 compSize = 0;
    quint32 rawSize = 0;
    quint16 method = 0;
    quint32 localOffset = 0;
  };
  QList<Written> written;
  written.reserve(entries.size());

  for (const ZipPayload& entry : entries)
  {
    const QByteArray name = entry.name.toUtf8();
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.size() > 65535)
    {
      if (error)
      {
        *error = QObject::tr("Invalid zip entry name \"%1\".").arg(entry.name);
      }
      return false;
    }
    if (static_cast<quint64>(entry.bytes.size()) > 0xffffffffu)
    {
      if (error)
      {
        *error = QObject::tr("\"%1\" is too large for a zip entry.").arg(entry.name);
      }
      return false;
    }

    QByteArray compressed;
    quint16 method = 0;
    if (!entry.bytes.isEmpty())
    {
      if (!deflateRaw(entry.bytes, &compressed, error))
      {
        return false;
      }
      method = 8;
      if (compressed.size() >= entry.bytes.size())
      {
        method = 0;
        compressed = entry.bytes;
      }
    }

    const qint64 localOffset = file.pos();
    if (localOffset < 0 || static_cast<quint64>(localOffset) > 0xffffffffu)
    {
      if (error)
      {
        *error = QObject::tr("Zip archive exceeds the 4 GB limit.");
      }
      return false;
    }

    Written item;
    item.name = name;
    item.crc = static_cast<quint32>(crc32Of(entry.bytes));
    item.compSize = static_cast<quint32>(compressed.size());
    item.rawSize = static_cast<quint32>(entry.bytes.size());
    item.method = method;
    item.localOffset = static_cast<quint32>(localOffset);
    written.append(item);

    // Bit 11: file name is UTF-8.
    const quint16 flags = 0x0800;
    if (!writeU32(file, 0x04034b50u, path, error) || !writeU16(file, 20, path, error) ||
      !writeU16(file, flags, path, error) || !writeU16(file, method, path, error) ||
      !writeU16(file, dosTime, path, error) || !writeU16(file, dosDate, path, error) ||
      !writeU32(file, item.crc, path, error) || !writeU32(file, item.compSize, path, error) ||
      !writeU32(file, item.rawSize, path, error) ||
      !writeU16(file, static_cast<quint16>(name.size()), path, error) ||
      !writeU16(file, 0, path, error) || !writeExact(file, name.constData(), name.size(), path, error) ||
      !writeExact(file, compressed.constData(), compressed.size(), path, error))
    {
      return false;
    }
  }

  const qint64 centralOffset = file.pos();
  if (centralOffset < 0 || static_cast<quint64>(centralOffset) > 0xffffffffu)
  {
    if (error)
    {
      *error = QObject::tr("Zip archive exceeds the 4 GB limit.");
    }
    return false;
  }

  for (const Written& item : written)
  {
    const quint16 flags = 0x0800;
    if (!writeU32(file, 0x02014b50u, path, error) || !writeU16(file, 20, path, error) ||
      !writeU16(file, 20, path, error) || !writeU16(file, flags, path, error) ||
      !writeU16(file, item.method, path, error) || !writeU16(file, dosTime, path, error) ||
      !writeU16(file, dosDate, path, error) || !writeU32(file, item.crc, path, error) ||
      !writeU32(file, item.compSize, path, error) || !writeU32(file, item.rawSize, path, error) ||
      !writeU16(file, static_cast<quint16>(item.name.size()), path, error) ||
      !writeU16(file, 0, path, error) || !writeU16(file, 0, path, error) ||
      !writeU16(file, 0, path, error) || !writeU16(file, 0, path, error) ||
      !writeU32(file, 0, path, error) || !writeU32(file, item.localOffset, path, error) ||
      !writeExact(file, item.name.constData(), item.name.size(), path, error))
    {
      return false;
    }
  }

  const qint64 centralEnd = file.pos();
  if (centralEnd < centralOffset ||
    static_cast<quint64>(centralEnd - centralOffset) > 0xffffffffu || written.size() > 65535)
  {
    if (error)
    {
      *error = QObject::tr("Zip central directory is too large.");
    }
    return false;
  }

  const quint16 count = static_cast<quint16>(written.size());
  const quint32 centralSize = static_cast<quint32>(centralEnd - centralOffset);
  if (!writeU32(file, 0x06054b50u, path, error) || !writeU16(file, 0, path, error) ||
    !writeU16(file, 0, path, error) || !writeU16(file, count, path, error) ||
    !writeU16(file, count, path, error) || !writeU32(file, centralSize, path, error) ||
    !writeU32(file, static_cast<quint32>(centralOffset), path, error) ||
    !writeU16(file, 0, path, error))
  {
    return false;
  }
  return true;
}

} // namespace

//-----------------------------------------------------------------------------
QString pqSHYXBoundaryAssignmentInfoWidget::defaultExoName(
  const QString& tag, const QString& caseId)
{
  return QStringLiteral("%1_%2.exo").arg(tag, caseId);
}

QString pqSHYXBoundaryAssignmentInfoWidget::defaultOptName(
  const QString& tag, const QString& caseId)
{
  // No .opt suffix by design (solver expects extensionless options files).
  return QStringLiteral("options_%1_%2").arg(tag, caseId);
}

QString pqSHYXBoundaryAssignmentInfoWidget::defaultNodesetName(
  const QString& tag, const QString& caseId)
{
  // No .bc suffix by design.
  return QStringLiteral("Nodeset_%1_%2").arg(tag, caseId);
}

QString pqSHYXBoundaryAssignmentInfoWidget::defaultPvsmName(
  const QString& tag, const QString& caseId)
{
  return QStringLiteral("%1_%2.pvsm").arg(tag, caseId);
}

void pqSHYXBoundaryAssignmentInfoWidget::splitExportKey(
  const QString& key, QString& tag, QString& caseId)
{
  const int bar = key.indexOf(QLatin1Char('|'));
  if (bar < 0)
  {
    tag = QStringLiteral("PV");
    caseId = key.isEmpty() ? QStringLiteral("0") : key;
    return;
  }
  tag = key.left(bar);
  caseId = key.mid(bar + 1);
  if (tag.isEmpty())
  {
    tag = QStringLiteral("PV");
  }
  if (caseId.isEmpty())
  {
    caseId = QStringLiteral("0");
  }
}

QString pqSHYXBoundaryAssignmentInfoWidget::currentModeTag() const
{
  vtkSMProxy* filter = this->proxy();
  if (!filter || !filter->GetProperty("FlowBoundaryMode"))
  {
    return QStringLiteral("PV");
  }
  // 0 = Single inlet → PV; 1 = Single outlet → HV
  const int mode = vtkSMPropertyHelper(filter, "FlowBoundaryMode").GetAsInt();
  return mode == 0 ? QStringLiteral("PV") : QStringLiteral("HV");
}

vtkSMProxy* pqSHYXBoundaryAssignmentInfoWidget::topLevelProducer(vtkSMProxy* proxy)
{
  vtkSMProxy* cur = proxy;
  // Bound the walk in case of unusual cycles / multi-input graphs.
  for (int guard = 0; cur && guard < 64; ++guard)
  {
    vtkSMProperty* inputProp = cur->GetProperty("Input");
    if (!inputProp)
    {
      break;
    }
    vtkSMProxy* upstream = vtkSMPropertyHelper(inputProp).GetAsProxy(0);
    if (!upstream)
    {
      break;
    }
    cur = upstream;
  }
  return cur;
}

QString pqSHYXBoundaryAssignmentInfoWidget::filePathFromProxy(vtkSMProxy* proxy)
{
  if (!proxy)
  {
    return QString();
  }

  // Single-file readers (most common for Open File).
  if (vtkSMProperty* fn = proxy->GetProperty("FileName"))
  {
    const char* path = vtkSMPropertyHelper(fn).GetAsString();
    if (path && path[0] != '\0')
    {
      return QString::fromUtf8(path);
    }
  }

  // Multi-file readers: use the first listed path.
  if (vtkSMProperty* fns = proxy->GetProperty("FileNames"))
  {
    vtkSMPropertyHelper helper(fns);
    if (helper.GetNumberOfElements() > 0)
    {
      const char* path = helper.GetAsString(0);
      if (path && path[0] != '\0')
      {
        return QString::fromUtf8(path);
      }
    }
  }

  return QString();
}

QString pqSHYXBoundaryAssignmentInfoWidget::upstreamFilePath() const
{
  return filePathFromProxy(topLevelProducer(this->proxy()));
}

QString pqSHYXBoundaryAssignmentInfoWidget::caseIdFromFilePath(const QString& filePath)
{
  if (filePath.isEmpty())
  {
    return QString();
  }
  QString stem = QFileInfo(filePath).completeBaseName().trimmed();
  if (stem.isEmpty())
  {
    return QString();
  }
  // K2-1_plaque / K2-1_aorta → K2-1
  const QString lower = stem.toLower();
  if (lower.endsWith(QStringLiteral("_plaque")))
  {
    stem.chop(7);
  }
  else if (lower.endsWith(QStringLiteral("_aorta")))
  {
    stem.chop(6);
  }
  return stem.trimmed();
}

int pqSHYXBoundaryAssignmentInfoWidget::inferFlowModeFromFileName(const QString& filePath)
{
  if (filePath.isEmpty())
  {
    return -1;
  }
  // Match against the basename only (not the directory path).
  const QString name = QFileInfo(filePath).fileName().toLower();
  const bool aorta = name.contains(QStringLiteral("aorta"));
  const bool plaque = name.contains(QStringLiteral("plaque"));
  if (plaque && !aorta)
  {
    return 0; // Single inlet → PV
  }
  if (aorta && !plaque)
  {
    return 1; // Single outlet → HV
  }
  return -1; // none or ambiguous
}

QString pqSHYXBoundaryAssignmentInfoWidget::resolveCaseId() const
{
  const QString id = caseIdFromFilePath(this->upstreamFilePath());
  return id.isEmpty() ? QStringLiteral("0") : id;
}

QString pqSHYXBoundaryAssignmentInfoWidget::resolveExportKey() const
{
  return this->currentModeTag() + QLatin1Char('|') + this->resolveCaseId();
}

//-----------------------------------------------------------------------------
pqSHYXBoundaryAssignmentInfoWidget::pqSHYXBoundaryAssignmentInfoWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  this->AssignmentProp = vtkSMStringVectorProperty::SafeDownCast(
    propertyFromGroup(smgroup, smproxy, "AssignmentText", "BoundaryAssignmentText"));
  this->InletOptProp = vtkSMStringVectorProperty::SafeDownCast(
    propertyFromGroup(smgroup, smproxy, "InletOptText", "InletOptText"));

  auto* namesHost = new QWidget(this);
  auto* namesForm = new QFormLayout(namesHost);
  namesForm->setContentsMargins(0, 0, 0, 0);
  namesForm->setSpacing(4);
  this->ExoNameEdit = new QLineEdit(namesHost);
  this->OptNameEdit = new QLineEdit(namesHost);
  this->BcNameEdit = new QLineEdit(namesHost);
  this->PvsmNameEdit = new QLineEdit(namesHost);
  this->ExoNameEdit->setPlaceholderText(QStringLiteral("PV_K2-1.exo / HV_K2-1.exo"));
  this->OptNameEdit->setPlaceholderText(QStringLiteral("options_PV_K2-1 / options_HV_K2-1"));
  this->BcNameEdit->setPlaceholderText(QStringLiteral("Nodeset_PV_K2-1 / Nodeset_HV_K2-1"));
  this->PvsmNameEdit->setPlaceholderText(QStringLiteral("PV_K2-1.pvsm / HV_K2-1.pvsm"));
  this->ExoNameEdit->setToolTip(tr(
    "Exodus output basename. Default PV_<case>.exo or HV_<case>.exo from the top-level "
    "source (e.g. K2-1_plaque.stl → PV_K2-1.exo). Falls back to PV_0.exo / HV_0.exo."));
  this->OptNameEdit->setToolTip(tr(
    "Options file basename (no extension). Default options_PV_<case> / options_HV_<case>."));
  this->BcNameEdit->setToolTip(tr(
    "Boundary assignment / nodeset basename (no extension). "
    "Default Nodeset_PV_<case> / Nodeset_HV_<case>."));
  this->PvsmNameEdit->setToolTip(tr(
    "ParaView state basename. Default PV_<case>.pvsm or HV_<case>.pvsm. "
    "Written beside the Exodus file; captures the current pipeline / views."));
  namesForm->addRow(tr("Exodus (.exo)"), this->ExoNameEdit);
  namesForm->addRow(tr("Options (no ext)"), this->OptNameEdit);
  namesForm->addRow(tr("Nodeset (no ext)"), this->BcNameEdit);
  namesForm->addRow(tr("State (.pvsm)"), this->PvsmNameEdit);
  vbox->addWidget(namesHost);

  this->PackZipCheck = new QCheckBox(tr("Pack into .zip"), this);
  this->PackZipCheck->setToolTip(tr(
    "When checked, export saves one .zip that contains the Exodus, options, Nodeset, "
    "and .pvsm files. Loose files are not written beside it."));
  vbox->addWidget(this->PackZipCheck);

  // Filename hint before wiring mode Modified → avoids a redundant name sync mid-setup.
  this->syncFlowModeFromUpstreamFile();
  {
    QString tag;
    QString caseId;
    this->LastAutoKey = this->resolveExportKey();
    splitExportKey(this->LastAutoKey, tag, caseId);
    this->ExoNameEdit->setText(defaultExoName(tag, caseId));
    this->OptNameEdit->setText(defaultOptName(tag, caseId));
    this->BcNameEdit->setText(defaultNodesetName(tag, caseId));
    this->PvsmNameEdit->setText(defaultPvsmName(tag, caseId));
  }

  if (vtkSMProperty* modeProp = smproxy->GetProperty("FlowBoundaryMode"))
  {
    pqCoreUtilities::connect(
      modeProp, vtkCommand::ModifiedEvent, this, SLOT(syncExportNameDefaults()));
  }
  if (vtkSMProperty* inputProp = smproxy->GetProperty("Input"))
  {
    // Re-resolve mode + names when the upstream connection changes.
    pqCoreUtilities::connect(
      inputProp, vtkCommand::ModifiedEvent, this, SLOT(syncFlowModeFromUpstreamFile()));
    pqCoreUtilities::connect(
      inputProp, vtkCommand::ModifiedEvent, this, SLOT(syncExportNameDefaults()));
  }

  auto* exportBtn =
    new QPushButton(tr("Export port 0 (.exo) + options + Nodeset + .pvsm"), this);
  exportBtn->setToolTip(tr(
    "Choose a folder via the .exo save dialog (suggested name from the box above). "
    "Writes the Exodus file plus extensionless options, Nodeset, and the current "
    "ParaView state (.pvsm) beside it. With Pack into .zip, those four files go "
    "inside one archive instead."));
  vbox->addWidget(exportBtn);
  QObject::connect(exportBtn, &QPushButton::clicked, this,
    &pqSHYXBoundaryAssignmentInfoWidget::onExportClicked);

  auto* assignLabel = new QLabel(tr("Boundary assignment (id/nodeset/sideset)"), this);
  vbox->addWidget(assignLabel);
  this->AssignmentEdit = makeReadonlyMultiline(this, 14);
  vbox->addWidget(this->AssignmentEdit);

  auto* optLabel = new QLabel(tr("Options file (OPT)"), this);
  vbox->addWidget(optLabel);
  this->InletOptEdit = makeReadonlyMultiline(this, 22);
  vbox->addWidget(this->InletOptEdit);

  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    this->PipelineSource = smm->findItem<pqPipelineSource*>(smproxy);
    if (this->PipelineSource)
    {
      QObject::connect(this->PipelineSource.data(),
        static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(&pqPipelineSource::dataUpdated),
        this, [this](pqPipelineSource*) {
          this->refreshTexts();
          this->configureDebugPointLabels();
          // Representation may appear one tick after Apply; force OccludeLabels off again.
          QTimer::singleShot(0, this, [this]() { this->configureDebugPointLabels(); });
        });
    }
  }

  this->refreshTexts();
}

//-----------------------------------------------------------------------------
pqSHYXBoundaryAssignmentInfoWidget::~pqSHYXBoundaryAssignmentInfoWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::syncFlowModeFromUpstreamFile()
{
  if (this->ApplyingAutoFlowMode)
  {
    return;
  }

  const QString path = this->upstreamFilePath();
  if (path == this->LastHintFilePath)
  {
    return;
  }
  this->LastHintFilePath = path;

  const int inferred = inferFlowModeFromFileName(path);
  if (inferred < 0)
  {
    return;
  }

  vtkSMProxy* filter = this->proxy();
  vtkSMProperty* modeProp = filter ? filter->GetProperty("FlowBoundaryMode") : nullptr;
  if (!modeProp)
  {
    return;
  }

  const int current = vtkSMPropertyHelper(modeProp).GetAsInt();
  if (current == inferred)
  {
    return;
  }

  this->ApplyingAutoFlowMode = true;
  vtkSMPropertyHelper(modeProp).Set(inferred);
  filter->UpdateVTKObjects();
  this->ApplyingAutoFlowMode = false;
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::syncExportNameDefaults()
{
  if (this->ApplyingAutoFlowMode)
  {
    // Mode auto-set will be followed by an explicit name sync from Input / ctor / refresh.
    return;
  }

  const QString key = this->resolveExportKey();
  if (key == this->LastAutoKey)
  {
    return;
  }

  QString oldTag;
  QString oldCaseId;
  QString newTag;
  QString newCaseId;
  splitExportKey(this->LastAutoKey, oldTag, oldCaseId);
  splitExportKey(key, newTag, newCaseId);

  const QString oldExo = defaultExoName(oldTag, oldCaseId);
  const QString oldOpt = defaultOptName(oldTag, oldCaseId);
  const QString oldBc = defaultNodesetName(oldTag, oldCaseId);
  const QString oldPvsm = defaultPvsmName(oldTag, oldCaseId);
  const QString newExo = defaultExoName(newTag, newCaseId);
  const QString newOpt = defaultOptName(newTag, newCaseId);
  const QString newBc = defaultNodesetName(newTag, newCaseId);
  const QString newPvsm = defaultPvsmName(newTag, newCaseId);

  // Preserve manual edits: only rewrite boxes that still hold the previous auto default.
  if (this->ExoNameEdit && this->ExoNameEdit->text().trimmed() == oldExo)
  {
    this->ExoNameEdit->setText(newExo);
  }
  if (this->OptNameEdit && this->OptNameEdit->text().trimmed() == oldOpt)
  {
    this->OptNameEdit->setText(newOpt);
  }
  if (this->BcNameEdit && this->BcNameEdit->text().trimmed() == oldBc)
  {
    this->BcNameEdit->setText(newBc);
  }
  if (this->PvsmNameEdit && this->PvsmNameEdit->text().trimmed() == oldPvsm)
  {
    this->PvsmNameEdit->setText(newPvsm);
  }
  this->LastAutoKey = key;
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::setTextFromProperty(
  QTextEdit* edit, vtkSMStringVectorProperty* prop)
{
  if (!edit)
  {
    return;
  }
  if (!prop || prop->GetNumberOfElements() < 1)
  {
    edit->setPlainText(QString());
    return;
  }
  const char* value = prop->GetElement(0);
  edit->setPlainText(QString::fromUtf8(value ? value : ""));
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::refreshTexts()
{
  vtkSMProxy* filter = this->proxy();
  if (!filter)
  {
    return;
  }

  this->syncFlowModeFromUpstreamFile();
  this->syncExportNameDefaults();

  auto* source = vtkSMSourceProxy::SafeDownCast(filter);
  if (this->AssignmentProp)
  {
    if (source)
    {
      source->UpdatePropertyInformation(this->AssignmentProp);
    }
    else
    {
      filter->UpdatePropertyInformation(this->AssignmentProp);
    }
  }
  if (this->InletOptProp)
  {
    if (source)
    {
      source->UpdatePropertyInformation(this->InletOptProp);
    }
    else
    {
      filter->UpdatePropertyInformation(this->InletOptProp);
    }
  }

  this->setTextFromProperty(this->AssignmentEdit, this->AssignmentProp);
  this->setTextFromProperty(this->InletOptEdit, this->InletOptProp);
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::configureDebugPointLabels()
{
  if (!this->PipelineSource)
  {
    return;
  }
  pqOutputPort* port1 = this->PipelineSource->getOutputPort(1);
  if (!port1)
  {
    return;
  }

  auto applyToProxy = [](vtkSMProxy* proxy) {
    if (!proxy)
    {
      return;
    }
    auto setInt = [proxy](const char* name, int value) {
      if (vtkSMProperty* p = proxy->GetProperty(name))
      {
        vtkSMPropertyHelper(p).Set(value);
      }
    };
    auto setStr = [proxy](const char* name, const char* value) {
      if (vtkSMProperty* p = proxy->GetProperty(name))
      {
        vtkSMPropertyHelper(p).Set(value);
      }
    };

    // Display proxy exposes Point Label settings as PL_* (GetSubProxy is protected).
    setStr("Representation", "Point Label");
    setInt("PL_ShowPointLabels", 1);
    setInt("PL_OccludeLabels", 0); // port-1 only: always-on-top 2D overlay
    setInt("PL_VertexOnly", 1);
    setStr("PL_PointLabelArray", "Label");
    proxy->UpdateVTKObjects();
  };

  const QList<pqDataRepresentation*> reps = port1->getRepresentations(nullptr);
  for (pqDataRepresentation* rep : reps)
  {
    if (!rep)
    {
      continue;
    }
    applyToProxy(rep->getProxy());
    if (pqView* view = rep->getView())
    {
      view->render();
    }
  }
}

//-----------------------------------------------------------------------------
void pqSHYXBoundaryAssignmentInfoWidget::onExportClicked()
{
  this->refreshTexts();

  auto* source = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!source)
  {
    QMessageBox::warning(this, tr("Export"), tr("Filter proxy is missing."));
    return;
  }

  QString tag;
  QString caseId;
  splitExportKey(this->resolveExportKey(), tag, caseId);

  QString exoName = ensureExtension(
    this->ExoNameEdit ? this->ExoNameEdit->text() : QString(), QStringLiteral(".exo"));
  // Options / Nodeset intentionally have no extension.
  QString optName = this->OptNameEdit ? this->OptNameEdit->text().trimmed() : QString();
  QString bcName = this->BcNameEdit ? this->BcNameEdit->text().trimmed() : QString();
  QString pvsmName = ensureExtension(
    this->PvsmNameEdit ? this->PvsmNameEdit->text() : QString(), QStringLiteral(".pvsm"));
  if (exoName.isEmpty())
  {
    exoName = defaultExoName(tag, caseId);
  }
  if (optName.isEmpty())
  {
    optName = defaultOptName(tag, caseId);
  }
  if (bcName.isEmpty())
  {
    bcName = defaultNodesetName(tag, caseId);
  }
  if (pvsmName.isEmpty())
  {
    pvsmName = defaultPvsmName(tag, caseId);
  }

  const QStringList archiveNames{ QFileInfo(exoName).fileName(), QFileInfo(optName).fileName(),
    QFileInfo(bcName).fileName(), QFileInfo(pvsmName).fileName() };
  for (int i = 0; i < archiveNames.size(); ++i)
  {
    for (int j = i + 1; j < archiveNames.size(); ++j)
    {
      if (archiveNames.at(i).compare(archiveNames.at(j), Qt::CaseInsensitive) == 0)
      {
        QMessageBox::warning(this, tr("Export"),
          tr("Export names must be different.\n\"%1\" is used more than once.")
            .arg(archiveNames.at(i)));
        return;
      }
    }
  }

  const bool packZip = this->PackZipCheck && this->PackZipCheck->isChecked();
  QString zipPath;
  std::unique_ptr<QTemporaryDir> scratch;
  QString exoFile;
  QString optPath;
  QString bcPath;
  QString pvsmPath;

  if (packZip)
  {
    const QString zipSuggest = QFileInfo(exoName).completeBaseName() + QStringLiteral(".zip");
    zipPath = QFileDialog::getSaveFileName(pqCoreUtilities::mainWidget(), tr("Export zip package"),
      zipSuggest, tr("Zip archive (*.zip);;All files (*)"));
    if (zipPath.isEmpty())
    {
      return;
    }
    zipPath = ensureExtension(zipPath, QStringLiteral(".zip"));
    scratch.reset(new QTemporaryDir());
    if (!scratch->isValid())
    {
      QMessageBox::critical(
        this, tr("Export"), tr("Cannot create a temporary folder for the package."));
      return;
    }
    exoFile = scratch->filePath(archiveNames.at(0));
    optPath = scratch->filePath(archiveNames.at(1));
    bcPath = scratch->filePath(archiveNames.at(2));
    pvsmPath = scratch->filePath(archiveNames.at(3));
  }
  else
  {
    const QString exoPath = QFileDialog::getSaveFileName(pqCoreUtilities::mainWidget(),
      tr("Export Exodus + options + Nodeset + .pvsm"), exoName,
      tr("Exodus (*.exo);;All files (*)"));
    if (exoPath.isEmpty())
    {
      return;
    }

    exoFile = ensureExtension(exoPath, QStringLiteral(".exo"));
    const QString outDir = QFileInfo(exoFile).absolutePath();
    optPath = outDir + QLatin1Char('/') + archiveNames.at(1);
    bcPath = outDir + QLatin1Char('/') + archiveNames.at(2);
    pvsmPath = outDir + QLatin1Char('/') + archiveNames.at(3);

    // Keep the exo line edit in sync with the path the user actually chose.
    if (this->ExoNameEdit)
    {
      this->ExoNameEdit->setText(QFileInfo(exoFile).fileName());
    }
  }

  source->UpdatePipeline();
  source->UpdatePipelineInformation();

  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(source->GetClientSideObject());
  if (!alg)
  {
    QMessageBox::warning(this, tr("Export"),
      tr("Cannot access client-side filter (remote servers are not supported for this export)."));
    return;
  }

  auto* pdc = vtkPartitionedDataSetCollection::SafeDownCast(alg->GetOutputDataObject(0));
  QString error;
  if (!writeExodusIossDefault(pdc, exoFile, &error))
  {
    QMessageBox::critical(this, tr("Export"), error);
    return;
  }

  const QString assignText =
    this->AssignmentEdit ? this->AssignmentEdit->toPlainText() : QString();
  const QString optText = this->InletOptEdit ? this->InletOptEdit->toPlainText() : QString();
  if (!writeTextFile(bcPath, assignText, &error) || !writeTextFile(optPath, optText, &error))
  {
    QMessageBox::critical(this, tr("Export"), error);
    return;
  }

  auto* core = pqApplicationCore::instance();
  if (!core || !core->saveState(pvsmPath))
  {
    if (packZip)
    {
      QMessageBox::critical(this, tr("Export"),
        tr("Failed to save ParaView state, so the zip package was not written."));
    }
    else
    {
      QMessageBox::critical(this, tr("Export"),
        tr("Wrote Exodus / options / Nodeset, but failed to save ParaView state:\n%1")
          .arg(pvsmPath));
    }
    return;
  }

  if (packZip)
  {
    QList<ZipPayload> payloads;
    const QStringList paths{ exoFile, optPath, bcPath, pvsmPath };
    for (int i = 0; i < paths.size(); ++i)
    {
      ZipPayload item;
      item.name = archiveNames.at(i);
      if (!readFileBytes(paths.at(i), &item.bytes, &error))
      {
        QMessageBox::critical(this, tr("Export"), error);
        return;
      }
      payloads.append(item);
    }
    if (!writeZipFile(zipPath, payloads, &error))
    {
      QFile::remove(zipPath);
      QMessageBox::critical(this, tr("Export"), error);
      return;
    }
    QMessageBox::information(this, tr("Export"),
      tr("Wrote package:\n%1\n\nContents:\n%2\n%3\n%4\n%5")
        .arg(zipPath, archiveNames.at(0), archiveNames.at(1), archiveNames.at(2),
          archiveNames.at(3)));
    return;
  }

  QMessageBox::information(this, tr("Export"),
    tr("Wrote:\n%1\n%2\n%3\n%4").arg(exoFile, optPath, bcPath, pvsmPath));
}
