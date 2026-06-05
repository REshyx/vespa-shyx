#include "vtkSHYXPartitionedCollectionWslSimulation.h"

#include "vtkDataArraySelection.h"
#include "vtkDataAssembly.h"
#include "vtkDataObject.h"
#include "vtkDirectory.h"
#include "vtkErrorCode.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkIOSSReader.h"
#include "vtkIOSSWriter.h"
#include "vtkObjectFactory.h"
#include "vtkPartitionedDataSetCollection.h"

#include "vtkLogger.h"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXPartitionedCollectionWslSimulation);

namespace
{
struct WslUncPathInfo
{
  bool Valid = false;
  std::string Distro;
  std::string LinuxPath;
};

bool CharIEqual(char a, char b)
{
  return std::tolower(static_cast<unsigned char>(a)) ==
    std::tolower(static_cast<unsigned char>(b));
}

bool ContainsI(const std::string& haystack, const char* needle)
{
  const std::string n(needle);
  if (n.empty() || haystack.size() < n.size())
  {
    return false;
  }
  for (size_t i = 0; i + n.size() <= haystack.size(); ++i)
  {
    bool match = true;
    for (size_t j = 0; j < n.size(); ++j)
    {
      if (!CharIEqual(haystack[i + j], n[j]))
      {
        match = false;
        break;
      }
    }
    if (match)
    {
      return true;
    }
  }
  return false;
}

std::string ToLowerCopy(std::string s)
{
  for (char& c : s)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string NormalizePathSeparators(std::string path)
{
  for (char& c : path)
  {
    if (c == '/')
    {
      c = '\\';
    }
  }
  return path;
}

std::string UnixDirname(const std::string& unixPath)
{
  const size_t pos = unixPath.find_last_of('/');
  if (pos == std::string::npos)
  {
    return ".";
  }
  if (pos == 0)
  {
    return "/";
  }
  return unixPath.substr(0, pos);
}

/** Parse WSL UNC (any leading slashes) into distro + Linux path. */
WslUncPathInfo ParseWslUncPath(const std::string& path)
{
  WslUncPathInfo info;
  const std::string normalized = NormalizePathSeparators(path);
  const std::string lower = ToLowerCopy(normalized);

  const char* markers[] = { "wsl.localhost\\", "wsl$\\" };
  size_t markerPos = std::string::npos;
  size_t markerLen = 0;
  for (const char* marker : markers)
  {
    const size_t pos = lower.find(marker);
    if (pos != std::string::npos && (markerPos == std::string::npos || pos < markerPos))
    {
      markerPos = pos;
      markerLen = std::strlen(marker);
    }
  }
  if (markerPos == std::string::npos)
  {
    return info;
  }

  const size_t distroStart = markerPos + markerLen;
  const size_t distroEnd = normalized.find('\\', distroStart);
  if (distroEnd == std::string::npos || distroEnd <= distroStart)
  {
    return info;
  }

  info.Distro = normalized.substr(distroStart, distroEnd - distroStart);
  std::string tail = normalized.substr(distroEnd + 1);
  for (char& c : tail)
  {
    if (c == '\\')
    {
      c = '/';
    }
  }
  if (!tail.empty() && tail.front() != '/')
  {
    tail = "/" + tail;
  }
  info.LinuxPath = tail;
  info.Valid = !info.Distro.empty() && !info.LinuxPath.empty();
  return info;
}

std::string BashSingleQuote(const std::string& s)
{
  std::string out = "'";
  for (char c : s)
  {
    if (c == '\'')
    {
      out += "'\\''";
    }
    else
    {
      out += c;
    }
  }
  out += "'";
  return out;
}

int RunWslCommand(const std::string& distro, const std::string& bashBody)
{
  std::ostringstream cmd;
#ifdef _WIN32
  cmd << "wsl -d " << distro << " -- bash -lc " << BashSingleQuote(bashBody);
#else
  (void)distro;
  cmd << "bash -lc " << BashSingleQuote(bashBody);
#endif
  vtkLogF(INFO, "Executing: %s", cmd.str().c_str());
  return std::system(cmd.str().c_str());
}

std::string JoinPath(const std::string& dir, const std::string& file)
{
  if (dir.empty())
  {
    return file;
  }
  if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
  {
    return dir + file;
  }
#ifdef _WIN32
  return dir + '\\' + file;
#else
  return dir + '/' + file;
#endif
}

std::string JoinUnixPath(const std::string& dir, const std::string& file)
{
  if (dir.empty())
  {
    return file;
  }
  if (dir.back() == '/')
  {
    return dir + file;
  }
  return dir + "/" + file;
}

/** Forward slashes for wslpath -u on Windows. */
std::string ToWslpathWindowsArg(std::string path)
{
  for (char& c : path)
  {
    if (c == '\\')
    {
      c = '/';
    }
  }
  return path;
}

bool PathExists(const std::string& path)
{
#ifdef _WIN32
  return _access(path.c_str(), 0) == 0;
#else
  return access(path.c_str(), F_OK) == 0;
#endif
}

#ifdef _WIN32
std::string GetWindowsStagingDirectory()
{
  const char* temp = std::getenv("TEMP");
  if (temp == nullptr || temp[0] == '\0')
  {
    temp = std::getenv("TMP");
  }
  if (temp != nullptr && temp[0] != '\0')
  {
    return JoinPath(temp, "vespa-shyx-wsl");
  }
  return "vespa-shyx-wsl-staging";
}

bool ShouldUseWslStaging(const std::string& workDir)
{
  const WslUncPathInfo wsl = ParseWslUncPath(workDir);
  return wsl.Valid || ContainsI(workDir, "wsl");
}

bool EnsureWindowsDirectory(const std::string& dir)
{
  if (dir.empty())
  {
    return false;
  }
  if (PathExists(dir))
  {
    return true;
  }
  return vtkDirectory::MakeDirectory(dir.c_str()) != 0;
}
#endif

WslUncPathInfo ResolveLinuxWorkDir(
  const std::string& workDir, const std::string& defaultDistro, const std::string& runScriptLinuxPath)
{
  WslUncPathInfo wsl = ParseWslUncPath(workDir);
  if (!wsl.Valid && ContainsI(workDir, "wsl"))
  {
    wsl.Valid = true;
    wsl.Distro = defaultDistro;
    wsl.LinuxPath = UnixDirname(runScriptLinuxPath);
  }
  return wsl;
}

bool EnsureDirectory(
  const std::string& dir, const std::string& defaultDistro, const std::string& runScriptLinuxPath)
{
  if (dir.empty())
  {
    return false;
  }

  WslUncPathInfo wsl = ParseWslUncPath(dir);
#ifdef _WIN32
  if (!wsl.Valid && ContainsI(dir, "wsl"))
  {
    wsl.Valid = true;
    wsl.Distro = defaultDistro;
    wsl.LinuxPath = UnixDirname(runScriptLinuxPath);
    vtkLogF(WARNING,
      "WorkDirectory is not a recognized WSL UNC; using Linux path %s from run script.",
      wsl.LinuxPath.c_str());
  }
  if (wsl.Valid)
  {
    const std::string& distro = wsl.Distro.empty() ? defaultDistro : wsl.Distro;
    const int rc = RunWslCommand(distro, "mkdir -p " + wsl.LinuxPath);
    if (rc == 0)
    {
      return true;
    }
    vtkLogF(ERROR, "wsl mkdir -p %s failed (exit %d).", wsl.LinuxPath.c_str(), rc);
    return false;
  }
#endif

  if (PathExists(dir))
  {
    return true;
  }
  return vtkDirectory::MakeDirectory(dir.c_str()) != 0;
}

bool WritePartitionedCollectionExodus(
  vtkPartitionedDataSetCollection* input, const std::string& filePath)
{
  if (!input)
  {
    return false;
  }

  vtkNew<vtkIOSSWriter> writer;
  writer->SetInputData(input);
  writer->SetFileName(filePath.c_str());

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
  return writer->GetErrorCode() == vtkErrorCode::NoError;
}

bool ReadExodusToCollection(const std::string& filePath, vtkPartitionedDataSetCollection* output)
{
  if (!output)
  {
    return false;
  }

  vtkNew<vtkIOSSReader> reader;
  reader->SetFileName(filePath.c_str());
  reader->UpdateInformation();

  if (reader->GetElementBlockSelection())
  {
    reader->GetElementBlockSelection()->EnableAllArrays();
  }
  if (reader->GetSideSetSelection())
  {
    reader->GetSideSetSelection()->EnableAllArrays();
  }
  if (reader->GetNodeSetSelection())
  {
    reader->GetNodeSetSelection()->EnableAllArrays();
  }

  reader->Update();
  vtkDataObject* result = reader->GetOutputDataObject(0);
  auto* pdc = vtkPartitionedDataSetCollection::SafeDownCast(result);
  if (!pdc)
  {
    return false;
  }
  output->ShallowCopy(pdc);
  return true;
}

int RunWslScript(const std::string& distro, const std::string& scriptPath)
{
  if (scriptPath.empty())
  {
    return -1;
  }
  return RunWslCommand(distro, "bash " + scriptPath);
}

void AppendWslCopyFromWindows(
  std::ostringstream& body, const std::string& winPath, const std::string& linuxDest)
{
  body << "cp -f \"$(wslpath -u " << BashSingleQuote(ToWslpathWindowsArg(winPath)) << ")\" "
       << BashSingleQuote(linuxDest) << "; ";
}

/** Copy Exodus (and optional option file) to WSL work dir, run script, copy result back. */
int RunWslTransferAndScript(const std::string& distro, const std::string& linuxWorkDir,
  const std::string& winInputExo, const std::string& winResultExo, const std::string& linuxInputName,
  const std::string& linuxResultName, const std::string& runScript, bool runScriptFlag,
  const std::string& winOptionFile, const std::string& linuxOptionDest)
{
  const std::string winIn = ToWslpathWindowsArg(winInputExo);
  const std::string winOut = ToWslpathWindowsArg(winResultExo);
  const std::string linuxInput = JoinUnixPath(linuxWorkDir, linuxInputName);
  const std::string linuxResult = JoinUnixPath(linuxWorkDir, linuxResultName);

  std::ostringstream body;
  body << "set -euo pipefail; ";
  if (!winOptionFile.empty() && !linuxOptionDest.empty())
  {
    AppendWslCopyFromWindows(body, winOptionFile, linuxOptionDest);
  }
  body << "cp -f \"$(wslpath -u " << BashSingleQuote(winIn) << ")\" "
       << BashSingleQuote(linuxInput) << "; ";
  if (runScriptFlag)
  {
    body << "bash " << BashSingleQuote(runScript) << "; ";
  }
  body << "cp -f " << BashSingleQuote(linuxResult) << " "
       << "\"$(wslpath -u " << BashSingleQuote(winOut) << ")\"";

  return RunWslCommand(distro, body.str());
}

int CopyOptionFileToWsl(const std::string& distro, const std::string& linuxWorkDir,
  const std::string& winOptionFile, const std::string& linuxOptionDest)
{
  if (winOptionFile.empty() || linuxOptionDest.empty())
  {
    return 0;
  }
  std::ostringstream body;
  body << "set -euo pipefail; ";
  AppendWslCopyFromWindows(body, winOptionFile, linuxOptionDest);
  return RunWslCommand(distro, body.str());
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXPartitionedCollectionWslSimulation::vtkSHYXPartitionedCollectionWslSimulation()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetWorkDirectory("\\\\wsl.localhost\\Ubuntu-24.04\\home\\shyx\\Test");
  this->SetWslDistribution("Ubuntu-24.04");
  this->SetRunScriptWslPath("/home/shyx/Test/run.sh");
  this->SetInputExoFileName("input.exo");
  this->SetResultExoFileName("result.exo");
  this->SetOptionFilePath("");
  this->SetOptionFileWslName("opt");
}

//------------------------------------------------------------------------------
vtkSHYXPartitionedCollectionWslSimulation::~vtkSHYXPartitionedCollectionWslSimulation()
{
  this->SetWorkDirectory(nullptr);
  this->SetWslDistribution(nullptr);
  this->SetRunScriptWslPath(nullptr);
  this->SetInputExoFileName(nullptr);
  this->SetResultExoFileName(nullptr);
  this->SetOptionFilePath(nullptr);
  this->SetOptionFileWslName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXPartitionedCollectionWslSimulation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "WorkDirectory: " << (this->WorkDirectory ? this->WorkDirectory : "(null)")
     << "\n";
  os << indent << "WslDistribution: "
     << (this->WslDistribution ? this->WslDistribution : "(null)") << "\n";
  os << indent << "RunScriptWslPath: "
     << (this->RunScriptWslPath ? this->RunScriptWslPath : "(null)") << "\n";
  os << indent << "InputExoFileName: "
     << (this->InputExoFileName ? this->InputExoFileName : "(null)") << "\n";
  os << indent << "ResultExoFileName: "
     << (this->ResultExoFileName ? this->ResultExoFileName : "(null)") << "\n";
  os << indent << "OptionFilePath: " << (this->OptionFilePath ? this->OptionFilePath : "(null)")
     << "\n";
  os << indent << "OptionFileWslName: "
     << (this->OptionFileWslName ? this->OptionFileWslName : "(null)") << "\n";
  os << indent << "RunScript: " << this->RunScript << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionWslSimulation::FillInputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionWslSimulation::FillOutputPortInformation(
  int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXPartitionedCollectionWslSimulation::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPartitionedDataSetCollection* input =
    vtkPartitionedDataSetCollection::GetData(inputVector[0], 0);
  vtkPartitionedDataSetCollection* output =
    vtkPartitionedDataSetCollection::GetData(outputVector, 0);

  if (!input)
  {
    vtkErrorMacro(<< "Input vtkPartitionedDataSetCollection is null.");
    return 0;
  }
  if (!output)
  {
    vtkErrorMacro(<< "Output vtkPartitionedDataSetCollection is null.");
    return 0;
  }

  const char* workDir = this->WorkDirectory ? this->WorkDirectory : "";
  const char* distro = this->WslDistribution ? this->WslDistribution : "Ubuntu-24.04";
  const char* inputName = this->InputExoFileName ? this->InputExoFileName : "input.exo";
  const char* resultName = this->ResultExoFileName ? this->ResultExoFileName : "result.exo";
  const char* runScript = this->RunScriptWslPath ? this->RunScriptWslPath : "/home/shyx/Test/run.sh";
  const char* optionFilePath = this->OptionFilePath ? this->OptionFilePath : "";
  const char* optionWslName =
    (this->OptionFileWslName && this->OptionFileWslName[0] != '\0') ? this->OptionFileWslName : "opt";

  if (workDir[0] == '\0')
  {
    vtkErrorMacro(<< "WorkDirectory is empty.");
    return 0;
  }

  const WslUncPathInfo wslInfo = ResolveLinuxWorkDir(workDir, distro, runScript);
#ifdef _WIN32
  const bool useWslStaging = ShouldUseWslStaging(workDir);
#else
  const bool useWslStaging = false;
#endif

  if (!EnsureDirectory(workDir, distro, runScript))
  {
    if (wslInfo.Valid)
    {
      vtkErrorMacro(<< "Failed to create WSL directory via mkdir -p " << wslInfo.LinuxPath
                    << " (distro: " << (wslInfo.Distro.empty() ? distro : wslInfo.Distro.c_str())
                    << "). Is WSL running?");
    }
    else
    {
      vtkErrorMacro(<< "Failed to create or access WorkDirectory: " << workDir);
    }
    return 0;
  }

  std::string ioInputPath;
  std::string ioResultPath;
#ifdef _WIN32
  if (useWslStaging)
  {
    const std::string stagingDir = GetWindowsStagingDirectory();
    if (!EnsureWindowsDirectory(stagingDir))
    {
      vtkErrorMacro(<< "Failed to create Windows staging directory: " << stagingDir);
      return 0;
    }
    ioInputPath = JoinPath(stagingDir, inputName);
    ioResultPath = JoinPath(stagingDir, resultName);
    vtkLogF(INFO, "Using Windows staging for IOSS I/O: %s", stagingDir.c_str());
  }
  else
#endif
  {
    ioInputPath = JoinPath(workDir, inputName);
    ioResultPath = JoinPath(workDir, resultName);
  }

  if (!WritePartitionedCollectionExodus(input, ioInputPath))
  {
    vtkErrorMacro(<< "Failed to write Exodus file: " << ioInputPath);
    return 0;
  }
  vtkLogF(INFO, "Wrote input Exodus: %s", ioInputPath.c_str());

  std::string winOptionFile;
  std::string linuxOptionDest;
  if (optionFilePath[0] != '\0')
  {
    if (!PathExists(optionFilePath))
    {
      vtkErrorMacro(<< "Option file not found: " << optionFilePath);
      return 0;
    }
    winOptionFile = optionFilePath;
    if (wslInfo.Valid)
    {
      linuxOptionDest = JoinUnixPath(wslInfo.LinuxPath, optionWslName);
    }
    else
    {
      vtkErrorMacro(<< "Option file copy requires a WSL work directory.");
      return 0;
    }
    vtkLogF(INFO, "Will copy option file to WSL: %s", linuxOptionDest.c_str());
  }

  const std::string& effectiveDistro =
    (wslInfo.Valid && !wslInfo.Distro.empty()) ? wslInfo.Distro : std::string(distro);

  if (this->RunScript)
  {
    int rc = 0;
#ifdef _WIN32
    if (useWslStaging && wslInfo.Valid)
    {
      rc = RunWslTransferAndScript(effectiveDistro, wslInfo.LinuxPath, ioInputPath, ioResultPath,
        inputName, resultName, runScript, true, winOptionFile, linuxOptionDest);
    }
    else
#endif
    {
      if (!winOptionFile.empty())
      {
        rc = CopyOptionFileToWsl(effectiveDistro, wslInfo.LinuxPath, winOptionFile, linuxOptionDest);
        if (rc != 0)
        {
          vtkErrorMacro(<< "Failed to copy option file to WSL (exit " << rc << ").");
          return 0;
        }
      }
      rc = RunWslScript(distro, runScript);
    }
    if (rc != 0)
    {
      vtkErrorMacro(<< "WSL run script failed (exit " << rc << "): " << runScript);
      return 0;
    }
  }
#ifdef _WIN32
  else if (useWslStaging && wslInfo.Valid)
  {
    const int rc = RunWslTransferAndScript(effectiveDistro, wslInfo.LinuxPath, ioInputPath,
      ioResultPath, inputName, resultName, runScript, false, winOptionFile, linuxOptionDest);
    if (rc != 0)
    {
      vtkErrorMacro(<< "WSL file transfer failed (exit " << rc << ").");
      return 0;
    }
  }
#endif
  else if (!winOptionFile.empty() && wslInfo.Valid)
  {
    const int rc = CopyOptionFileToWsl(effectiveDistro, wslInfo.LinuxPath, winOptionFile, linuxOptionDest);
    if (rc != 0)
    {
      vtkErrorMacro(<< "Failed to copy option file to WSL (exit " << rc << ").");
      return 0;
    }
  }

  if (!ReadExodusToCollection(ioResultPath, output))
  {
    vtkErrorMacro(<< "Failed to read result Exodus: " << ioResultPath);
    return 0;
  }
  vtkLogF(INFO, "Read result Exodus: %s", ioResultPath.c_str());

  return 1;
}

VTK_ABI_NAMESPACE_END
