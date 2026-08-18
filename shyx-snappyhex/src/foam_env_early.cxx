// CRT "lib" constructors run while the loader lock is held (same as DllMain).
// Only kernel32 SetEnvironmentVariable is used here.
// GetModuleHandleEx in this ctor deadlocks ParaView on LoadLibrary.
//
// Load trace: %TEMP%\vespa-snappy-load.log (FlushFileBuffers every line).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>

static bool shyxJoinPath(char* out, size_t cap, const char* a, const char* b)
{
    if (!out || cap < 4 || !a || !*a || !b)
    {
        return false;
    }
    const size_t na = std::strlen(a);
    const size_t nb = std::strlen(b);
    const bool slash = a[na - 1] == '\\' || a[na - 1] == '/';
    const size_t n = na + nb + (slash ? 0 : 1);
    if (n + 1 > cap)
    {
        return false;
    }
    std::memcpy(out, a, na);
    size_t i = na;
    if (!slash)
    {
        out[i++] = '\\';
    }
    std::memcpy(out + i, b, nb);
    out[i + nb] = '\0';
    return true;
}

extern "C" void shyx_load_log(const char* msg)
{
    if (!msg)
    {
        msg = "(null)";
    }
    OutputDebugStringA("vespa-snappy: ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");

    char path[MAX_PATH];
    path[0] = '\0';
    char tmp[MAX_PATH];
    if (GetEnvironmentVariableA("TEMP", tmp, MAX_PATH) > 0)
    {
        shyxJoinPath(path, sizeof(path), tmp, "vespa-snappy-load.log");
    }
    if (!path[0] && GetTempPathA(MAX_PATH, tmp) > 0)
    {
        shyxJoinPath(path, sizeof(path), tmp, "vespa-snappy-load.log");
    }
    if (!path[0])
    {
        std::memcpy(path, "C:\\Windows\\Temp\\vespa-snappy-load.log", 37);
        path[37] = '\0';
    }

    const HANDLE h = CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        return;
    }
    char line[1536];
    const int n = std::snprintf(
        line,
        sizeof(line),
        "[tick=%lu pid=%lu] %s\r\n",
        static_cast<unsigned long>(GetTickCount()),
        static_cast<unsigned long>(GetCurrentProcessId()),
        msg);
    if (n > 0)
    {
        DWORD written = 0;
        WriteFile(h, line, static_cast<DWORD>(n < static_cast<int>(sizeof(line)) ? n : sizeof(line) - 1),
            &written, nullptr);
        FlushFileBuffers(h);
    }
    CloseHandle(h);
}

static void shyxLogEnv(const char* name)
{
    char buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA(name, buf, MAX_PATH);
    char line[MAX_PATH + 64];
    if (n == 0)
    {
        std::snprintf(line, sizeof(line), "env %s=(unset) gle=%lu", name,
            static_cast<unsigned long>(GetLastError()));
    }
    else
    {
        std::snprintf(line, sizeof(line), "env %s=%s", name, buf);
    }
    shyx_load_log(line);
}

#pragma warning(disable : 4073)
#pragma init_seg(lib)
namespace
{
struct ShyxFoamEnv
{
    ShyxFoamEnv()
    {
        shyx_load_log("===== VESPAPlugin LoadLibrary / foam_env_early =====");
        shyxLogEnv("TEMP");
        shyxLogEnv("TMP");
        shyx_load_log("set FOAM_SIGFPE=false FOAM_ABORT=false");
        SetEnvironmentVariableA("FOAM_SIGFPE", "false");
        SetEnvironmentVariableA("FOAM_ABORT", "false");
        shyx_load_log("foam_env_early done (OpenFOAM static ctors next)");
    }
};
const ShyxFoamEnv shyxFoamEnv;
}

#pragma warning(disable : 4075)
#pragma init_seg(user)
namespace
{
struct ShyxFoamEnvUser
{
    ShyxFoamEnvUser() { shyx_load_log("init_seg user (OpenFOAM/user ctors in progress or done)"); }
};
const ShyxFoamEnvUser shyxFoamEnvUser;
}

extern "C" void shyx_touch_foam_env(void)
{
}

#else
extern "C" void shyx_load_log(const char* msg)
{
    (void)msg;
}

extern "C" void shyx_touch_foam_env(void)
{
}
#endif
