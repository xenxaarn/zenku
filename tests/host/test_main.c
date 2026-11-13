#include <stdio.h>
#include <stdlib.h>

#include "test.h"

int g_checks;
int g_failures;

int main(int argc, char **argv)
{
    const char *image_dir = argc > 1 ? argv[1] : "images";

    test_string();
    test_ext2(image_dir);

    printf("host tests: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
