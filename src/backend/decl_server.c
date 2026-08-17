/* decl_server.c -- see decl_server.h.
 *
 * The ordinary overrides layer is demand-driven: it can replace bytes only
 * after DOOM asks for an already-registered source path. This service handles
 * the complementary case. At startup it snapshots user files under
 * overrides/generated/decls, derives each decl's type and logical name from
 * its path, then queues one private command. DOOM drains that command on its
 * main thread, where we use the engine's own decl-registry virtual methods:
 *
 *   +0x58  Find decl type by short name
 *   +0x70  Add a decl from (logical name, source name, text)
 *
 * An existing logical identity is never replaced here; it remains a normal
 * file-shadow override. Only a missing identity is registered. The snapshot
 * is immutable for the process lifetime and the command is one-shot: there is
 * no watcher, hot reload, retry, or unload path.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend_log.h"
#include "decl_server.h"
#include "decl_server_path.h"
#include "decl_text.h"
#include "overrides.h"
#include "user_overrides.h"

#define DS_ROOT_SUFFIX          "\\overrides\\generated\\decls"
#define DS_INTERNAL_COMMAND     "snapmap_plus_decl_server_apply"
#define DS_MAX_CANDIDATES       512
#define DS_MAX_DEPTH            16
#define DS_MAX_FILE_BYTES       (1024u * 1024u)
#define DS_MAX_TOTAL_BYTES      (16u * 1024u * 1024u)
#define DS_REGISTRY_TYPE_SLOT   0x58u
#define DS_REGISTRY_ADD_SLOT    0x70u
#define DS_ANCHOR_MOV_OFFSET    0x10u
#define DS_ANCHOR_MOV_LENGTH    7u

enum {
    DS_STATE_NEW = 0,
    DS_STATE_INSTALLING,
    DS_STATE_QUEUED,
    DS_STATE_APPLYING,
    DS_STATE_DONE,
    DS_STATE_FAILED
};

typedef void (*add_command_fn)(void *cmdsys, const char *name, void *handler,
                               const char *help, void *arg_comp, unsigned int flags);
typedef void (*buffer_command_fn)(void *cmdsys, const char *text);
typedef void *(*decl_type_by_name_fn)(void *registry, const char *short_name);
typedef void (*decl_add_from_text_fn)(void *registry, void *type_manager,
                                     const char *logical_name, const char *source_name,
                                     const char *body_text);
typedef void *(*decl_find_fn)(void *type_manager, const char *logical_name,
                              unsigned char make_default);

typedef struct ds_candidate {
    char type[SH_DECL_SERVER_TYPE_CAP];
    char name[SH_DECL_SERVER_NAME_CAP];
    char source[SH_DECL_SERVER_SOURCE_CAP];
    char *body;
    size_t body_length;
    int duplicate;
} ds_candidate;

static volatile LONG g_state = DS_STATE_NEW;
static ds_candidate *g_candidates;
static int g_candidate_count;
static int g_capture_refused;
static size_t g_total_bytes;
static void *g_cmdsys;
static add_command_fn g_add_command;
static buffer_command_fn g_buffer_command;
static const uint8_t *g_registry_anchor;
static decl_type_by_name_fn g_expected_type_by_name;
static decl_add_from_text_fn g_expected_add_from_text;
static decl_find_fn g_find_decl;

static void ds_log(const char *status, const char *subject, const char *reason)
{
    char line[512];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "decl-server %s: '%s'%s%s",
                status ? status : "INFO", subject ? subject : "",
                reason && reason[0] ? " -- " : "", reason ? reason : "");
    backend_log(line);
}

static int ds_safe_read(const void *source, void *destination, size_t length)
{
    __try {
        memcpy(destination, source, length);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static const sig_result *ds_result(const sig_result *results, size_t count, const char *name)
{
    size_t i;
    if (!results || !name) return NULL;
    for (i = 0; i < count; i++)
        if (results[i].name && strcmp(results[i].name, name) == 0) return &results[i];
    return NULL;
}

static void ds_free_candidates(void)
{
    int i;
    if (!g_candidates) return;
    for (i = 0; i < g_candidate_count; i++) {
        if (g_candidates[i].body) HeapFree(GetProcessHeap(), 0, g_candidates[i].body);
        g_candidates[i].body = NULL;
    }
    HeapFree(GetProcessHeap(), 0, g_candidates);
    g_candidates = NULL;
    g_candidate_count = 0;
    g_total_bytes = 0;
}

static int ds_compare_candidates(const void *left, const void *right)
{
    const ds_candidate *a = (const ds_candidate *)left;
    const ds_candidate *b = (const ds_candidate *)right;
    int result = _stricmp(a->type, b->type);
    if (!result) result = _stricmp(a->name, b->name);
    if (!result) result = _stricmp(a->source, b->source);
    if (!result) result = strcmp(a->source, b->source);
    return result;
}

static int ds_same_identity(const ds_candidate *a, const ds_candidate *b)
{
    return _stricmp(a->type, b->type) == 0 && _stricmp(a->name, b->name) == 0;
}

static char *ds_read_file(const char *path, size_t *out_length, const char **reason)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    DWORD got = 0;
    char *body;

    *out_length = 0;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
                       FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *reason = "open failed";
        return NULL;
    }
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(file);
        *reason = "directory or reparse-point file refused";
        return NULL;
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > (LONGLONG)DS_MAX_FILE_BYTES) {
        CloseHandle(file);
        *reason = "file is empty or exceeds the 1 MiB cap";
        return NULL;
    }
    if (g_total_bytes + (size_t)size.QuadPart > DS_MAX_TOTAL_BYTES) {
        CloseHandle(file);
        *reason = "launch snapshot exceeds the 16 MiB total cap";
        return NULL;
    }
    body = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)size.QuadPart + 1);
    if (!body) {
        CloseHandle(file);
        *reason = "allocation failed";
        return NULL;
    }
    if (!ReadFile(file, body, (DWORD)size.QuadPart, &got, NULL) || got != (DWORD)size.QuadPart) {
        HeapFree(GetProcessHeap(), 0, body);
        CloseHandle(file);
        *reason = "short read";
        return NULL;
    }
    CloseHandle(file);
    *out_length = (size_t)size.QuadPart;

    /* The engine lexer expects text, not a transport marker. Normalize a UTF-8
     * BOM at the boundary; all other bytes remain exact. */
    if (*out_length >= 3 && (unsigned char)body[0] == 0xef &&
        (unsigned char)body[1] == 0xbb && (unsigned char)body[2] == 0xbf) {
        memmove(body, body + 3, *out_length - 3);
        *out_length -= 3;
    }
    body[*out_length] = '\0';
    if (!sh_decl_text_well_formed((const unsigned char *)body, *out_length)) {
        HeapFree(GetProcessHeap(), 0, body);
        *reason = "structural validation failed (NUL, braces, quote, or comment)";
        return NULL;
    }
    return body;
}

static void ds_capture_one(const char *absolute_path, const char *relative_path)
{
    ds_candidate *candidate;
    const char *reason = NULL;
    char type[SH_DECL_SERVER_TYPE_CAP];
    char name[SH_DECL_SERVER_NAME_CAP];
    char source[SH_DECL_SERVER_SOURCE_CAP];
    char *body;
    size_t body_length;

    if (!sh_decl_server_identity_from_relative(relative_path,
                                               type, sizeof(type), name, sizeof(name),
                                               source, sizeof(source), &reason)) {
        ds_log("REFUSED", relative_path, reason);
        g_capture_refused++;
        return;
    }
    if (g_candidate_count >= DS_MAX_CANDIDATES) {
        ds_log("REFUSED", source, "launch snapshot exceeds the 512-decl cap");
        g_capture_refused++;
        return;
    }
    body = ds_read_file(absolute_path, &body_length, &reason);
    if (!body) {
        ds_log("REFUSED", source, reason);
        g_capture_refused++;
        return;
    }
    candidate = &g_candidates[g_candidate_count++];
    strcpy_s(candidate->type, sizeof(candidate->type), type);
    strcpy_s(candidate->name, sizeof(candidate->name), name);
    strcpy_s(candidate->source, sizeof(candidate->source), source);
    candidate->body = body;
    candidate->body_length = body_length;
    g_total_bytes += body_length;
}

static void ds_walk(const char *directory, const char *relative, int depth)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;

    if (depth > DS_MAX_DEPTH) {
        ds_log("REFUSED", relative, "directory nesting exceeds 16 levels");
        g_capture_refused++;
        return;
    }
    if (_snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s\\*", directory) < 0) {
        ds_log("REFUSED", relative, "directory path exceeds MAX_PATH");
        g_capture_refused++;
        return;
    }
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        char child[MAX_PATH];
        char child_relative[MAX_PATH];
        if (strcmp(found.cFileName, ".") == 0 || strcmp(found.cFileName, "..") == 0) continue;
        if (_snprintf_s(child, sizeof(child), _TRUNCATE, "%s\\%s", directory, found.cFileName) < 0 ||
            _snprintf_s(child_relative, sizeof(child_relative), _TRUNCATE, "%s%s%s",
                        relative, relative[0] ? "\\" : "", found.cFileName) < 0) {
            ds_log("REFUSED", found.cFileName, "path exceeds MAX_PATH");
            g_capture_refused++;
            continue;
        }
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ds_log("REFUSED", child_relative, "reparse-point directory");
                g_capture_refused++;
            } else {
                ds_walk(child, child_relative, depth + 1);
            }
        } else if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            ds_log("REFUSED", child_relative, "reparse-point file");
            g_capture_refused++;
        } else {
            ds_capture_one(child, child_relative);
        }
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static int ds_capture_snapshot(void)
{
    char root[MAX_PATH];
    char directory[MAX_PATH];
    DWORD attributes;
    int i;

    if (!sh_overrides_get_root(root, sizeof(root)) ||
        _snprintf_s(directory, sizeof(directory), _TRUNCATE, "%s%s", root, DS_ROOT_SUFFIX) < 0) {
        backend_log("decl-server REFUSED: could not resolve overrides/generated/decls root");
        g_capture_refused++;
        return 0;
    }
    attributes = GetFileAttributesA(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        char line[MAX_PATH + 96];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "decl-server idle: no registration directory at %s", directory);
        backend_log(line);
        return 1;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        backend_log("decl-server REFUSED: registration root is not a regular directory");
        g_capture_refused++;
        return 0;
    }
    g_candidates = (ds_candidate *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                             sizeof(ds_candidate) * DS_MAX_CANDIDATES);
    if (!g_candidates) {
        backend_log("decl-server REFUSED: candidate-table allocation failed");
        g_capture_refused++;
        return 0;
    }
    ds_walk(directory, "", 0);
    if (g_candidate_count == 0) return 1;

    qsort(g_candidates, (size_t)g_candidate_count, sizeof(g_candidates[0]), ds_compare_candidates);
    for (i = 0; i < g_candidate_count;) {
        int end = i + 1;
        while (end < g_candidate_count && ds_same_identity(&g_candidates[i], &g_candidates[end])) end++;
        if (end - i > 1) {
            int j;
            for (j = i; j < end; j++) {
                g_candidates[j].duplicate = 1;
                ds_log("REFUSED", g_candidates[j].source,
                       "case-insensitive duplicate type/name identity");
                g_capture_refused++;
            }
        }
        i = end;
    }
    return 1;
}

static int ds_decode_registry(void **out_registry,
                              decl_type_by_name_fn *out_type_by_name,
                              decl_add_from_text_fn *out_add_from_text)
{
    uint8_t instruction[DS_ANCHOR_MOV_LENGTH];
    int32_t displacement;
    const uint8_t *slot;
    void *registry = NULL;
    void **vtable = NULL;
    void *type_method = NULL;
    void *add_method = NULL;

    if (!g_registry_anchor ||
        !ds_safe_read(g_registry_anchor + DS_ANCHOR_MOV_OFFSET, instruction, sizeof(instruction)) ||
        instruction[0] != 0x48 || instruction[1] != 0x8b || instruction[2] != 0x0d) return 0;
    memcpy(&displacement, instruction + 3, sizeof(displacement));
    slot = (const uint8_t *)((uintptr_t)g_registry_anchor + DS_ANCHOR_MOV_OFFSET +
                            DS_ANCHOR_MOV_LENGTH + (intptr_t)displacement);
    if (!ds_safe_read(slot, &registry, sizeof(registry)) || !registry ||
        !ds_safe_read(registry, &vtable, sizeof(vtable)) || !vtable ||
        !ds_safe_read((const uint8_t *)vtable + DS_REGISTRY_TYPE_SLOT,
                      &type_method, sizeof(type_method)) ||
        !ds_safe_read((const uint8_t *)vtable + DS_REGISTRY_ADD_SLOT,
                      &add_method, sizeof(add_method))) return 0;
    if (type_method != (void *)g_expected_type_by_name ||
        add_method != (void *)g_expected_add_from_text) return 0;
    *out_registry = registry;
    *out_type_by_name = (decl_type_by_name_fn)type_method;
    *out_add_from_text = (decl_add_from_text_fn)add_method;
    return 1;
}

static void ds_refuse_remaining(int start, const char *reason, int *refused)
{
    int i;
    for (i = start; i < g_candidate_count; i++) {
        if (g_candidates[i].duplicate) continue;
        ds_log("REFUSED", g_candidates[i].source, reason);
        (*refused)++;
    }
}

static void __cdecl ds_apply_command(void)
{
    void *registry = NULL;
    decl_type_by_name_fn type_by_name = NULL;
    decl_add_from_text_fn add_from_text = NULL;
    int registered = 0, shadowed = 0, refused = g_capture_refused;
    int i;

    if (InterlockedCompareExchange(&g_state, DS_STATE_APPLYING, DS_STATE_QUEUED) != DS_STATE_QUEUED)
        return;
    if (!ds_decode_registry(&registry, &type_by_name, &add_from_text)) {
        ds_refuse_remaining(0, "decl registry/vtable validation failed", &refused);
        backend_log("decl-server FAILED: native decl registry was unavailable or did not match resolved methods");
        InterlockedExchange(&g_state, DS_STATE_FAILED);
        ds_free_candidates();
        return;
    }

    for (i = 0; i < g_candidate_count; i++) {
        ds_candidate *candidate = &g_candidates[i];
        void *type_manager = NULL;
        void *existing = NULL;
        void *published = NULL;
        int fault = 0;

        if (candidate->duplicate) continue;
        __try {
            type_manager = type_by_name(registry, candidate->type);
            if (type_manager) existing = g_find_decl(type_manager, candidate->name, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            fault = 1;
        }
        if (fault) {
            ds_log("REFUSED", candidate->source, "engine exception during type/existence lookup");
            refused++;
            ds_refuse_remaining(i + 1, "not attempted after engine exception", &refused);
            break;
        }
        if (!type_manager) {
            ds_log("REFUSED", candidate->source, "unknown or unsupported decl type directory");
            refused++;
            continue;
        }
        if (existing) {
            ds_log("SHADOWED", candidate->source,
                   "identity already registered; ordinary file-shadow remains authoritative");
            shadowed++;
            continue;
        }

        __try {
            add_from_text(registry, type_manager, candidate->name,
                          candidate->source, candidate->body);
            published = g_find_decl(type_manager, candidate->name, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            fault = 1;
        }
        if (fault) {
            ds_log("REFUSED", candidate->source, "engine exception during native registration");
            refused++;
            ds_refuse_remaining(i + 1, "not attempted after engine exception", &refused);
            break;
        }
        if (!published) {
            ds_log("REFUSED", candidate->source, "engine did not publish the requested identity");
            refused++;
            continue;
        }
        ds_log("REGISTERED", candidate->source, candidate->name);
        registered++;
    }

    {
        char line[256];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "decl-server complete: %d REGISTERED, %d SHADOWED, %d REFUSED; restart-only snapshot (%zu bytes)",
                    registered, shadowed, refused, g_total_bytes);
        backend_log(line);
    }
    InterlockedExchange(&g_state, DS_STATE_DONE);
    ds_free_candidates();
}

int sh_decl_server_install(const sig_result *results, size_t count,
                           const uint8_t *module_base, void *cmdsys)
{
    const sig_result *anchor;
    const sig_result *type_method;
    const sig_result *add_method;
    const sig_result *find_decl;
    uintptr_t add_command;
    uintptr_t buffer_command;
    int command_registered = 0;
    int queued = 0;

    (void)module_base;
    if (InterlockedCompareExchange(&g_state, DS_STATE_INSTALLING, DS_STATE_NEW) != DS_STATE_NEW)
        return 0;
    if (!sh_user_overrides_enabled_for_launch()) {
        backend_log("decl-server disabled for this launch with the user override layer");
        InterlockedExchange(&g_state, DS_STATE_DONE);
        return 1;
    }
    if (!ds_capture_snapshot()) {
        InterlockedExchange(&g_state, DS_STATE_FAILED);
        ds_free_candidates();
        return 0;
    }
    if (g_candidate_count == 0) {
        InterlockedExchange(&g_state, DS_STATE_DONE);
        ds_free_candidates();
        return 1;
    }

    anchor = ds_result(results, count, "DeclRegistryAnchor");
    type_method = ds_result(results, count, "DeclTypeByName");
    add_method = ds_result(results, count, "DeclAddFromText");
    find_decl = ds_result(results, count, "DeclFind");
    add_command = sig_addr_by_name(results, count, "AddCommand");
    buffer_command = sig_addr_by_name(results, count, "BufferCommandText");
    if (!anchor || anchor->status != SIG_OK || !type_method || !add_method || !find_decl ||
        !type_method->addr || !add_method->addr || !find_decl->addr ||
        !add_command || !buffer_command || !cmdsys) {
        backend_log("decl-server REFUSED: signature, command-system, or clean registry anchor dependency missing");
        InterlockedExchange(&g_state, DS_STATE_FAILED);
        ds_free_candidates();
        return 0;
    }

    g_cmdsys = cmdsys;
    g_add_command = (add_command_fn)add_command;
    g_buffer_command = (buffer_command_fn)buffer_command;
    g_registry_anchor = (const uint8_t *)anchor->addr;
    g_expected_type_by_name = (decl_type_by_name_fn)type_method->addr;
    g_expected_add_from_text = (decl_add_from_text_fn)add_method->addr;
    g_find_decl = (decl_find_fn)find_decl->addr;

    __try {
        g_add_command(g_cmdsys, DS_INTERNAL_COMMAND, (void *)ds_apply_command,
                      "Snapmap+ internal one-shot decl registration", NULL, 2u);
        command_registered = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        command_registered = 0;
    }
    if (!command_registered) {
        backend_log("decl-server REFUSED: internal main-thread command registration failed");
        InterlockedExchange(&g_state, DS_STATE_FAILED);
        ds_free_candidates();
        return 0;
    }

    InterlockedExchange(&g_state, DS_STATE_QUEUED);
    __try {
        g_buffer_command(g_cmdsys, DS_INTERNAL_COMMAND "\n");
        queued = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        queued = 0;
    }
    if (!queued) {
        backend_log("decl-server REFUSED: main-thread command enqueue failed");
        InterlockedExchange(&g_state, DS_STATE_FAILED);
        ds_free_candidates();
        return 0;
    }
    {
        char line[224];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "decl-server queued: immutable launch snapshot has %d candidate(s), %zu bytes; no hot reload",
                    g_candidate_count, g_total_bytes);
        backend_log(line);
    }
    return 1;
}
