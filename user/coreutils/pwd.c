#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char cwd[256];

    (void)argc;
    (void)argv;
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("pwd: cannot determine current directory\n");
        return 1;
    }
    puts(cwd);
    return 0;
}
