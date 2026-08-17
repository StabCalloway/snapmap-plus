/* imgpreview.h -- the SECOND preview producer: plain (non-megatexture) materials and direct images.
 *
 * megapreview.c serves the 5,033 materials that have a `.vmtr` atlas rect. This serves the rest,
 * which render fine in game but are backed by ordinary image assets in the `.index`/`.resources`
 * containers, plus image names selected directly in the Assets browser. Together the material
 * routes cover ~84% of the ~9,805-material catalog; the remainder are decals and particles baked
 * into shared atlases, which need a third route.
 *
 * Read-only against the shipped containers, and no engine call at all: DEFLATE and BCn are public
 * formats, unlike the megatexture page codec which had to be called rather than reimplemented.
 */
#ifndef BACKEND_IMGPREVIEW_H
#define BACKEND_IMGPREVIEW_H

/* One-time setup. Cheap: resource indexes are parsed lazily on the first request, and optional
 * Wwise/.vmtr catalog metadata waits for its corresponding category. Always returns 1. */
int sh_imgpreview_install(void);

/* Resolve a material name through its decl to an image, or accept a direct image name, decode it to
 * RGBA, and publish through sh_preview_publish. Returns 1 if something was published, 0 otherwise.
 * Call it only AFTER the megatexture route has declined for a material request. */
int sh_imgpreview_produce(const char *name, unsigned long generation);

/* Decode exactly the named Image record. Unlike the compatibility producer above, this never treats a
 * same-named Material as authoritative and is called before any VMTR work for a typed Image selection. */
int sh_imgpreview_produce_image(const char *name, unsigned long generation);

/* The SH_ASSET_* type ids live in the shared ABI header -- the UI sends one across the iface. */
#include "../common/snapmap_plus_iface.h"

/* Enumerate installed asset names of one type for the Assets browser, newline-separated, starting
 * at index `start`. Returns how many names were written; 0 means "no more" (or unavailable data).
 * The caller pages by adding the returned count to `start`. Base index metadata is compacted after
 * parsing; the Wwise sound union and decl-less .vmtr material union are loaded only for those kinds. */
int sh_imgpreview_list(int kind, unsigned start, char *out, size_t cap);

/* Does a decl of this SH_ASSET_* type with this exact name exist in the shipped containers? The
 * answer comes from our own index, never from the engine: the engine's by-name decl find is a
 * find-OR-CREATE that fatals on a bad name, so it can only be called with a name already known to
 * be good. sh_soundpreview_play is the caller that needs this. Returns 1 if present. */
int sh_imgpreview_has(int kind, const char *name);

/* Internal cooked-geometry record kind. It is intentionally outside SH_ASSET_*: baseModel rows are
 * implementation payloads behind an md6Def, not names the Assets browser should offer directly. */
#define SH_IMGPREVIEW_BASEMODEL_KIND 250

/* Read one exact installed-resource payload by indexed kind/name. The caller owns *out_bytes and frees
 * it with free(). `max_bytes` is a hard allocation/decompression ceiling; oversize and missing records
 * fail without allocating. This is the read-only bridge used by the Prefab Details renderer, which
 * decodes geometry lazily without copying any game asset into Snapmap+ itself. */
int sh_imgpreview_read_payload(int kind, const char *name, size_t max_bytes,
                               unsigned char **out_bytes, size_t *out_len);

#endif /* BACKEND_IMGPREVIEW_H */
