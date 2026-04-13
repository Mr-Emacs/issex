#define NOB_IMPLEMENTATION
#include "nob.h"


int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-ggdb", "-Wall", "-Wextra", "-o", "issex", "main.c");
    if (!nob_cmd_run(&cmd)) return 1;
    return 0;
}
