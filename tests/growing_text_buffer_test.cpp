#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "growing_text_buffer.h"

struct CopySource {
    std::string text;
    int calls;
    bool fail;
};

static bool copy_source(CopySource *source, char *out, int cap)
{
    source->calls++;
    if (source->fail || !out || cap < 1) return false;
    std::size_t count = source->text.size();
    if (count > static_cast<std::size_t>(cap - 1)) count = static_cast<std::size_t>(cap - 1);
    if (count) std::memcpy(out, source->text.data(), count);
    out[count] = '\0';
    return true;
}

static int expect(bool condition, const char *message)
{
    if (condition) return 0;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main()
{
    int failures = 0;

    {
        CopySource source = {std::string(240000, 'A'), 0, false};
        std::vector<char> buffer;
        bool truncated = false;
        bool ok = sh_read_growing_text(buffer, 64u * 1024u, 1024u * 1024u,
            [&](char *out, int cap) { return copy_source(&source, out, cap); }, &truncated);
        failures += expect(ok, "large text read failed");
        failures += expect(!truncated, "large text was reported truncated");
        failures += expect(std::string(buffer.data()) == source.text, "large text did not round-trip exactly");
        failures += expect(source.calls == 3, "large text did not grow through the expected capacities");
    }

    {
        CopySource source = {std::string((64u * 1024u) - 1u, 'B'), 0, false};
        std::vector<char> buffer;
        bool truncated = false;
        bool ok = sh_read_growing_text(buffer, 64u * 1024u, 1024u * 1024u,
            [&](char *out, int cap) { return copy_source(&source, out, cap); }, &truncated);
        failures += expect(ok && !truncated, "exact-boundary text was mistaken for truncation");
        failures += expect(source.calls == 2, "exact-boundary text was not disambiguated with a retry");
        failures += expect(std::strlen(buffer.data()) == source.text.size(), "exact-boundary text changed length");
    }

    {
        CopySource source = {std::string(70000, 'C'), 0, false};
        std::vector<char> buffer;
        bool truncated = false;
        bool ok = sh_read_growing_text(buffer, 64u * 1024u, 64u * 1024u,
            [&](char *out, int cap) { return copy_source(&source, out, cap); }, &truncated);
        failures += expect(ok && truncated, "safety-cap truncation was not surfaced");
        failures += expect(std::strlen(buffer.data()) == (64u * 1024u) - 1u, "safety-cap copy length was unexpected");
    }

    {
        CopySource source = {"ignored", 0, true};
        std::vector<char> buffer;
        bool truncated = true;
        bool ok = sh_read_growing_text(buffer, 64u, 1024u,
            [&](char *out, int cap) { return copy_source(&source, out, cap); }, &truncated);
        failures += expect(!ok, "copy failure was reported as success");
        failures += expect(!truncated, "copy failure retained a stale truncation flag");
        failures += expect(buffer[0] == '\0', "copy failure did not clear the destination");
    }

    if (failures) return 1;
    std::puts("growing text buffer tests passed");
    return 0;
}
