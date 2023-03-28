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
    uint64_t ts;
    spms_ring_get_back_ts(ring, &ts);
    uint32_t pos;
    spms_ring_get_pos_by_ts(ring, &pos, ts - 3);
    spms_ring_set_front_pos(ring, pos);
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
