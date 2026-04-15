#define NOB_IMPLEMENTATION
#include "nob.h"

#if defined(__clang__)
#define CMD "clang"
#elif defined(__GNUC__)
#define CMD "cc"
#else
#error "Unsupported Compiler install GCC or CLANG"
#endif

#ifdef _WIN32
    const char *out   = "issex.exe";
#else
    const char *out   = "issex";
#endif

void common_cmd(Cmd *cmd)
{
    cmd_append(cmd, CMD);
    cmd_append(cmd, "-Wno-deprecated-declarations");
    cmd_append(cmd, "-Wall", "-Wextra");
    // cmd_append(cmd, "-g");
    cmd_append(cmd, "-std=c23");
    // cmd_append(cmd, "-DEMACS_PRINT");
    cmd_append(cmd, "-Wno-unused-function");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    static Cmd   cmd   = {0};

    common_cmd(&cmd);
    cmd_append(&cmd, "-o", out, "main.c");
    if (!nob_cmd_run(&cmd)) return 1;

    return 0;
}
