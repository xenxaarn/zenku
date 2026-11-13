#ifndef ZENKU_HOST_TEST_H
#define ZENKU_HOST_TEST_H

#include <stdio.h>

extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        long long _a = (long long)(actual);                                  \
        long long _e = (long long)(expected);                                \
        g_checks++;                                                          \
        if (_a != _e) {                                                      \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s == %lld, expected %lld\n",       \
                    __FILE__, __LINE__, #actual, _a, _e);                    \
        }                                                                    \
    } while (0)

void test_string(void);
void test_ext2(const char *image_dir);

#endif /* ZENKU_HOST_TEST_H */
