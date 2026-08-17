/* decl_server_path.h -- pure path-to-decl-identity normalization. */
#ifndef BACKEND_DECL_SERVER_PATH_H
#define BACKEND_DECL_SERVER_PATH_H

#include <stddef.h>

#define SH_DECL_SERVER_TYPE_CAP   64
#define SH_DECL_SERVER_NAME_CAP   512
#define SH_DECL_SERVER_SOURCE_CAP 768

/* Convert a path relative to overrides/generated/decls into the two identities
 * the engine API requires. Example:
 *
 *   actormodifier/actormodifier/demon/cacodemon.decl
 *       type   = actormodifier
 *       name   = actormodifier/demon/cacodemon
 *       source = generated/decls/actormodifier/actormodifier/demon/cacodemon.decl
 *
 * Both slash styles are accepted. Absolute paths, traversal, empty segments,
 * whitespace/control bytes, Windows-special bytes, non-.decl files, and
 * truncated outputs are refused. `reason` receives a static diagnostic string. */
int sh_decl_server_identity_from_relative(const char *relative,
                                          char *type, size_t type_cap,
                                          char *name, size_t name_cap,
                                          char *source, size_t source_cap,
                                          const char **reason);

#endif /* BACKEND_DECL_SERVER_PATH_H */
