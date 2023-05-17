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
        void *buffer = calloc(spms_ring_mem_needed_size(NULL), 1);
        TEST(spms_pub_create(&pub, buffer, NULL) == 0);
        spms_pub_free(pub);
        free(buffer);
    }
    {
        spms_pub *pub;
        struct spms_config config = {.buf_length = 1024, .msg_entries = 1024, .nonblocking = 0};
        void *buffer = calloc(spms_ring_mem_needed_size(&config), 1);
        TEST(spms_pub_create(&pub, buffer, &config) == 0);
        spms_pub_free(pub);
        free(buffer);
    }
    return 0;
}

static int test_spms_sub_ctor_dtor()
{
    {
        spms_sub *sub;
        void *buffer = calloc(spms_ring_mem_needed_size(NULL), 1);
        TEST(spms_sub_create(&sub, buffer) != 0);
        free(buffer);
    }
    {
        spms_sub *sub;
        spms_pub *pub;
        void *buffer = calloc(spms_ring_mem_needed_size(NULL), 1);
        TEST(spms_pub_create(&pub, buffer, NULL) == 0);
        TEST(spms_sub_create(&sub, buffer) == 0);
        spms_sub_free(sub);
        spms_pub_free(pub);
        free(buffer);
    }
    return 0;
}

struct read_thread_args
{
    void *buf;
    int result;
};

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

static int test_read(void *buf)
{
    spms_sub *sub;
    TEST(spms_sub_create(&sub, buf) == 0);
    uint32_t pos = 0;
    TEST(spms_sub_get_latest_pos(sub, &pos) == 0);
    spms_sub_set_pos(sub, pos);
    size_t count = pos;
    for (int i = 0; i < test_packet_count; i++)
    {
        char buf[128 * 1024];
        size_t len = sizeof(buf);
        while (spms_sub_read_msg(sub, buf, &len, 10) == -1)
        {
            uint64_t dropped = 0;
            spms_sub_get_dropped_count(sub, &dropped);
            if (count + dropped == test_packet_count)
            {
                spms_sub_free(sub);
                return 0;
            }
        }

        for (int j = 0; j < len; j++)
            TEST(buf[j] == test_sequence[j % sizeof(test_sequence)]);
        ++count;
    }
    spms_sub_free(sub);
    return 0;
}

static void *test_read_thread(void *args)
{
    struct read_thread_args *a = args;
    a->result = test_read(a->buf);
    return NULL;
}

static int test_spms_read_write_consistency()
{
    pthread_t write_thread;
    pthread_t read_threads[4];
    struct read_thread_args args[4] = {0};

    spms_pub *pub;
    void *buf = calloc(1, 4 * 1024 * 1024);
    TEST(spms_pub_create(&pub, buf, NULL) == 0);

    // start readers
    for (int i = 0; i < 4; i++)
    {
        args[i].buf = buf;
        pthread_create(&read_threads[i], NULL, test_read_thread, &args[i]);
    }

    TEST(test_write(pub) == 0);

    for (int i = 0; i < 4; i++)
        pthread_join(read_threads[i], NULL);
    for (int i = 0; i < 4; i++)
        TEST(args[i].result == 0);
    free(buf);
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
