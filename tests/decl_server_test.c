/* decl_server_test.c -- pure identity and structural-validation tests. */
#include <stdio.h>
#include <string.h>

#include "decl_server_path.h"
#include "decl_text.h"

static int g_failed;

#define CHECK(expr) do {                                                        \
    if (!(expr)) {                                                              \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
        g_failed++;                                                             \
    }                                                                           \
} while (0)

static void expect_path(const char *relative, const char *want_type,
                        const char *want_name, const char *want_source)
{
    char type[SH_DECL_SERVER_TYPE_CAP];
    char name[SH_DECL_SERVER_NAME_CAP];
    char source[SH_DECL_SERVER_SOURCE_CAP];
    const char *reason = NULL;
    int ok = sh_decl_server_identity_from_relative(relative,
                                                    type, sizeof(type),
                                                    name, sizeof(name),
                                                    source, sizeof(source),
                                                    &reason);
    CHECK(ok == 1);
    if (!ok) {
        fprintf(stderr, "  path '%s' refused: %s\n", relative, reason ? reason : "?");
        return;
    }
    CHECK(strcmp(type, want_type) == 0);
    CHECK(strcmp(name, want_name) == 0);
    CHECK(strcmp(source, want_source) == 0);
}

static void refuse_path(const char *relative, const char *reason_fragment)
{
    char type[SH_DECL_SERVER_TYPE_CAP];
    char name[SH_DECL_SERVER_NAME_CAP];
    char source[SH_DECL_SERVER_SOURCE_CAP];
    const char *reason = NULL;
    int ok = sh_decl_server_identity_from_relative(relative,
                                                    type, sizeof(type),
                                                    name, sizeof(name),
                                                    source, sizeof(source),
                                                    &reason);
    CHECK(ok == 0);
    CHECK(reason != NULL);
    if (reason && reason_fragment) CHECK(strstr(reason, reason_fragment) != NULL);
}

static void test_identity_paths(void)
{
    expect_path("actormodifier\\actormodifier\\demon\\cacodemon.decl",
                "actormodifier", "actormodifier/demon/cacodemon",
                "generated/decls/actormodifier/actormodifier/demon/cacodemon.decl");
    expect_path("ActorModifier/actormodifier/demon/pinky.DECL",
                "ActorModifier", "actormodifier/demon/pinky",
                "generated/decls/ActorModifier/actormodifier/demon/pinky.DECL");
    expect_path("material/generated/my-material.v2.decl",
                "material", "generated/my-material.v2",
                "generated/decls/material/generated/my-material.v2.decl");

    refuse_path("cacodemon.decl", "type");
    refuse_path("actormodifier/cacodemon.json", "extension");
    refuse_path("/actormodifier/cacodemon.decl", "absolute");
    refuse_path("C:\\actormodifier\\cacodemon.decl", "absolute");
    refuse_path("actormodifier//cacodemon.decl", "empty");
    refuse_path("actormodifier/../cacodemon.decl", "traversal");
    refuse_path("actormodifier/./cacodemon.decl", "traversal");
    refuse_path("actor modifier/cacodemon.decl", "type");
    refuse_path("actormodifier/demon rune.decl", "character");
    refuse_path("actormodifier/.decl", "name");
}

static void test_text_validation(void)
{
    static const unsigned char valid[] =
        "// opening brace in a comment: {\n"
        "{\n"
        "  inherit = \"player/{ignored}\\\"quoted\\\"\"\n"
        "  /* ignored close: } */\n"
        "}\n";
    static const unsigned char nested[] = "{ outer { inner } }";
    static const unsigned char bad_close[] = "{ } }";
    static const unsigned char bad_open[] = "{ { }";
    static const unsigned char bad_quote[] = "{ value = \"unterminated }";
    static const unsigned char bad_comment[] = "{ /* unterminated }";
    static const unsigned char no_brace[] = "value = 1";
    static const unsigned char embedded_nul[] = { '{', '}', 0, '{', '}' };

    CHECK(sh_decl_text_well_formed(valid, sizeof(valid) - 1) == 1);
    CHECK(sh_decl_text_well_formed(nested, sizeof(nested) - 1) == 1);
    CHECK(sh_decl_text_well_formed(bad_close, sizeof(bad_close) - 1) == 0);
    CHECK(sh_decl_text_well_formed(bad_open, sizeof(bad_open) - 1) == 0);
    CHECK(sh_decl_text_well_formed(bad_quote, sizeof(bad_quote) - 1) == 0);
    CHECK(sh_decl_text_well_formed(bad_comment, sizeof(bad_comment) - 1) == 0);
    CHECK(sh_decl_text_well_formed(no_brace, sizeof(no_brace) - 1) == 0);
    CHECK(sh_decl_text_well_formed(embedded_nul, sizeof(embedded_nul)) == 0);
    CHECK(sh_decl_text_well_formed(NULL, 0) == 0);
}

int main(void)
{
    test_identity_paths();
    test_text_validation();
    if (g_failed) {
        fprintf(stderr, "%d decl-server test(s) failed\n", g_failed);
        return 1;
    }
    puts("decl server tests passed");
    return 0;
}
