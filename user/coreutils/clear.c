#include <zenku.h>

int main(long argc, char **argv)
{
    (void)argc;
    (void)argv;
    return sys_clear() < 0 ? 1 : 0;
}
