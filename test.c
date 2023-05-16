#include "spms.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>

#define TEST(x)                                                        \
    if (x == 0)                                                        \
    {                                                                  \
        printf("Test failed: %s, at %s:%d\n", #x, __FILE__, __LINE__); \
        return -1;                                                     \
    }

static int test_spms_pub_ctor_dtor()
{
    {
        spms_pub *pub;
        TEST(spms_pub_create(&pub, "spms_ring_unit_test", NULL, 0) == 0);
        spms_pub_free(pub);
    }
    {
        spms_pub *pub;
        struct spms_config config = {1024, 1024};
        TEST(spms_pub_create(&pub, "spms_ring_unit_test", &config, 0) == 0);
        spms_pub_free(pub);
    }
    return 0;
}

static int test_spms_sub_ctor_dtor()
{
    {
        spms_sub *sub;
        TEST(spms_sub_create(&sub, "spms_ring_unit_test") != 0);
    }
    {
        spms_sub *sub;
        spms_pub *pub;
        TEST(spms_pub_create(&pub, "spms_ring_unit_test", NULL, 0) == 0);
        TEST(spms_sub_create(&sub, "spms_ring_unit_test") == 0);
        spms_sub_free(sub);
        spms_pub_free(pub);
    }
    return 0;
}

static const unsigned char test_sequence[] = {'t', 'e', 's', 't', 's', 'e', 'q', 'u', 'e', 'n', 'c', 'e'};
static const size_t test_blocks_per_msg[] = {1, 20, 256, 490, 1024, 2040, 5500};
static const size_t test_packet_count = 10 * 1000;

static int test_write(spms_pub *pub)
{
    for (int i = 0; i < test_packet_count; i++)
    {
        void *addr;
        size_t blocks = test_blocks_per_msg[i % (sizeof(test_blocks_per_msg) / sizeof(test_blocks_per_msg[0]))];
        TEST(spms_pub_get_write_buf(pub, &addr, blocks * sizeof(test_sequence)) == 0);
        for (int j = 0; j < blocks; j++)
            memcpy(addr + j * sizeof(test_sequence), test_sequence, sizeof(test_sequence));
        TEST(spms_pub_flush_write_buf_with_info(pub, addr, blocks * sizeof(test_sequence), NULL) == 0);
        usleep(100);
    }
    spms_pub_free(pub);
    return 0;
}

static int test_read()
{
    spms_sub *sub;
    TEST(spms_sub_create(&sub, "spms_ring_unit_test") == 0);
    uint32_t pos = 0;
    TEST(spms_sub_get_latest_pos(sub, &pos) == 0);
    spms_sub_set_pos(sub, pos);
    size_t count = pos;
    for (int i = 0; i < test_packet_count; i++)
    {
        char buf[128 * 1024];
        size_t len = sizeof(buf);
        while (spms_sub_read_msg(sub, buf, &len, 0) == -1)
        {
            uint64_t dropped = 0;
            spms_sub_get_dropped_count(sub, &dropped);
            if (count + dropped == test_packet_count)
            {
                spms_sub_free(sub);
                return 0;
            }
            usleep(10);
        }

        for (int j = 0; j < len; j++)
            TEST(buf[j] == test_sequence[j % sizeof(test_sequence)]);
        ++count;
    }
    spms_sub_free(sub);
    return 0;
}

static void *test_read_thread(void *result)
{
    *(int *)result = test_read();
    return NULL;
}

static int test_spms_read_write_consistency()
{
    pthread_t write_thread;
    pthread_t read_threads[4];
    int read_results[4] = {0};

    spms_pub *pub;
    TEST(spms_pub_create(&pub, "spms_ring_unit_test", NULL, 0) == 0);

    // start writers
    for (int i = 0; i < 4; i++)
        pthread_create(&read_threads[i], NULL, test_read_thread, &read_results[i]);

    TEST(test_write(pub) == 0);

    for (int i = 0; i < 4; i++)
        pthread_join(read_threads[i], NULL);
    for (int i = 0; i < 4; i++)
        TEST(read_results[i] == 0);
    return 0;
}

int main()
{
    TEST(test_spms_pub_ctor_dtor() == 0);
    TEST(test_spms_sub_ctor_dtor() == 0);
    TEST(test_spms_read_write_consistency() == 0);
    printf("All tests passed\n");
    return 0;
}
