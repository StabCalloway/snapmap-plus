/* packages.h -- one override package is one folder the user can drag in. */
#ifndef BACKEND_PACKAGES_H
#define BACKEND_PACKAGES_H

#include <windows.h>
#include <stddef.h>

#define SH_PACKAGES_MAX      64
#define SH_PACKAGE_NAME_CAP  96

/* An override package is a directory directly below
 * %LOCALAPPDATA%\snapmap-plus\overrides that contains a package.json, holding
 * its own decls, resources and requirements:
 *
 *   overrides\cyberdemon\package.json
 *   overrides\cyberdemon\decls\<type>\<logical-name>.decl
 *   overrides\cyberdemon\resources\<name>.manifest
 *   overrides\cyberdemon\requirements\<name>.requirements
 *
 * Installing is copying that folder in; uninstalling is deleting it. Nothing is
 * compiled, staged or merged anywhere, so a package cannot leave artefacts
 * behind and two packages cannot quietly overwrite each other's files on disk.
 *
 * The pre-package layout -- a single shared overrides\generated tree -- is still
 * read, reported as a package named "generated", so existing installs keep
 * working unchanged.
 *
 * Identity collisions BETWEEN packages are not resolved here. They are left to
 * the decl server's existing case-insensitive collision rule, which already
 * refuses every member of an ambiguous identity group; carrying the owning
 * package name is what lets it say which packages collided. */
typedef struct sh_package {
    char name[SH_PACKAGE_NAME_CAP];  /* folder name; the package's identity */
    char root[MAX_PATH];             /* absolute path to the package folder */
} sh_package;

/* Enumerate packages below `<data_root>\overrides`, in deterministic
 * case-insensitive name order so two machines see the same order. Directories
 * without a package.json are ignored, as are reparse points. Returns 1 on a
 * complete enumeration (including "none found"), 0 when the directory could not
 * be read; `*count` is always set. */
int sh_packages_enumerate(const char *data_root, sh_package *out, size_t capacity,
                          size_t *count);

/* Join `<package root>\<subdirectory>` into `out`. Returns 0 when it would not
 * fit. */
int sh_package_subdir(const sh_package *package, const char *subdirectory,
                      char *out, size_t out_size);

#ifdef SH_PACKAGES_TESTING
typedef struct sh_packages_test_find_api {
    HANDLE (WINAPI *find_first)(LPCSTR pattern, LPWIN32_FIND_DATAA found);
    BOOL (WINAPI *find_next)(HANDLE search, LPWIN32_FIND_DATAA found);
    BOOL (WINAPI *find_close)(HANDLE search);
    DWORD (WINAPI *get_attributes)(LPCSTR path);
} sh_packages_test_find_api;

void sh_packages_test_set_api(const sh_packages_test_find_api *api);
void sh_packages_test_reset_api(void);
#endif

#endif /* BACKEND_PACKAGES_H */
