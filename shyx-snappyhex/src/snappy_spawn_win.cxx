// Windows process spawn for snappy_cli. Kept in a TU without OpenFOAM headers
// because windows.h macros (ERROR, IGNORE, DebugInfo) break Foam.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
void setErr(char* err, int err_len, const std::string& msg)
{
    if (!err || err_len <= 0)
    {
        return;
    }
    const size_t n = static_cast<size_t>(err_len - 1);
    const size_t m = msg.size() < n ? msg.size() : n;
    std::memcpy(err, msg.c_str(), m);
    err[m] = '\0';
}

bool fileExists(const std::string& p)
{
    return !p.empty() && GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string tailFile(const std::string& path, std::size_t maxBytes)
{
    std::ifstream is(path, std::ios::binary);
    if (!is)
    {
        return {};
    }
    is.seekg(0, std::ios::end);
    const auto sz = is.tellg();
    if (sz <= 0)
    {
        return {};
    }
    const auto n = std::min<std::streamoff>(static_cast<std::streamoff>(maxBytes), sz);
    is.seekg(-n, std::ios::end);
    std::string s(static_cast<std::size_t>(n), '\0');
    is.read(&s[0], n);
    return s;
}

std::string findSnappyCli()
{
    char env[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("SHYX_SNAPPY_CLI", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH && fileExists(env))
    {
        return env;
    }
    HMODULE h = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&fileExists), &h);
    char mod[MAX_PATH] = {};
    if (h && GetModuleFileNameA(h, mod, MAX_PATH))
    {
        std::string dir(mod);
        const auto slash = dir.find_last_of("\\/");
        if (slash != std::string::npos)
        {
            const std::string cli = dir.substr(0, slash + 1) + "snappy_cli.exe";
            if (fileExists(cli))
            {
                return cli;
            }
        }
    }
    return {};
}
} // namespace

extern "C" int shyx_host_is_paraview()
{
    char exe[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH))
    {
        return 0;
    }
    std::string s(exe);
    for (char& c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return (s.size() >= 12 && s.compare(s.size() - 12, 12, "paraview.exe") == 0) ? 1 : 0;
}

extern "C" int shyx_spawn_snappy_cli(const char* case_dir, char* err, int err_len)
{
    if (!case_dir)
    {
        setErr(err, err_len, "null case_dir");
        return 1;
    }
    const std::string cli = findSnappyCli();
    if (cli.empty())
    {
        setErr(err, err_len, "snappy_cli.exe not found next to VESPAPlugin.dll");
        return 6;
    }
    const std::string caseDir(case_dir);
    std::string cmd = "\"" + cli + "\" -runCase \"" + caseDir + "\"";
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(cli.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
            caseDir.c_str(), &si, &pi))
    {
        setErr(err, err_len, "CreateProcess snappy_cli failed");
        return 6;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != 0)
    {
        std::string msg = "snappy_cli exited " + std::to_string(static_cast<unsigned long>(code));
        const std::string tail = tailFile(caseDir + "/snappyHexMesh.log", 1200);
        if (!tail.empty())
        {
            msg += "\n--- log tail ---\n" + tail;
        }
        setErr(err, err_len, msg);
        return 4;
    }
    return 0;
}
#endif
