// CRT "lib" constructors run while the loader lock is held (same as DllMain).
// Only kernel32 SetEnvironmentVariable is used here.
// GetModuleHandleEx in this ctor deadlocks ParaView on LoadLibrary.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma warning(disable : 4073)
#pragma init_seg(lib)
namespace
{
struct ShyxFoamEnv
{
    ShyxFoamEnv()
    {
        SetEnvironmentVariableA("FOAM_SIGFPE", "false");
        SetEnvironmentVariableA("FOAM_ABORT", "false");
    }
};
const ShyxFoamEnv shyxFoamEnv;
}

extern "C" void shyx_touch_foam_env(void)
{
}

#else
extern "C" void shyx_touch_foam_env(void)
{
}
#endif
