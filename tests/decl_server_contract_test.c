/* decl_server_contract_test.c -- startup/signature/source-wiring contract. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

#define CHECK(expr) do {                                                        \
    if (!(expr)) {                                                              \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
        g_failed++;                                                             \
    }                                                                           \
} while (0)

static char *read_file(const char *root, const char *relative)
{
    char path[1024];
    FILE *file = NULL;
    long length;
    char *bytes;
    if (_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s", root, relative) < 0) return NULL;
    if (fopen_s(&file, path, "rb") != 0 || !file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (char *)malloc((size_t)length + 1);
    if (!bytes) { fclose(file); return NULL; }
    if (length && fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes); fclose(file); return NULL;
    }
    fclose(file);
    bytes[length] = '\0';
    return bytes;
}

int main(int argc, char **argv)
{
    char *dllmain, *server, *signatures, *build, *overrides;
    const char *capture, *shadow, *commands, *decl_server;
    if (argc != 2) {
        fprintf(stderr, "usage: decl_server_contract_test <repo-root>\n");
        return 2;
    }
    dllmain = read_file(argv[1], "src\\backend\\dllmain.c");
    server = read_file(argv[1], "src\\backend\\decl_server.c");
    signatures = read_file(argv[1], "src\\backend\\signatures.c");
    build = read_file(argv[1], "src\\backend\\build.ps1");
    overrides = read_file(argv[1], "src\\backend\\overrides.c");
    CHECK(dllmain && server && signatures && build && overrides);
    if (!dllmain || !server || !signatures || !build || !overrides) goto done;

    capture = strstr(dllmain, "sh_user_overrides_capture_launch_state();");
    shadow = strstr(dllmain, "sh_overrides_install(res_ctor");
    commands = strstr(dllmain, "sh_commands_install(add_cmd");
    decl_server = strstr(dllmain, "sh_decl_server_install(results, db");
    CHECK(capture && shadow && commands && decl_server);
    if (capture && shadow) CHECK(capture < shadow);
    if (shadow && commands) CHECK(shadow < commands);
    if (commands && decl_server) CHECK(commands < decl_server);

    CHECK(strstr(server, "\\\\overrides\\\\generated\\\\decls") != NULL);
    CHECK(strstr(server, "snapmap_plus_decl_server_apply") != NULL);
    CHECK(strstr(server, "FILE_FLAG_OPEN_REPARSE_POINT") != NULL);
    CHECK(strstr(server, "DS_REGISTRY_TYPE_SLOT   0x58u") != NULL);
    CHECK(strstr(server, "DS_REGISTRY_ADD_SLOT    0x70u") != NULL);
    CHECK(strstr(server, "decl-server REGISTERED") == NULL); /* status is emitted through ds_log */
    CHECK(strstr(server, "ds_log(\"REGISTERED\"") != NULL);
    CHECK(strstr(server, "ds_log(\"SHADOWED\"") != NULL);
    CHECK(strstr(server, "ds_log(\"REFUSED\"") != NULL);
    CHECK(strstr(server, "Sleep(") == NULL);

    CHECK(strstr(signatures, "\"DeclRegistryAnchor\"") != NULL);
    CHECK(strstr(signatures, "0x184E1D0u") != NULL);
    CHECK(strstr(signatures, "\"DeclTypeByName\"") != NULL);
    CHECK(strstr(signatures, "0x17B43B0u") != NULL);
    CHECK(strstr(signatures, "\"DeclAddFromText\"") != NULL);
    CHECK(strstr(signatures, "0x17B2C00u") != NULL);
    CHECK(strstr(signatures, "\"DeclFind\"") != NULL);
    CHECK(strstr(signatures, "0x17B36F0u") != NULL);

    CHECK(strstr(build, "\"decl_text.c\"") != NULL);
    CHECK(strstr(build, "\"decl_server_path.c\"") != NULL);
    CHECK(strstr(build, "\"decl_server.c\"") != NULL);
    CHECK(strstr(overrides, "sh_decl_text_well_formed") != NULL);
    CHECK(strstr(overrides, "int sh_overrides_get_root") != NULL);

done:
    free(dllmain); free(server); free(signatures); free(build); free(overrides);
    if (g_failed) {
        fprintf(stderr, "%d decl-server contract test(s) failed\n", g_failed);
        return 1;
    }
    puts("decl server contract tests passed");
    return 0;
}
