#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc > 2) {
        fputs("Usage: hello [name]\n", stderr);
        return EXIT_FAILURE;
    }

    printf("Hello, %s!\n", argc == 2 ? argv[1] : "embedded");
    return 0;
}
