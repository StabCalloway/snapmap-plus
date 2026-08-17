/* decl_text.c -- see decl_text.h. Pure C; no engine or filesystem dependency. */
#include "decl_text.h"

int sh_decl_text_well_formed(const unsigned char *text, size_t length)
{
    size_t i;
    long depth = 0;
    int seen_brace = 0;
    int in_quote = 0;
    int escaped = 0;
    int line_comment = 0;
    int block_comment = 0;

    if (!text || length == 0) return 0;
    for (i = 0; i < length; i++) {
        unsigned char c = text[i];
        unsigned char next = i + 1 < length ? text[i + 1] : 0;

        if (c == 0) return 0;
        if (line_comment) {
            if (c == '\n') line_comment = 0;
            continue;
        }
        if (block_comment) {
            if (c == '*' && next == '/') {
                block_comment = 0;
                i++;
            }
            continue;
        }
        if (in_quote) {
            if (c == '\n' || c == '\r') return 0;
            if (escaped) {
                escaped = 0;
            } else if (c == '\\') {
                escaped = 1;
            } else if (c == '"') {
                in_quote = 0;
            }
            continue;
        }
        if (c == '/' && next == '/') {
            line_comment = 1;
            i++;
        } else if (c == '/' && next == '*') {
            block_comment = 1;
            i++;
        } else if (c == '"') {
            in_quote = 1;
        } else if (c == '{') {
            depth++;
            seen_brace = 1;
        } else if (c == '}') {
            if (depth == 0) return 0;
            depth--;
        }
    }
    return seen_brace && depth == 0 && !in_quote && !escaped && !block_comment;
}
