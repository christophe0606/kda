#include "RTE_Components.h"
#include CMSIS_device_header

int main(void)
{
    /* Both cores boot; only HP owns the console and application. */
    for (;;) {
        __WFE();
    }
}
