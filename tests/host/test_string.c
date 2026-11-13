#include <string.h>

#include "test.h"

void test_string(void)
{
    char buf[32];
    char overlap[16] = "abcdefghij";

    /* memcpy/memset return the destination. */
    CHECK(memset(buf, 0xAA, sizeof(buf)) == buf);
    for (size_t i = 0; i < sizeof(buf); i++) {
        CHECK_EQ((unsigned char)buf[i], 0xAA);
    }

    CHECK(memcpy(buf, "hello", 6) == buf);
    CHECK(strcmp(buf, "hello") == 0);

    CHECK(memset(buf, 0, 0) == buf);
    CHECK(memcpy(buf, "zzz", 0) == buf);

    /* memmove handles overlapping ranges in both directions. */
    CHECK(memmove(overlap + 2, overlap, 5) == overlap + 2);
    CHECK(memcmp(overlap, "ababcdehij", 10) == 0);

    memset(overlap, 0, sizeof(overlap));
    memcpy(overlap, "abcdefghij", 10);
    CHECK(memmove(overlap, overlap + 2, 5) == overlap);
    CHECK(memcmp(overlap, "cdefgfghij", 10) == 0);

    /* memcmp ordering and zero-length behaviour. */
    CHECK_EQ(memcmp("abc", "abd", 3) < 0, 1);
    CHECK_EQ(memcmp("abd", "abc", 3) > 0, 1);
    CHECK_EQ(memcmp("abc", "abc", 3), 0);
    CHECK_EQ(memcmp("abc", "xyz", 0), 0);

    /* strlen / strcmp / strncmp. */
    CHECK_EQ(strlen(""), 0);
    CHECK_EQ(strlen("zenku"), 5);
    CHECK_EQ(strcmp("", ""), 0);
    CHECK_EQ(strcmp("a", "b") < 0, 1);
    CHECK_EQ(strcmp("b", "a") > 0, 1);
    CHECK_EQ(strncmp("zenku", "zen", 3), 0);
    CHECK_EQ(strncmp("zenku", "zen", 4) > 0, 1);
    CHECK_EQ(strncmp("zenku", "zen", 0), 0);

    /* strcpy / strncpy including padding. */
    memset(buf, 'Q', sizeof(buf));
    CHECK(strcpy(buf, "abc") == buf);
    CHECK(strcmp(buf, "abc") == 0);

    memset(buf, 'Q', sizeof(buf));
    CHECK(strncpy(buf, "abc", 6) == buf);
    CHECK(memcmp(buf, "abc\0\0\0", 6) == 0);

    memset(buf, 'Q', sizeof(buf));
/* Deliberate truncation is the behaviour under test. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(buf, "abcdef", 3);
#pragma GCC diagnostic pop
    CHECK(memcmp(buf, "abc", 3) == 0);

    printf("string: %d checks\n", g_checks);
}
