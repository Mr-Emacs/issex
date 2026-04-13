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
    const char *query = "query.exe";
#else
    const char *out   = "issex";
    const char *query = "query";
#endif

void common_cmd(Cmd *cmd)
{
    cmd_append(cmd, CMD);
    cmd_append(cmd, "-Wno-deprecated-declarations");
    cmd_append(cmd, "-ggdb", "-Wall", "-Wextra");
    cmd_append(cmd, "-std=c23");
    cmd_append(cmd, "-Wno-unused-function");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    static Cmd   cmd   = {0};
    static Procs procs = {0};

    common_cmd(&cmd);
    cmd_append(&cmd, "-o", out, "main.c");
    if (!nob_cmd_run(&cmd, .async = &procs)) return 1;

    common_cmd(&cmd);
    cmd_append(&cmd, "-o", out, "query.c");
    if (!nob_cmd_run(&cmd, .async = &procs)) return 1;

    if (!nob_procs_flush(&procs)) return 1;
    return 0;
}
