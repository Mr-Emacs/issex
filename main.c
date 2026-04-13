#define NOB_NO_ECHO
#define NOB_IMPLEMENTATION
#include "nob.h"
#include "flag.h"

#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <direct.h>
#else
#include <libgen.h>
#include <limits.h>
#include <sys/types.h>
#endif

#define NOTES "notes"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct header_t header_t;
typedef struct task_t task_t;
typedef struct String_Views String_Views;

struct header_t {
    const char *title;
    bool status;
    size_t priority;
    char *notes;
    char *huid;
};

struct String_Views {
    String_View *items;
    size_t count;
    size_t capacity;
};

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

#ifndef _WIN32
static int is_directory(const char *path)
{
    struct stat st = {0};
    if (stat(path, &st) != 0) goto fail;
    if (!S_ISDIR(st.st_mode)) goto fail;

    return 0;

fail:
    return -1;
}
#else
static int is_directory(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return -1;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return -1;
    return 0;
}
#endif

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

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", task->root_path, buf);

    if (!nob_mkdir_if_not_exists(full_path)) return -1;

    size_t size = strlen(full_path) + 1;
    task->current_path = malloc(size);
    memcpy(task->current_path, full_path, size);

    size_t huid_size = strlen(buf) + 1;
    task->header.huid = malloc(huid_size);
    memcpy(task->header.huid, buf, huid_size);

    return 0;
}

static int create_task_md(task_t *task)
{
    if (create_task_dir(task) < 0) return -1;
    String_Builder sb = {0};
    task->header.title = task->task_name;
    sb_appendf(&sb, "# %s\n\n", task->task_name);
    sb_appendf(&sb, "- ID: %s\n\n", task->header.huid);
    sb_appendf(&sb, "## PRIORITY: %zu\n\n", task->header.priority);
    sb_append_cstr(&sb, "## STATUS: OPEN\n\n");
    task->header.status = true;
    sb_appendf(&sb, "## NOTES: \n\n%s\n", task->header.notes);

    char notes_path[PATH_MAX];
    snprintf(notes_path, sizeof(notes_path), "%s/NOTES.md", task->current_path);

    if (!nob_write_entire_file(notes_path, sb.items, sb.count)) return -1;
    nob_log(INFO, "Created issue with ID %s", task->header.huid);
    return 0;
}

static int find_root(task_t *task)
{
    char path[PATH_MAX];
    char old_dir[PATH_MAX];

    if (getcwd(old_dir, sizeof(old_dir)) == NULL) return -1;
    while(1) {
        if (getcwd(path, sizeof(path)) == NULL) goto fail;
        #ifndef _WIN32
        struct stat st = {0};

        if (stat(NOTES, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(task->root_path, sizeof(task->root_path), "%s/"NOTES, path);
            chdir(old_dir);
            return 0;
        }

        if (strcmp(path, "/") == 0) goto fail;
        if (chdir("..") != 0) goto fail;
        #else
        DWORD dir = GetFileAttributesA(NOTES);
        if (dir != INVALID_FILE_ATTRIBUTES && (dir & FILE_ATTRIBUTE_DIRECTORY)) {
            snprintf(task->root_path, sizeof(task->root_path), "%s/"NOTES, path);
            _chdir(old_dir);
            return 0;
        }

        // Check if we've reached the drive root (e.g. "C:\")
        if (strlen(path) <= 3 && path[1] == ':') goto fail;
        if (_chdir("..") != 0) goto fail;
        #endif
    }

fail:
    #ifdef _WIN32
    _chdir(old_dir);
    #else
    chdir(old_dir);
    #endif
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
        const char *full = temp_sv_to_cstr(task->list.items[i]);
#ifndef _WIN32
        char *base = strdup(full);
        char *name = basename(base);
        printf("%s\n", name);
        free(base);
#else
        char win_path[PATH_MAX];
        strncpy(win_path, full, PATH_MAX - 1);
        win_path[PATH_MAX - 1] = '\0';
        for (char *p = win_path; *p; p++) if (*p == '/') *p = '\\';

        char fname[_MAX_FNAME];
        char ext[_MAX_EXT];
        _splitpath_s(win_path, NULL, 0, NULL, 0, fname, _MAX_FNAME, ext, _MAX_EXT);
        printf("%s%s\n", fname, ext);
#endif
    }
    return 0;
}

static int cmd_close(int argc, char **argv, task_t *task)
{
    // FIXME: This is an error dunno why the first arg is close
    int skip = create_flag(argc, argv, int, NULL, NULL);
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
