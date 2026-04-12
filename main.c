#define NOB_IMPLEMENTATION
#include "nob.h"

#include <time.h>
#include <sys/types.h>

#define NOTES "notes"

typedef struct task_t task_t;
struct task_t {
    char *path;
    time_t time;
    const char *task_name;
    char *current_path;
    char *notes;
};

int create_task_dir(task_t *task)
{
    Nob_File_Paths child = {0};
    nob_read_entire_dir(task->path, &child);

    nob_mkdir_if_not_exists(NOTES);

    task->time = time(NULL);
    struct tm *now = localtime(&task->time);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y%m%d", now);
    char *buf = temp_sprintf("%s_%s_%02d%02d%02d", task->task_name, buffer, 
                              now->tm_hour, now->tm_min, now->tm_sec);

    struct stat st = {0};

    if (chdir(NOTES) == 0) {
        if (stat(buf, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return 0;
            } 
        } else {
            if (!nob_mkdir_if_not_exists(buf)) return -1;
        }
    }

    size_t size = strlen(buf) + 1;
    task->current_path = malloc(size);
    memcpy(task->current_path, buf, size);
    task->current_path[size-1] = '\0';

    return 0;
}

int create_task_md(task_t *task)
{
    String_Builder sb = {0};
    sb_appendf(&sb, "# %s\n\n", task->task_name);
    sb_appendf(&sb, "- ID: %s\n\n", task->current_path);
    sb_append_cstr(&sb,  "## PRIORITY: 50\n\n");
    sb_append_cstr(&sb,  "## STATUS: OPEN\n\n");
    sb_appendf(&sb, "## NOTES: \n\n%s\n", task->notes);

    struct stat st = {0};

    if (stat(task->current_path, &st) == 0) {
        if (chdir(task->current_path) == 0) {
            if (!nob_write_entire_file("NOTES.md", sb.items, sb.count)) return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    UNUSED(argv);
    UNUSED(argc);


    // TODO: Customize this with command line argument
    task_t t = {0};
    t.path = ".";
    t.task_name = "task";
    t.notes = "This is aboba";

    if (create_task_dir(&t) < 0) {
        return 1;
    }

    if (create_task_md(&t) < 0) {
        return 1;
    }

    return 0;
}
