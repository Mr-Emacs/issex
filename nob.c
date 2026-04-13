#define NOB_IMPLEMENTATION
#include "nob.h"

#if defined(__clang__)
#define CMD "clang"
#else
#define CMD "cc"
#endif

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    Cmd cmd = {0};

    #ifdef _WIN32
    const char *out = "issex.exe";
    #else
    const char *out = "issex";
    #endif
    cmd_append(&cmd, CMD);
    cmd_append(&cmd, "-Wno-deprecated-declarations", "-ggdb", "-Wall", "-Wextra");
    cmd_append(&cmd, "-o", out, "main.c");
    if (!nob_cmd_run(&cmd)) return 1;
    return 0;
}
