#define NOB_NO_ECHO
#define NOB_IMPLEMENTATION
#include "nob.h"
#include "flag.h"

#include <time.h>
#include <libgen.h>
#include <limits.h>
#include <sys/types.h>

#define NOTES "notes"

typedef struct header_t header_t;
typedef struct task_t task_t;

struct header_t {
    const char *title;
    bool status;
    size_t priority;
    char *notes;
    char *huid;
};

typedef struct {
    String_View *items;
    size_t count;
    size_t capacity;
} String_Views;

struct task_t {
    char *path;
    char root_path[PATH_MAX + sizeof("/" NOTES)];
    time_t time;
    const char *task_name;
    char *current_path;
    header_t header;
    String_Views list;
};

static int find_root(task_t *task);
static int create_task_dir(task_t *task);
static int create_task_md(task_t *task);
static int is_directory(const char *path);

static int is_directory(const char *path)
{
    struct stat st = {0};
    if (stat(path, &st) != 0) goto fail;
    if (!S_ISDIR(st.st_mode)) goto fail;

    return 0;

fail:
    return -1;
}

static int create_task_dir(task_t *task)
{

    if (find_root(task) < 0) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) return -1;
        snprintf(task->root_path, sizeof(task->root_path), "%s/" NOTES, cwd);
        if (!nob_mkdir_if_not_exists(task->root_path)) return -1;
    }

    task->time = time(NULL);
    struct tm *now = localtime(&task->time);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y%m%d", now);
    char *buf = temp_sprintf("%s_%s_%02d%02d%02d", task->task_name, buffer, 
                              now->tm_hour, now->tm_min, now->tm_sec);

    struct stat st = {0};

    if (chdir(task->root_path) == 0) {
        if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) {
            size_t size = strlen(buf) + 1;
            task->current_path = malloc(size);
            memcpy(task->current_path, buf, size);
            task->header.huid = malloc(size);
            memcpy(task->header.huid, buf, size);
            return 0;
        } else {
            if (!nob_mkdir_if_not_exists(buf)) return -1;
        }
    }

    // TODO: Use arena allocator
    size_t size = strlen(buf) + 1;
    task->current_path = malloc(size);
    memcpy(task->current_path, buf, size);
    task->current_path[size-1] = '\0';

    task->header.huid = malloc(size);
    memcpy(task->header.huid, buf, size);
    task->header.huid[size-1] = '\0';

    return 0;
}

static int create_task_md(task_t *task)
{
    if (create_task_dir(task) < 0) return -1;
    String_Builder sb = {0};
    task->header.title = task->task_name;
    sb_appendf(&sb, "# %s\n\n", task->task_name);
    sb_appendf(&sb, "- ID: %s\n\n", task->header.huid);
    sb_appendf(&sb,  "## PRIORITY: %ld\n\n", task->header.priority);

    sb_append_cstr(&sb,  "## STATUS: OPEN\n\n");
    task->header.status = true;

    sb_appendf(&sb, "## NOTES: \n\n%s\n", task->header.notes);

    struct stat st = {0};

    if (stat(task->current_path, &st) == 0) {
        if (chdir(task->current_path) == 0) {
            if (!nob_write_entire_file("NOTES.md", sb.items, sb.count)) return -1;
            nob_log(INFO, "Created issue with ID %s", task->header.huid);
        }
    }
    return 0;
}

static int find_root(task_t *task)
{
    char path[PATH_MAX];
    char old_dir[PATH_MAX];

    if (getcwd(old_dir, sizeof(old_dir)) == NULL) return -1;
    while(1) {
        if (getcwd(path, sizeof(path)) == NULL) goto fail;
        struct stat st = {0};

        if (stat(NOTES, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(task->root_path, sizeof(task->root_path), "%s/"NOTES, path);
            chdir(old_dir);
            return 0;
        }

        if (strcmp(path, "/") == 0) goto fail;
        if (chdir("..") != 0) goto fail;
    }

fail:
    chdir(old_dir);
    return -1;
}

static int list_tasks(task_t *task)
{
    if (task->list.count > 0) task->list.count = 0;
    if (find_root(task) < 0) return -1;
    File_Paths children = {0};
    if (!read_entire_dir(task->root_path, &children)) return -1;

    for (size_t i = 0; i < children.count; i++) {
        if (strcmp(children.items[i], "..") == 0 || strcmp(children.items[i], ".") == 0) continue;
        const char *full = temp_sprintf("%s/%s", task->root_path, children.items[i]);
        if (is_directory(full) == 0) {
            da_append(&task->list, sv_from_cstr(full));
        }
    }
    return 0;
}

static int close_task(task_t *task, const char *huid)
{
    String_Builder sb = {0};
    list_tasks(task);
    const char *full_huid = temp_sprintf("%s/%s", task->root_path, huid);
    if (list_tasks(task) < 0) return -1;
    for (size_t i = 0; i < task->list.count; i++) {
        if (sv_eq(task->list.items[i], sv_from_cstr(full_huid))) {
            const char *notes_path = temp_sprintf("%s/NOTES.md",
                                     temp_sv_to_cstr(task->list.items[i]));
            printf("%s\n", notes_path);

            if (!nob_read_entire_file(notes_path, &sb)) return -1;

            String_View content = nob_sb_to_sv(sb);
            String_Builder out = {0};
            while(content.count > 0) {
                String_View line = sv_chop_by_delim(&content, '\n');
                if (sv_starts_with(line, sv_from_cstr("## STATUS: "))) {
                    sb_append_cstr(&out, "## STATUS: CLOSED\n");
                } else {
                    sb_appendf(&out, SV_Fmt"\n", SV_Arg(line));
                }
            }
            if (!nob_write_entire_file(notes_path, out.items, out.count)) return -1;

            task->header.status = false;
            nob_log(INFO, "Closed task: %s", huid);
            return 0;
        }
    }
    return -1;
}

void usage(const char *program)
{
    printf("Usage: %s <command> [flags]\n\n", program);
    printf("Commands:\n");
    printf("  add    -name <name> -priority <level> -notes <notes>\n");
    printf("  list   -<query>\n");
    printf("  close  -id <huid>\n");
}

static int cmd_add(int argc, char **argv, task_t *task)
{
    char *name   = create_flag(argc, argv, char*, "name", "Name of the issue");
    int priority = create_flag(argc, argv, int, "priority", "Priority of the issue");
    char *notes  = create_flag(argc, argv, char*, "notes", "Notes for the current issue");
    if (!name) { 
        nob_log(ERROR, "add: missing name\n");
        return -1;
    }

    task->task_name = name;
    task->header.notes = notes;
    task->header.priority = (size_t)priority;

    return create_task_md(task);
}

static int cmd_ls(int argc, char **argv, task_t *task)
{
    // TODO: Add the query language
    UNUSED(argc);
    UNUSED(argv);
    if (list_tasks(task) < 0) return -1;
    for (size_t i = 0; i < task->list.count; ++i) {
        char *base = strdup(temp_sv_to_cstr(task->list.items[i]));
        char *name = basename(base);
        printf("%s\n", name);
    }
    return 0;
}

static int cmd_close(int argc, char **argv, task_t *task)
{
    // FIXME: This is an error dunno why the first arg is close
    int skip = create_flag(argc, argv, int, "id", "HUID of the task Issue");
    UNUSED(skip);
    char *huid = create_flag(argc, argv, char*, "id", "HUID of the task Issue");
    return close_task(task, huid);
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *cmd = argv[1];
    static task_t t = {0};

    if (strcmp(cmd, "add")   == 0)  { if (cmd_add(argc, argv, &t) < 0) return -1; }
    else if (strcmp(cmd, "list")  == 0)  { if (cmd_ls(argc, argv, &t) < 0) return -1; }
    else if (strcmp(cmd, "close") == 0) { if (cmd_close(argc, argv, &t) < 0) return -1; }
    else { usage(argv[0]); return -1; }

    return 0;
}
