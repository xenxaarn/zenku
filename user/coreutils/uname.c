#include <zenku.h>

int main(long argc, char **argv)
{
    static const char name[] = "Zenku Thunder x86_64\n";

    (void)argc;
    (void)argv;
    sys_write(1, name, sizeof(name) - 1);
    return 0;
}
