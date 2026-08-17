/* decl_server.h -- restart-time registration of genuinely new engine decl identities. */
#ifndef BACKEND_DECL_SERVER_H
#define BACKEND_DECL_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include "signatures.h"

/* Snapshot overrides/generated/decls, register an internal command, and enqueue
 * that command through DOOM's command buffer. The command executes exactly once
 * on the engine main thread. Existing identities remain file-shadow overrides;
 * only absent identities are added through the engine's native AddDeclFromText
 * virtual method. Returns 1 when work was queued or there was nothing to do, 0
 * when the service was refused. */
int sh_decl_server_install(const sig_result *results, size_t count,
                           const uint8_t *module_base, void *cmdsys);

#endif /* BACKEND_DECL_SERVER_H */
