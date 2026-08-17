/* decl_text.h -- bounded structural validation shared by decl file consumers. */
#ifndef BACKEND_DECL_TEXT_H
#define BACKEND_DECL_TEXT_H

#include <stddef.h>

/* Return 1 when `text` is non-empty, contains no embedded NUL, and has balanced
 * braces, quotes, and comments. This is deliberately structural only; DOOM's
 * own decl parser remains the semantic authority. */
int sh_decl_text_well_formed(const unsigned char *text, size_t length);

#endif /* BACKEND_DECL_TEXT_H */
