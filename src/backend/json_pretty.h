/* json_pretty.h -- pure WHITESPACE re-layout of the engine's compact JSON, for sh_pretty_on.
 *
 * The engine's map serializer emits one dense line. `sh_pretty_on 1` promises the rawmap this project
 * writes is "pretty printed", which means exactly one thing here: the same document, same tokens, same
 * order, laid out over indented lines. This module is that layout pass and nothing more -- it never
 * parses a value, never re-encodes a number, never touches the inside of a string. Whitespace BETWEEN
 * tokens is the only thing it decides, so a re-laid-out rawmap stays byte-equivalent as JSON and still
 * loads through the LOAD swap.
 *
 * Why not just ask the engine for pretty output: its serializer's third argument selects compact vs not,
 * but that call fills the out-idStr the player's own save is written from -- steering it would change the
 * saved map, and the rawmap shadow's standing rule is that the real save completes identically whether
 * the shadow is armed or not (see rawmap.c). Laying out our own copy leaves the engine's save alone.
 *
 * Shape matches config_json.c's writer -- two spaces per level, ": " after a key, '\n' line ends, one
 * trailing newline -- so every JSON file this project writes reads the same way.
 *
 * No file system, no OS calls, no engine calls: the exact same code is unit-tested off-game
 * (tests/json_pretty_test.c) and compiled into the backend, the same arrangement dumpmap_path.h uses.
 */
#ifndef SNAPMAP_PLUS_JSON_PRETTY_H
#define SNAPMAP_PLUS_JSON_PRETTY_H

#include <stddef.h>

#define JSON_PRETTY_INDENT     2    /* spaces per nesting level (config_json.c's writer emits the same) */
#define JSON_PRETTY_MAX_DEPTH  64   /* deeper than any real map nests; refuse rather than emit a wall of indent */

/* Emit one character: counted always, stored only while it fits. `out == NULL` therefore turns the whole
 * pass into a pure size measurement, which is how the caller learns how much to allocate. */
static void jp_put(char *out, size_t outcap, size_t *pos, char ch)
{
    if (out != NULL && *pos < outcap) out[*pos] = ch;
    (*pos)++;
}

static void jp_indent(char *out, size_t outcap, size_t *pos, int depth)
{
    int n = depth * JSON_PRETTY_INDENT;
    for (int i = 0; i < n; i++) jp_put(out, outcap, pos, ' ');
}

static int jp_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Re-lay-out `len` bytes of JSON from `src` into `out` (capacity `outcap`). Returns the number of bytes
 * the laid-out form NEEDS, whether or not it fit, so the two-pass caller measures with (NULL, 0) then
 * writes with a buffer of exactly that size. The pass is deterministic: the same input always measures
 * and writes the same bytes.
 *
 * Returns 0 -- refusing, not truncating -- for input this cannot re-lay-out safely: an unterminated
 * string, an unbalanced or over-deep container, or nothing but whitespace. Callers write the ORIGINAL
 * bytes on a 0, because a rawmap that is hard to read still loads and a mangled one does not. Fails
 * closed, like every other JSON-surgery routine in the backend. */
static size_t json_pretty(const char *src, size_t len, char *out, size_t outcap)
{
    size_t pos = 0, i = 0;
    int    depth = 0;

    if (src == NULL || len == 0) return 0;

    while (i < len) {
        char c = src[i];

        /* The input's own spacing is discarded -- the layout below decides all of it. (The engine emits
         * none, but a hand-edited rawmap that got saved again would carry some.) */
        if (jp_is_space(c)) { i++; continue; }

        if (c == '"') {
            /* A string is copied byte for byte, escapes included: its contents are DATA, and a '{' or a
             * ',' inside one must not be read as structure. */
            jp_put(out, outcap, &pos, c);
            i++;
            for (;;) {
                char s;
                if (i >= len) return 0;                 /* unterminated string -> refuse */
                s = src[i];
                jp_put(out, outcap, &pos, s);
                i++;
                if (s == '\\') {                        /* \" and \\ -- the escaped byte is never a delimiter */
                    if (i >= len) return 0;
                    jp_put(out, outcap, &pos, src[i]);
                    i++;
                    continue;
                }
                if (s == '"') break;
            }
            continue;
        }

        if (c == '{' || c == '[') {
            size_t j;
            if (depth >= JSON_PRETTY_MAX_DEPTH) return 0;
            jp_put(out, outcap, &pos, c);
            depth++;
            /* An EMPTY container stays on its own line as "{}" / "[]" -- a close bracket alone on the
             * next line reads as a mistake, and map JSON is full of empty ones. */
            j = i + 1;
            while (j < len && jp_is_space(src[j])) j++;
            if (j < len && ((c == '{' && src[j] == '}') || (c == '[' && src[j] == ']'))) {
                jp_put(out, outcap, &pos, src[j]);
                depth--;
                i = j + 1;
                continue;
            }
            jp_put(out, outcap, &pos, '\n');
            jp_indent(out, outcap, &pos, depth);
            i++;
            continue;
        }

        if (c == '}' || c == ']') {
            if (depth <= 0) return 0;                   /* a close with nothing open -> refuse */
            depth--;
            jp_put(out, outcap, &pos, '\n');
            jp_indent(out, outcap, &pos, depth);
            jp_put(out, outcap, &pos, c);
            i++;
            continue;
        }

        if (c == ',') {                                 /* separator hugs the value it follows */
            jp_put(out, outcap, &pos, c);
            jp_put(out, outcap, &pos, '\n');
            jp_indent(out, outcap, &pos, depth);
            i++;
            continue;
        }

        if (c == ':') {
            jp_put(out, outcap, &pos, c);
            jp_put(out, outcap, &pos, ' ');
            i++;
            continue;
        }

        jp_put(out, outcap, &pos, c);                   /* number / true / false / null, verbatim */
        i++;
    }

    if (depth != 0) return 0;                           /* truncated document -> refuse */
    if (pos == 0)   return 0;                           /* nothing but whitespace -> refuse */
    jp_put(out, outcap, &pos, '\n');                    /* one trailing newline, as config_json.c writes */
    return pos;
}

#endif /* SNAPMAP_PLUS_JSON_PRETTY_H */
