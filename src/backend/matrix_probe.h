/* matrix_probe.h -- one-shot diagnostic for a singular editor model matrix.
 *
 * The engine builds a render instance's model matrix from an origin, a 3x3 axis
 * and a 3-component scale, then inverts it; when the determinant is ~0 it logs
 * "modelMatrix invert failed on model %s", forces the axis to identity and
 * carries on. That fallback is why an affected entity still draws but can no
 * longer be rotated.
 *
 * The log line names only the model, never the numbers, so it cannot say which
 * of the nine axis terms or three scale terms is zero. This service samples the
 * builder's own arguments and logs the first few degenerate cases, then goes
 * quiet. It is read-only and changes no engine state.
 *
 * Diagnostic only: it is expected to be removed once the cause is understood. */
#ifndef BACKEND_MATRIX_PROBE_H
#define BACKEND_MATRIX_PROBE_H

#include <stdint.h>

/* Detour the pinned matrix builder. Verifies the prologue before patching and
 * refuses on any mismatch. Returns 1 when installed, 0 on refusal. */
int sh_matrix_probe_install(const uint8_t *module_base);

/* Reverse the detour. Idempotent; returns 1 when a hook was removed. */
int sh_matrix_probe_uninstall(void);

#endif /* BACKEND_MATRIX_PROBE_H */
