/* package_requirements.h -- safe, restart-only runtime requirements for override packages. */
#ifndef SNAPMAP_PLUS_PACKAGE_REQUIREMENTS_H
#define SNAPMAP_PLUS_PACKAGE_REQUIREMENTS_H

#include <stddef.h>
#include <stdint.h>

/* Capture generated/requirements/*.requirements below data_root. Each row is:
 *   cvar<TAB>name<TAB>value
 *
 * Only product-audited, idempotent cvar/value pairs are accepted. Requirements
 * are queued once, after DOOM reports load_state == RUNNING. The command-system
 * pointers are signature-resolved by the caller. */
int sh_package_requirements_install(const char *data_root,
                                    const uint8_t *module_base,
                                    void *cmdsys,
                                    void *buffer_command,
                                    int user_layer_enabled);

/* Called from the existing backend/UI tick. It is a no-op until an admitted
 * requirement snapshot exists and the engine has reached RUNNING. */
void sh_package_requirements_poll(void);

#ifdef SH_PACKAGE_REQUIREMENTS_TESTING
void sh_package_requirements_test_reset(void);
void sh_package_requirements_test_set_load_state(volatile int *state);
size_t sh_package_requirements_test_count(void);
#endif

#endif /* SNAPMAP_PLUS_PACKAGE_REQUIREMENTS_H */
