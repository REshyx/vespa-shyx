#pragma once
/* Minimal POSIX unistd.h stand-in for OpenFOAM on MSVC/clang-cl. */
#include <io.h>
#include <process.h>
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif
