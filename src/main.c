#include <stdio.h>

#if defined(KDA_ALIF_E8)
#include "RTE_Components.h"
#include CMSIS_device_header
#include "retarget_init.h"

int main(void)
{
    if (stdout_init() == 0) {
        printf("Hello World!\r\n");
        fflush(stdout);
    }

    for (;;) {
        __WFE();
    }
}

#else
/* Keep the original host executable available for the CMake tests. */
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
#endif
