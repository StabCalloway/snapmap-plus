/* megapreview_io_test.c -- compact VMTR metadata and selected-entry Mega2 reads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/backend/megapreview.h"
#include "../src/backend/imgpreview.h"
#include "../src/backend/preview.h"

static int failures;
static int material_image_calls, direct_image_calls;
static unsigned long direct_image_generation;
static char direct_image_name[128];

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

void backend_log(const char *msg) { (void)msg; }
uintptr_t sig_addr_by_name(const sig_result *results, size_t n, const char *name)
{ (void)results; (void)n; (void)name; return 0; }
int sh_imgpreview_produce(const char *name, unsigned long generation)
{ (void)name; (void)generation; material_image_calls++; return SH_PREVIEW_FAILED; }
int sh_imgpreview_produce_image(const char *name, unsigned long generation)
{
    direct_image_calls++;
    direct_image_generation = generation;
    _snprintf_s(direct_image_name, sizeof direct_image_name, _TRUNCATE, "%s", name);
    return SH_PREVIEW_PUBLISHED;
}
int sh_preview_take_request(char *out, size_t cap, unsigned long *generation, int *kind)
{ (void)out; (void)cap; (void)generation; (void)kind; return 0; }
int sh_preview_publish(unsigned long generation, const unsigned char *rgba, unsigned w, unsigned h)
{ (void)generation; (void)rgba; (void)w; (void)h; return SH_PREVIEW_FAILED; }

#include "../src/backend/megapreview.c"

static void put_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void put_u64(unsigned char *p, unsigned long long v)
{
    for (int i = 0; i < 8; ++i) { p[i] = (unsigned char)v; v >>= 8; }
}

static int write_bytes(const char *path, const void *data, size_t size)
{
    FILE *f = NULL;
    if (fopen_s(&f, path, "wb") != 0 || !f) return 0;
    int ok = fwrite(data, 1, size, f) == size && fclose(f) == 0;
    return ok;
}

static void cleanup_rects(void)
{
    if (g_rectNames) free(g_rectNames);
    else for (int i = 0; i < g_rectCount; ++i) free((void *)g_rects[i].name);
    free(g_rects);
    g_rects = NULL; g_rectNames = NULL;
    g_rectCount = g_rectCap = 0; g_rectNameBytes = 0;
}

static void check_virtualtextures_root(const char *root)
{
    memset(g_shard, 0, sizeof g_shard);
    _snprintf_s(g_vtDir, sizeof g_vtDir, _TRUNCATE, "%s", root);
    CHECK(megapreview_load_rects() == 1);
    unsigned long long tableBytes = 0;
    int opened = 0;
    for (int n = 1; n <= 16; ++n) {
        shard_t *s = megapreview_shard(n);
        CHECK(s != NULL);
        if (!s) continue;
        opened++;
        tableBytes += (unsigned long long)s->idxCount * 4u +
                      (unsigned long long)s->pageCount * 16u;
    }
    printf("Mega2 metadata stays on disk: %llu table/index bytes across %d shards; "
           "%d VMTR rects retain %llu name bytes plus %llu rect bytes\n",
        tableBytes, opened, g_rectCount, (unsigned long long)g_rectNameBytes,
        (unsigned long long)((size_t)g_rectCap * sizeof *g_rects));
    for (int n = 1; n <= 16; ++n)
        if (g_shard[n].f) { fclose(g_shard[n].f); g_shard[n].f = NULL; }
    cleanup_rects();
}

int main(int argc, char **argv)
{
    char tempPath[MAX_PATH], tempDir[MAX_PATH], vmtr[MAX_PATH], shardPath[MAX_PATH];
    CHECK(megapreview_service_request("textures/overlap", 77, SH_ASSET_IMAGE) ==
          SH_PREVIEW_PUBLISHED);
    CHECK(direct_image_calls == 1 && material_image_calls == 0);
    CHECK(direct_image_generation == 77);
    CHECK(strcmp(direct_image_name, "textures/overlap") == 0);
    CHECK(g_rects == NULL && g_out == NULL && g_page == NULL);

    CHECK(GetTempPathA(MAX_PATH, tempPath) > 0);
    CHECK(GetTempFileNameA(tempPath, "smp", 0, tempDir) != 0);
    CHECK(DeleteFileA(tempDir) != 0);
    CHECK(CreateDirectoryA(tempDir, NULL) != 0);
    _snprintf_s(g_vtDir, sizeof g_vtDir, _TRUNCATE, "%s", tempDir);

    _snprintf_s(vmtr, sizeof vmtr, _TRUNCATE, "%s\\catalog.vmtr", tempDir);
    static const char rows[] =
        "1 2 120 240 0 0 0 \"materials/one\"\r\n"
        "3 4 360 480 0 0 0 \"materials/a_name_that_uses_only_its_real_bytes\"\r\n";
    CHECK(write_bytes(vmtr, rows, sizeof rows - 1u));
    CHECK(megapreview_load_rects() == 1);
    CHECK(g_rectCount == 2 && g_rectCap == 2);
    CHECK(g_rectNames != NULL);
    CHECK(g_rectNameBytes == strlen("materials/one") + 1u +
                             strlen("materials/a_name_that_uses_only_its_real_bytes") + 1u);
    CHECK(g_rectNameBytes < (size_t)g_rectCount * 192u);
    CHECK(strcmp(g_rects[0].name, "materials/one") == 0);
    CHECK(g_rects[1].x == 3 && g_rects[1].y == 4 &&
          g_rects[1].w == 360 && g_rects[1].h == 480);
    CHECK(g_out == NULL && g_page == NULL);
    CHECK(megapreview_alloc_scratch() == 1);
    CHECK(g_out != NULL && g_page != NULL);
    for (int L = 0; L < MAX_LEVELS; ++L) CHECK(g_levelTmp[L] != NULL);
    megapreview_free_scratch();
    CHECK(g_out == NULL && g_page == NULL);

    unsigned char file[0x220] = {0};
    const unsigned tableOff = 0x180, indexOff = 0x1A0, payloadOff = 0x1C0;
    put_u32(file + 0x00, 0xA63FBB21u);
    put_u32(file + 0x04, 2u);
    put_u64(file + 0x38, tableOff);
    put_u64(file + 0x40, indexOff);
    put_u32(file + 0x48, 2u);
    put_u32(file + 0x4C, 3u);
    put_u64(file + tableOff, payloadOff);
    put_u64(file + tableOff + 8, 32u);
    put_u32(file + indexOff, 0u);
    put_u32(file + indexOff + 4, 0xFFFFFFFFu);
    put_u32(file + indexOff + 8, 5u);
    memset(file + payloadOff, 0x5A, 32u);
    _snprintf_s(shardPath, sizeof shardPath, _TRUNCATE, "%s\\_vmtr_sq1.mega2", tempDir);
    CHECK(write_bytes(shardPath, file, sizeof file));

    shard_t *s = megapreview_shard(1);
    CHECK(s != NULL && s->f != NULL);
    CHECK(sizeof *s <= 64u);
    if (s) {
        unsigned long long off = 0, size = 0;
        CHECK(s->indexOff == indexOff && s->tableOff == tableOff);
        CHECK(s->idxCount == 3 && s->pageCount == 2);
        CHECK(megapreview_page_entry(s, 0, &off, &size) == 1);
        CHECK(off == payloadOff && size == 32u);
        CHECK(megapreview_page_entry(s, 1, &off, &size) == 0);
        CHECK(megapreview_page_entry(s, 2, &off, &size) == 0);
        CHECK(megapreview_page_entry(s, 3, &off, &size) == 0);
        fclose(s->f); s->f = NULL;
    }

    cleanup_rects();
    CHECK(DeleteFileA(shardPath) != 0);
    CHECK(DeleteFileA(vmtr) != 0);
    CHECK(RemoveDirectoryA(tempDir) != 0);

    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--virtualtextures-root") == 0 && i + 1 < argc)
            check_virtualtextures_root(argv[++i]);

    if (failures) {
        fprintf(stderr, "%d megatexture IO test(s) failed\n", failures);
        return 1;
    }
    puts("megatexture metadata IO tests passed");
    return 0;
}
