/* matrix_probe.c -- see matrix_probe.h.
 *
 * The builder at RVA 0x1A81640 is
 *
 *   void build(const idVec3 *origin, const idMat3 *axis, const idVec3 *scale,
 *              float out[16])
 *
 * and it composes the upper 3x3 as axis columns scaled component-wise:
 *
 *   out[0] = axis[0]*scale[0]   out[1] = axis[3]*scale[1]   out[2] = axis[6]*scale[2]
 *   out[4] = axis[1]*scale[0]   out[5] = axis[4]*scale[1]   out[6] = axis[7]*scale[2]
 *   out[8] = axis[2]*scale[0]   out[9] = axis[5]*scale[1]   out[10]= axis[8]*scale[2]
 *
 * so its determinant is det(axis) * scale.x * scale.y * scale.z, and the invert
 * that follows fails whenever that product is ~0. This detour samples the same
 * arguments, reports the first few degenerate calls with every term spelled out,
 * and then stays silent for the rest of the process.
 *
 * The prologue is 14 bytes of whole, position-independent SSE instructions, so
 * it can carry the backend's standard 14-byte absolute-jump detour without
 * relocating anything RIP-relative.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "backend_log.h"
#include "hook.h"
#include "matrix_probe.h"

#define MP_BUILDER_RVA   0x1A81640u
#define MP_STOLEN        14u
#define MP_MAX_REPORTS   6
/* Comfortably below any real scale, far above float noise in a valid axis. */
#define MP_SINGULAR_EPS  1e-9

typedef void (*mp_build_fn)(const float *origin, const float *axis,
                            const float *scale, float *out);

static mp_build_fn g_orig;
static void *g_tramp;
static volatile LONG g_reports;

static const unsigned char MP_PROLOGUE[] = {
    0xF3, 0x0F, 0x10, 0x02,              /* movss  xmm0, [rdx]      */
    0xF3, 0x41, 0x0F, 0x59, 0x00,        /* mulss  xmm0, [r8]       */
    0xF3, 0x41, 0x0F, 0x11, 0x01         /* movss  [r9], xmm0       */
};

static int mp_safe_read(const void *source, void *destination, size_t length)
{
    __try {
        memcpy(destination, source, length);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static double mp_axis_determinant(const float *a)
{
    return (double)a[0] * ((double)a[4] * a[8] - (double)a[5] * a[7])
         - (double)a[1] * ((double)a[3] * a[8] - (double)a[5] * a[6])
         + (double)a[2] * ((double)a[3] * a[7] - (double)a[4] * a[6]);
}

static void mp_report(const float *origin, const float *axis, const float *scale,
                      double axis_det, double det)
{
    char line[512];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "matrix-probe SINGULAR: origin=(%.3f %.3f %.3f) "
                "scale=(%.6f %.6f %.6f) axis=[%.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f] "
                "det(axis)=%.9f det(total)=%.9f",
                origin[0], origin[1], origin[2],
                scale[0], scale[1], scale[2],
                axis[0], axis[1], axis[2], axis[3], axis[4], axis[5],
                axis[6], axis[7], axis[8],
                axis_det, det);
    backend_log(line);
}

/* Runs on the engine's render path, so it does the cheapest possible thing once
 * the report budget is spent: an interlocked read and the original call. */
static void mp_detour(const float *origin, const float *axis, const float *scale,
                      float *out)
{
    if (InterlockedCompareExchange(&g_reports, 0, 0) < MP_MAX_REPORTS &&
        origin && axis && scale) {
        float o[3], a[9], s[3];
        if (mp_safe_read(origin, o, sizeof(o)) &&
            mp_safe_read(axis, a, sizeof(a)) &&
            mp_safe_read(scale, s, sizeof(s))) {
            double axis_det = mp_axis_determinant(a);
            double det = axis_det * (double)s[0] * (double)s[1] * (double)s[2];
            if (det < 0) det = -det;
            if (det < MP_SINGULAR_EPS &&
                InterlockedIncrement(&g_reports) <= MP_MAX_REPORTS)
                mp_report(o, a, s, axis_det, det);
        }
    }
    if (g_orig) g_orig(origin, axis, scale, out);
}

int sh_matrix_probe_install(const uint8_t *module_base)
{
    unsigned char prologue[sizeof(MP_PROLOGUE)];
    void *target;
    char line[256];

    if (g_tramp) return 1;
    if (!module_base) return 0;
    target = (void *)(module_base + MP_BUILDER_RVA);
    if (!mp_safe_read(target, prologue, sizeof(prologue)) ||
        memcmp(prologue, MP_PROLOGUE, sizeof(prologue)) != 0) {
        backend_log("matrix-probe REFUSED: pinned matrix builder prologue did not match this build");
        return 0;
    }
    g_tramp = install_inline_hook(target, (void *)mp_detour, MP_STOLEN);
    if (!g_tramp) {
        backend_log("matrix-probe REFUSED: inline detour installation failed");
        return 0;
    }
    g_orig = (mp_build_fn)g_tramp;
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "matrix-probe installed at rva=0x%x; reporting up to %d singular model matrices",
                (unsigned)MP_BUILDER_RVA, MP_MAX_REPORTS);
    backend_log(line);
    return 1;
}

int sh_matrix_probe_uninstall(void)
{
    if (!g_tramp) return 0;
    hook_unpatch(g_tramp);
    g_tramp = NULL;
    g_orig = NULL;
    backend_log("matrix-probe uninstalled");
    return 1;
}
