/* packages.c -- see packages.h.
 *
 * WHY THERE IS NO COMPILE STEP
 *
 * It is tempting to let users author isolated package folders and then "compile"
 * them into the single shared tree the loader used to read. Nothing requires
 * that. DOOM never sees this directory layout: the decl server derives a decl's
 * type and logical name from its path relative to a decls root, and the resource
 * bridge and requirements reader each glob one subdirectory. All three take a
 * root and append a fixed suffix, so supporting many packages is reading N roots
 * instead of one -- no staging, no generated copies to go stale, no bookkeeping
 * about which package wrote which file, and deleting a folder really does
 * uninstall it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "packages.h"

#define PK_OVERRIDES_SUFFIX "\\overrides"
#define PK_MARKER           "package.json"
/* The pre-package shared tree, still read so existing installs keep working. */
#define PK_LEGACY_NAME      "generated"

typedef HANDLE (WINAPI *pk_find_first_fn)(LPCSTR, LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *pk_find_next_fn)(HANDLE, LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *pk_find_close_fn)(HANDLE);
typedef DWORD (WINAPI *pk_get_attributes_fn)(LPCSTR);

static pk_find_first_fn g_find_first = FindFirstFileA;
static pk_find_next_fn g_find_next = FindNextFileA;
static pk_find_close_fn g_find_close = FindClose;
static pk_get_attributes_fn g_get_attributes = GetFileAttributesA;

int sh_package_subdir(const sh_package *package, const char *subdirectory,
                      char *out, size_t out_size)
{
    if (!package || !subdirectory || !out || out_size == 0) return 0;
    return _snprintf_s(out, out_size, _TRUNCATE, "%s\\%s",
                       package->root, subdirectory) >= 0;
}

static int pk_is_directory(const char *path)
{
    DWORD attributes = g_get_attributes(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static int pk_is_file(const char *path)
{
    DWORD attributes = g_get_attributes(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static int pk_append(sh_package *out, size_t capacity, size_t *count,
                     const char *name, const char *root)
{
    size_t i;
    if (*count >= capacity) return 0;
    for (i = 0; i < *count; i++)
        if (_stricmp(out[i].name, name) == 0) return 1;   /* already present */
    if (strcpy_s(out[*count].name, sizeof(out[*count].name), name) != 0 ||
        strcpy_s(out[*count].root, sizeof(out[*count].root), root) != 0)
        return 0;
    (*count)++;
    return 1;
}

static void pk_sort(sh_package *out, size_t count)
{
    size_t i, j;
    for (i = 1; i < count; i++) {
        sh_package key = out[i];
        j = i;
        while (j > 0 && _stricmp(out[j - 1].name, key.name) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
}

int sh_packages_enumerate(const char *data_root, sh_package *out, size_t capacity,
                          size_t *count)
{
    char overrides[MAX_PATH];
    char pattern[MAX_PATH];
    char candidate[MAX_PATH];
    char marker[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    int complete = 1;

    if (count) *count = 0;
    if (!data_root || !data_root[0] || !out || capacity == 0 || !count) return 0;
    if (_snprintf_s(overrides, sizeof(overrides), _TRUNCATE, "%s%s",
                    data_root, PK_OVERRIDES_SUFFIX) < 0) return 0;
    if (!pk_is_directory(overrides)) return 1;   /* nothing installed yet */

    if (_snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s\\*", overrides) < 0)
        return 0;
    search = g_find_first(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES;
    }
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (strcmp(found.cFileName, ".") == 0 || strcmp(found.cFileName, "..") == 0)
            continue;
        if (_snprintf_s(candidate, sizeof(candidate), _TRUNCATE, "%s\\%s",
                        overrides, found.cFileName) < 0) { complete = 0; continue; }
        /* The legacy shared tree has no package.json but is still a package. */
        if (_stricmp(found.cFileName, PK_LEGACY_NAME) != 0) {
            if (_snprintf_s(marker, sizeof(marker), _TRUNCATE, "%s\\%s",
                            candidate, PK_MARKER) < 0) { complete = 0; continue; }
            if (!pk_is_file(marker)) continue;
        }
        if (!pk_append(out, capacity, count, found.cFileName, candidate))
            complete = 0;
    } while (g_find_next(search, &found));
    g_find_close(search);

    pk_sort(out, *count);
    return complete;
}

#ifdef SH_PACKAGES_TESTING
void sh_packages_test_set_api(const sh_packages_test_find_api *api)
{
    if (!api) return;
    g_find_first = api->find_first;
    g_find_next = api->find_next;
    g_find_close = api->find_close;
    g_get_attributes = api->get_attributes;
}

void sh_packages_test_reset_api(void)
{
    g_find_first = FindFirstFileA;
    g_find_next = FindNextFileA;
    g_find_close = FindClose;
    g_get_attributes = GetFileAttributesA;
}
#endif
