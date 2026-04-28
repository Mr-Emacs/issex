#define _POSIX_C_SOURCE 200809L
#define NOB_NO_ECHO
#define NOB_IMPLEMENTATION
#include "nob.h"

#define QUERY_IMPLEMENTATION
#include "query.h"

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

#define HT_IMPLEMENTATION
#include "ht.h"

#define notes_dir "notes"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct header_t header_t;
typedef struct task_t task_t;
typedef struct task_meta_t task_meta_t;

#if defined(_WIN32)
static char *strndup_win(const char *s, size_t n)
{
    char *data = malloc(n + 1);
    if (data) {
        memcpy(data, s, n);
        data[n] = '\0';
    }
    return data;
}
#define strndup strndup_win
#else
#define strndup strndup
#endif

struct header_t {
    const char *title;
    bool status;
    size_t priority;
    char *notes;
    char *huid;
};

struct task_t {
    char *path;
    char root_path[PATH_MAX + sizeof("/" notes_dir)];
    time_t time;
    const char *task_name;
    char *current_path;
    header_t header;
    String_Views list;
    String_Views tags;
};

struct task_meta_t {
    char *huid;
    char *notes;
    char *path;
    char *title;
    bool status;
    size_t priority;
    time_t time;
    String_Views tags;
};

static int find_root(task_t *task);
static int create_task_dir(task_t *task);
static int create_task_md(task_t *task);
static int is_directory(const char *path);
static task_meta_t *task_parse(const char *path);
static bool evaluate_query(node_t *node, task_meta_t *task);
static void free_task_metadata(task_meta_t *task);

static void strip_whitespace(char *str);

static void strip_whitespace(char *str)
{
    for (; *str; ++str) {
        if (*str == ' ') *str = '_';
    }
}

static int find_note_line_num(const char *file_path)
{
    String_Builder sb = {0};

    int line_num = 1;
    if (!read_entire_file(file_path, &sb)) return -1;

    String_View content = sb_to_sv(sb);
    while(content.count > 0) {
        String_View line = sv_chop_by_delim(&content, '\n');
        line = sv_trim(line);
        if (sv_starts_with(line, sv_from_cstr("## NOTES:"))) {
            nob_sb_free(sb);
            return line_num+1;
        }
        line_num++;
    }
    nob_sb_free(sb);
    return 1;
}

static task_meta_t *task_parse(const char *path)
{
    char *notes = nob_temp_sprintf("%s/notes.md", path);
    String_Builder sb = {0};

    if (!read_entire_file(notes, &sb)) return NULL;

    task_meta_t *meta = calloc(1, sizeof(*meta));
    if (!meta) return NULL;

    meta->path = strdup(path);
    meta->huid = strdup(nob_path_name(meta->path));
    meta->priority = 0;
    meta->status = false;
    meta->title = NULL;

    String_View content = sb_to_sv(sb);

    while(content.count > 0) {
        String_View line = sv_chop_by_delim(&content, '\n');
        line = sv_trim(line);

        if (sv_starts_with(line, sv_from_cstr("# "))) {
            sv_chop_left(&line, 2);
            line = sv_trim(line);
            meta->title = strndup(line.data, line.count);
        }
        else if (sv_starts_with(line, sv_from_cstr("## PRIORITY"))) {
            String_View temp = line;
            String_View prefix = sv_chop_by_delim(&temp, ':');
            if (sv_starts_with(prefix, sv_from_cstr("## PRIORITY"))) {
                String_View priority = sv_trim(temp);
                if (priority.count > 0) {
                    char *priority_str = strndup(priority.data, priority.count);
                    meta->priority = atoi(priority_str);
                }
            }
        }
        else if (sv_starts_with(line, sv_from_cstr("## TAGS"))) {
            String_View temp = line;
            sv_chop_by_delim(&temp, ':');
            String_View tag_list = sv_trim(temp);
            while (tag_list.count > 0) {
                String_View tag = sv_trim(sv_chop_by_delim(&tag_list, ','));
                if (tag.count > 0) {
                    da_append(&meta->tags, tag);
                }
            }
        }
        else if (sv_starts_with(line, sv_from_cstr("## STATUS: "))) {
            sv_chop_left(&line, 11);
            line = sv_trim(line);
            meta->status = sv_eq(line, sv_from_cstr("OPEN"));
        }
    }

    nob_sb_free(sb);
    return meta;
}

static bool evaluate_query(node_t *query, task_meta_t *task)
{
    if (!query) return false;
   switch(query->as) {
        case NODE_TAG: {
            return sv_eq(query->tag, sv_from_cstr(task->huid)) ||
                   (task->title && sv_eq(query->tag, sv_from_cstr(task->title)));
        }
        case NODE_SUB_TAG: {
            for (size_t qi = 0; qi < query->sub_tag.count; qi++) {
                for (size_t ti = 0; ti < task->tags.count; ti++) {
                    if (sv_eq(query->sub_tag.items[qi], task->tags.items[ti])) return true;
                }
            }
            return false;
        }
        case NODE_PRIORITY: {
            return task->priority == query->prio.value;
        }
        case NODE_STATUS: {
            return  task->status == query->stat.status;
        }
        case NODE_GREATER: {
            return task->priority > query->prio.value;
        }
        case NODE_LESSER: {
            return task->priority < query->prio.value;
        }
        case NODE_NOT: {
            return !evaluate_query(query->unary.child, task);
        }
        case NODE_AND: {
            return evaluate_query(query->binary.lhs, task) &&
                   evaluate_query(query->binary.rhs, task);
        }
        case NODE_OR: {
            return evaluate_query(query->binary.lhs, task) ||
                   evaluate_query(query->binary.rhs, task);
        }
        case NODE_COUNT: default: return true;
    }
}

static void free_task_metadata(task_meta_t *task)
{
    if (task) {
        free(task->huid);
        free(task->path);
        free(task->title);
        free(task->tags.items);
        free(task);
    }
}

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
        snprintf(task->root_path, sizeof(task->root_path), "%s/" notes_dir, cwd);
        if (!nob_mkdir_if_not_exists(task->root_path)) return -1;
    }

    task->time = time(NULL);
    struct tm *now = localtime(&task->time);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y%m%d", now);
    char *buf = temp_sprintf("%s_%s_%02d%02d%02d", task->task_name, buffer,
                             now->tm_hour, now->tm_min, now->tm_sec);

    char full_path[PATH_MAX * 2];
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
    sb_append_cstr(&sb, "## TAGS: ");
    for (size_t i = 0; i < task->tags.count; i++) {
        if (i > 0) sb_append_cstr(&sb, ", ");
        sb_appendf(&sb, SV_Fmt, SV_Arg(task->tags.items[i]));
    }
    sb_append_cstr(&sb, "\n\n");
    task->header.status = true;
    sb_appendf(&sb, "## NOTES: \n\n%s\n", task->header.notes);

    char notes_path[PATH_MAX];
    snprintf(notes_path, sizeof(notes_path), "%s/notes.md", task->current_path);

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

        if (stat(notes_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(task->root_path, sizeof(task->root_path), "%s/"notes_dir, path);
            chdir(old_dir);
            return 0;
        }

        if (strcmp(path, "/") == 0) goto fail;
        if (chdir("..") != 0) goto fail;
        #else
        DWORD dir = GetFileAttributesA(notes_dir);
        if (dir != INVALID_FILE_ATTRIBUTES && (dir & FILE_ATTRIBUTE_DIRECTORY)) {
            snprintf(task->root_path, sizeof(task->root_path), "%s/"notes_dir, path);
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
            const char *notes_path = temp_sprintf("%s/notes.md",
                                     temp_sv_to_cstr(task->list.items[i]));
            if (!nob_read_entire_file(notes_path, &sb)) return -1;

            String_View content = nob_sb_to_sv(sb);
            String_Builder out = {0};
            while(content.count > 0) {
                String_View line = sv_chop_by_delim(&content, '\n');
                if (sv_starts_with(line , sv_from_cstr("## STATUS: "))) {
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
    printf("  add    -name <name> [-priority <level>] [-notes <text>]\n");
    printf("  list   [-src <query>]\n");
    printf("  close  -id <huid>\n\n");
    printf("Query syntax (for -src):\n");
    printf("  p=<n>          priority equals n\n");
    printf("  p><n>          priority greater than n\n");
    printf("  p<<n>          priority less than n\n");
    printf("  s=1            status OPEN\n");
    printf("  s=0            status CLOSED\n");
    printf("  t=<tag>        task has tag\n");
    printf("  <expr> & <expr>  AND\n");
    printf("  <expr> | <expr>  OR\n");
    printf("  !<expr>          NOT\n\n");
    printf("Examples:\n");
    printf("  %s add -name \"Fix bug\" -priority 2 -notes \"Segfault in parser\"\n", program);
    printf("  %s list\n", program);
    printf("  %s list -src \"p>1 & s=1\"\n", program);
    printf("  %s close -id <huid>\n", program);
}

static void warn_unconsumed_args(int argc, char **argv, int skip, const char *hint)
{
    if (!_arg_used) return;
    for (int i = skip; i < argc; i++) {
        if (!_arg_used[i]) {
            nob_log(NOB_WARNING, "unexpected argument '%s' -- did you forget a flag? (e.g. %s)", argv[i], hint);
        }
    }
}

static int cmd_add(int argc, char **argv, task_t *task)
{
    char *name   = create_flag(argc, argv, char*, "name", "Name of the issue");
    int priority = create_flag(argc, argv, int, "priority", "Priority of the issue");
    char *notes  = create_flag(argc, argv, char*, "notes", "Notes for the current issue");
    char *tags   = create_flag(argc, argv, char*, "tags", "Comma-separated tags for the issue");
    warn_unconsumed_args(argc, argv, 2, "-name, -priority, -notes, -tags");
    if (!name) {
        nob_log(NOB_ERROR, "add: missing -name flag");
        fprintf(stderr, "  usage: add -name <n> [-priority <level>] [-notes <text>] [-tags <t1,t2>]\n");
        return -1;
    }

    strip_whitespace(name);

    task->task_name = name;
    task->header.notes = notes;
    task->header.priority = (size_t)priority;

    if (tags) {
        String_View tag_list = sv_from_cstr(tags);
        while (tag_list.count > 0) {
            String_View tag = sv_trim(sv_chop_by_delim(&tag_list, ','));
            if (tag.count > 0) da_append(&task->tags, tag);
        }
    }

    return create_task_md(task);
}

static int cmd_ls(int argc, char **argv, task_t *task)
{
    char *src = create_flag(argc, argv, char*, "src", "Source Code for the query System");
    warn_unconsumed_args(argc, argv, 2, "-src");
    node_t *query_tree = NULL;

    if (src && src[0] != '\0') {
        lexer_t lexer = {
            .source = sv_from_cstr(src),
            .position = 0,
        };

        parser_t parser = { .lex = &lexer };
        parser_advance(&parser);
        query_tree = parse_expr(&parser);
        if (!query_tree) {
            nob_log(NOB_ERROR, "Failed to parse query '%s'\n", src);
            return -1;
        }
        if (parser.cur.as != TEOF) {
            parse_error_at(&parser, parser.cur, "use '&' for AND or '|' for OR");
            return -1;
        }
    }
    if (list_tasks(task) < 0) return -1;

    typedef Ht(const char *, task_meta_t *) Task_Ht;
    Task_Ht task_map = {
        .hasheq = ht_cstr_hasheq,
    };

    for (size_t i = 0; i < task->list.count; ++i) {
        const char *full_path = temp_sv_to_cstr(task->list.items[i]);
        task_meta_t *meta = task_parse(full_path);
        if (!meta) {
            nob_log(NOB_ERROR, "Failed to parse the task '%s'", nob_path_name(full_path));
            continue;
        }
        bool matches = (query_tree == NULL) || evaluate_query(query_tree, meta);

        if (matches) {
            *ht_put(&task_map, meta->huid) = meta;
            const char *status = meta->status ? "OPEN" : "CLOSED";

            #ifdef EMACS_PRINT
            const char *notes_path = temp_sprintf("%s/notes.md", full_path);
            int notes_line_num = find_note_line_num(notes_path);
            printf("%s:%d: [%s] P:%zu - %s (HUID: %s)\n",
                   notes_path, notes_line_num, status, meta->priority,
                   meta->title ? meta->title : "NoName",
                   meta->huid
            );
            #else
            printf("(HUID: %s) [Title: %s] |Status:%s| Priority:%zu\n",
                   meta->huid,
                   meta->title ? meta->title : "NoName",
                   status, meta->priority
            );
            #endif
        } else {
            free_task_metadata(meta);
        }
    }

    ht_foreach(value, &task_map) {
        free_task_metadata(*value);
    }

    ht_free(&task_map);
    if (query_tree) {
        free(query_tree);
    }
    return 0;
}

static int cmd_close(int argc, char **argv, task_t *task)
{
    char *huid = create_flag(argc, argv, char*, "id", "HUID of the task Issue");
    warn_unconsumed_args(argc, argv, 2, "-id");
    if (!huid) {
        nob_log(NOB_ERROR, "close: missing -id flag");
        fprintf(stderr, "  usage: close -id <huid>\n");
        return -1;
    }
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
    else {
        nob_log(NOB_ERROR, "unknown command '%s'", cmd);
        usage(argv[0]);
        return -1;
    }

    return 0;
}
