#include <zenku.h>

int main(long argc, char **argv)
{
    static const char failed[] = "poweroff: ACPI S5 did not power off\n";

    (void)argc;
    (void)argv;
    sys_poweroff();
    sys_write(1, failed, sizeof(failed) - 1);
    return 1;
}
