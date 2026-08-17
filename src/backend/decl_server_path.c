/* decl_server_path.c -- see decl_server_path.h. Pure C; no Windows APIs. */
#include "decl_server_path.h"

#include <stdio.h>
#include <string.h>

#define DS_NORMALIZED_CAP 704

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int suffix_decl(const char *s, size_t n)
{
    static const char ext[] = ".decl";
    size_t i;
    if (n < sizeof(ext) - 1) return 0;
    for (i = 0; i < sizeof(ext) - 1; i++)
        if (ascii_lower((unsigned char)s[n - (sizeof(ext) - 1) + i]) != ext[i]) return 0;
    return 1;
}

static int type_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static int name_char(unsigned char c)
{
    if (c <= 0x20 || c >= 0x7f) return 0;
    return c != '<' && c != '>' && c != ':' && c != '"' &&
           c != '|' && c != '?' && c != '*';
}

static int dot_segment(const char *s, size_t n)
{
    return (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.');
}

static int refuse(const char **reason, const char *message)
{
    if (reason) *reason = message;
    return 0;
}

int sh_decl_server_identity_from_relative(const char *relative,
                                          char *type, size_t type_cap,
                                          char *name, size_t name_cap,
                                          char *source, size_t source_cap,
                                          const char **reason)
{
    char normalized[DS_NORMALIZED_CAP];
    size_t length, i, slash = (size_t)-1, ext_start, segment_start;
    int written;

    if (reason) *reason = NULL;
    if (!relative || !relative[0]) return refuse(reason, "empty relative path");
    if (!type || !name || !source || type_cap == 0 || name_cap == 0 || source_cap == 0)
        return refuse(reason, "missing output buffer");

    length = strlen(relative);
    if (length >= sizeof(normalized)) return refuse(reason, "relative path too long");
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)relative[i];
        normalized[i] = c == '\\' ? '/' : (char)c;
    }
    normalized[length] = '\0';
    if (normalized[0] == '/' || strchr(normalized, ':'))
        return refuse(reason, "absolute or drive-qualified path");
    if (!suffix_decl(normalized, length)) return refuse(reason, "unsupported extension (expected .decl)");

    ext_start = length - 5;
    for (i = 0; i < ext_start; i++) {
        if (normalized[i] == '/') {
            if (slash == (size_t)-1) slash = i;
        }
    }
    if (slash == (size_t)-1 || slash == 0) return refuse(reason, "missing decl type directory");
    if (slash + 1 >= ext_start) return refuse(reason, "missing logical decl name");
    if (slash + 1 > type_cap) return refuse(reason, "decl type too long");
    for (i = 0; i < slash; i++)
        if (!type_char((unsigned char)normalized[i])) return refuse(reason, "invalid decl type character");

    segment_start = slash + 1;
    for (i = segment_start; i <= ext_start; i++) {
        int at_end = i == ext_start;
        if (!at_end && normalized[i] != '/') {
            if (!name_char((unsigned char)normalized[i])) return refuse(reason, "invalid logical-name character");
            continue;
        }
        if (i == segment_start) return refuse(reason, "empty logical-name segment");
        if (dot_segment(normalized + segment_start, i - segment_start))
            return refuse(reason, "dot traversal segment");
        segment_start = i + 1;
    }

    if (ext_start - (slash + 1) + 1 > name_cap) return refuse(reason, "logical decl name too long");
    memcpy(type, normalized, slash);
    type[slash] = '\0';
    memcpy(name, normalized + slash + 1, ext_start - slash - 1);
    name[ext_start - slash - 1] = '\0';
    written = _snprintf_s(source, source_cap, _TRUNCATE, "generated/decls/%s", normalized);
    if (written < 0) return refuse(reason, "source path too long");
    return 1;
}
