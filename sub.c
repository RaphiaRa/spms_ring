#include "spms_ring.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main()
{
    spms_ring *ring;
    if (spms_ring_sub_create(&ring, "test_ring") != 0)
    {
        printf("spms_ring_sub_create failed");
        return -1;
    }
    while(1)
    {
        char buf[1024];
        int64_t len = 0;
        if ((len = spms_ring_read_msg(ring, buf, sizeof(buf))) > 0)
            printf("Msg: %.*s\n", (int)len, buf);
        usleep(1000);
    }
    return 0;
}
