#ifdef __linux__
#define _GNU_SOURCE
#endif

#include "spms.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <stdalign.h>

#define TEST(x)                                                        \
    if ((x) == 0)                                                      \
    {                                                                  \
        printf("Test failed: %s, at %s:%d\n", #x, __FILE__, __LINE__); \
        return -1;                                                     \
    }

#define ALIGNED __attribute__((aligned(alignof(max_align_t))))

/** Publisher Constructor Test **/

static int test_spms_pub_ctor_dtor(void)
{
    {
        spms_pub *pub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_pub_create(&pub, buffer, NULL) == 0);
        spms_pub_free(pub);
    }
    {
        spms_pub *pub;
        struct spms_config config = {.buf_length = 1024, .msg_entries = 1024, .nonblocking = 0};
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_pub_create(&pub, buffer, &config) == SPMS_ERR_OK);
        spms_pub_free(pub);
    }
    return 0;
}

/** Subscriber Constructor Test **/

static int test_spms_sub_ctor_dtor(void)
{
    {
        spms_sub *sub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_sub_create(&sub, buffer) == SPMS_ERR_INVALID_ARG);
    }
    {
        spms_sub *sub;
        spms_pub *pub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_pub_create(&pub, buffer, NULL) == SPMS_ERR_OK);
        TEST(spms_sub_create(&sub, buffer) == SPMS_ERR_OK);
        spms_sub_free(sub);
        spms_pub_free(pub);
    }
    return 0;
}

/** Read-Write Thread Test **/
/* This test writes messages to a publisher and reads them from multiple subscriber threads */
/* The messages are enumberated and the readers check if the messages are in order and correct */

struct read_thread_args
{
    void *buf;
    int result;
};

static const char *test_data[] = {
    "tsRdPu3Dyj45psPXri0zn49UccZ11VY1koNe0ytMmAmT305G4QvQSse8jMACI1ki8jYUPizD9EWPtDFBEZTYZvTxkfkzcmDhhbAwPbtxaWyzdRfcysUr7SkTGnozpwo0",
    "567iilQ9VKEjLIDZHNODK5tR08IJaJNEsVCxPxWkp9RS9SeGlyBoxTrjCExy5wR2gt1akw4KFejjRta1YrrPrxUws29JPVDORbDtsdfrwCWO6eNfDBrjUdw1cRhERZYe",
    "s0F3DRzIS7RVZiVjYb94VoPhY4IiG2J85xXjWeo7Qz3n58c3KL5CL5XsDrj2U4eYFQfzQO6lDh1Uz7ry0TRzbOkUNYS8uGxXES1vmAH2rlR4drLWt7wKQzvVO2pgyPWT",
    "1f4rCB4b52xNxxEJJUZ7BJOKeE3vNBv8hTaBQ4WmWHH6SFFUNDfv4rQIbkLTajCeieZgOoHtpsST18o1G8j1Fdktd2xxG58vZp8vaP4BqXUIxuwOzyhcgDEMZeX8gkaF",
    "FfM78jwpUvZQqtR3vwZioRpVKtC4YjHmnBHsx6AqiGn7EWufXGkc8Tn5qnH0FSh7HZFGy0EfagnA4WyLpHf89BTyYYd1zNvSXYcmKvjW5VpwrPIoQrIapK0832mRT3Do",
    "qazMrT1oqraUfR7jD5ZPPfg5G2htVpRZfy7HeBoc9ko1qq48SLm1NYGCfvFJzszD8C6FNDgEsytXLRnpjcf9LtNolVPM94YCp63YcKk6dI0fDqRLvgjYUoj9xI9r9ch8",
    "jm55odcpwlTRc5Y6nRszc4AVyFFIlnZmiQo2jc3ytygNAssf3vrH9kupdFvklqlljPY1LUxRv3glNQU6zfp4klatwrLVpLpyYjto1Uw0LH3uuZ5cDwq5cEh2eRPxgJrm",
    "PnG92uklC58Dj36HDOb38ExvlpONtuZPdmkQNaHXhHBDtta2bsnHIUl9MsAm18CnTB6rRhS6OvDJXaRX1KQ8Sdbkl138Ch7uA5fx1Q65M0SmKSSikoAJOeZAVbN48brv",
    "S407JP4yCjX4NrzCT8skkrwCmzBS6GNF4AsqdaKhawb09MQOOsJfvbIDEuJFiCAJQ63uA669L0WRqmvmOlCFyBeyU8PibMO2pIyO2P22R53CFzzRegduRz95oyrqb8xf",
    "OXbtNNxNk2biFPXxkngmp7idFQ15w8MsgG9NsOURjfbmsNF1xmewdgjJ9bzAAhoD2lr1QZZbp26gRFZ0WoO61wOLsjlpU1QpMb4FaQ9NyLQQC1TgQr9OSVFnYItNHRrQ",
};
static const size_t test_data_count = sizeof(test_data) / sizeof(test_data[0]);
static const size_t test_packet_count = 4 * 1024;

static int test_write(spms_pub *pub)
{
    for (size_t i = 0; i < test_packet_count; i++)
    {
        struct spms_msg_info info = {0};
        info.ts = i;
        const char *addr = test_data[i % test_data_count];
        TEST(spms_pub_write_msg(pub, addr, strlen(addr), &info) == SPMS_ERR_OK);
    }
    // send a NULL message to indicate end of messages
    TEST(spms_pub_write_msg(pub, NULL, 0, NULL) == SPMS_ERR_OK);
    return 0;
}

static int test_read(void *buf)
{
    spms_sub *sub;
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);
    uint64_t last_ts = 0;
    while (1)
    {
        char buf[1024];
        size_t len = sizeof(buf);
        struct spms_msg_info info = {0};
        TEST(spms_sub_read_msg(sub, buf, &len, &info, 1000) == SPMS_ERR_OK);
        if (len == 0)
            break;
        TEST(last_ts == 0 || info.ts == last_ts + 1);
        last_ts = info.ts;
        TEST(memcmp(test_data[info.ts % test_data_count], buf, len) == 0);
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

#define TEST_THREAD_COUNT 4
static int test_spms_read_write_consistency(void)
{
    pthread_t read_threads[TEST_THREAD_COUNT];
    struct read_thread_args args[TEST_THREAD_COUNT] = {0};
    spms_pub *pub;
    struct spms_config config = {.buf_length = 1 * 1024 * 1024, .msg_entries = test_packet_count, .nonblocking = 0};
    size_t buf_size = 0;
    spms_ring_mem_needed_size(&config, &buf_size);
    uint8_t *buf = calloc(1, buf_size);
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);

    // start readers
    for (int i = 0; i < TEST_THREAD_COUNT; i++)
    {
        args[i].buf = buf;
        pthread_create(&read_threads[i], NULL, test_read_thread, &args[i]);
    }

    TEST(test_write(pub) == SPMS_ERR_OK);
    for (int i = 0; i < 4; i++)
        pthread_join(read_threads[i], NULL);
    for (int i = 0; i < 4; i++)
        TEST(args[i].result == SPMS_ERR_OK);
    spms_pub_free(pub);
    free(buf);
    return 0;
}

static int test_spms_readv_writev(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    // write
    {
        struct spms_ovec iov[2] = {{.addr = "test1", .len = 4}, {.addr = "test2", .len = 4}};
        TEST(spms_pub_writev_msg(pub, iov, 2, NULL) == SPMS_ERR_OK);
    }

    // read
    {
        char buf1[4], buf2[4];
        struct spms_ivec iov[2] = {{.addr = buf1, .len = 4}, {.addr = buf2, .len = 4}};
        size_t len = 2;
        TEST(spms_sub_readv_msg(sub, iov, &len, NULL, 1000) == SPMS_ERR_OK);
        TEST(len == 2);
        TEST(iov[0].len == 4);
        TEST(iov[1].len == 4);
        TEST(memcmp(iov[0].addr, "test1", 4) == 0);
        TEST(memcmp(iov[1].addr, "test2", 4) == 0);
    }
    spms_pub_free(pub);
    spms_sub_free(sub);
    return 0;
}

static int test_spms_empty_read(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);
    // read
    {
        char buffer[128];
        size_t len = sizeof(buffer);
        TEST(spms_sub_read_msg(sub, buffer, &len, NULL, 0) == SPMS_ERR_TIMEOUT);
    }
    spms_pub_free(pub);
    spms_sub_free(sub);
    return 0;
}

static int test_spms_read_exceeding_pos(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);
    { // write
        TEST(spms_pub_write_msg(pub, "test1", 5, NULL) == SPMS_ERR_OK);
        TEST(spms_pub_write_msg(pub, "test2", 5, NULL) == SPMS_ERR_OK);
        TEST(spms_pub_write_msg(pub, "test3", 5, NULL) == SPMS_ERR_OK);
        TEST(spms_pub_write_msg(pub, "test4", 5, NULL) == SPMS_ERR_OK);
    }
    { // read at an position exceeding the total number of messages will read the latest message
        spms_sub_set_pos(sub, 100);
        char buffer[128];
        size_t len = sizeof(buffer);
        TEST(spms_sub_read_msg(sub, buffer, &len, NULL, 0) == SPMS_ERR_OK);
        TEST(strncmp(buffer, "test4", 5) == 0);
    }
    spms_pub_free(pub);
    spms_sub_free(sub);
    return 0;
}

static int test_spms_read_exceeding_pos_empty(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);
    { // read at an position exceeding when no messages are available will return SPMS_ERR_TIMEOUT
        spms_sub_set_pos(sub, 100);
        char buffer[128];
        size_t len = sizeof(buffer);
        TEST(spms_sub_read_msg(sub, buffer, &len, NULL, 0) == SPMS_ERR_TIMEOUT)
    }
    spms_pub_free(pub);
    spms_sub_free(sub);
    return 0;
}

static int test_spms_empty_readv(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);
    { // read
        char buffer[128];
        struct spms_ivec iov[2] = {{.addr = buffer, .len = sizeof(buffer)}};
        size_t len = 1;
        TEST(spms_sub_readv_msg(sub, iov, &len, NULL, 0) == SPMS_ERR_TIMEOUT);
    }
    spms_pub_free(pub);
    spms_sub_free(sub);
    return 0;
}

static int test_spms_get_next_key_pos(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[8 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 4, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // write
        const char *msg = "test";
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), NULL) == SPMS_ERR_OK);
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), NULL) == SPMS_ERR_OK);
        struct spms_msg_info info = {0};
        info.is_key = 1;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
    }
    { // next key pos should be 2
        uint32_t pos = 0;
        TEST(spms_sub_get_next_key_pos(sub, &pos) == 0);
        TEST(pos == 2);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_next_key_pos_empty(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[8 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 4, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);
    { // can't find next key pos
        uint32_t pos = 0;
        TEST(spms_sub_get_next_key_pos(sub, &pos) == SPMS_ERR_NOT_AVAILABLE);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_next_key_pos_invalid_pos(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[8 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 4, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // write and overwrite first msg
        TEST(spms_pub_write_msg(pub, "test1", 5, NULL) == SPMS_ERR_OK);
        TEST(spms_pub_write_msg(pub, "test2", 5, NULL) == SPMS_ERR_OK);
        struct spms_msg_info info = {0};
        info.is_key = 1;
        TEST(spms_pub_write_msg(pub, "test3", 5, &info) == SPMS_ERR_OK);
        TEST(spms_pub_write_msg(pub, "test4", 5, NULL) == SPMS_ERR_OK);
        TEST(spms_pub_write_msg(pub, "test5", 5, NULL) == SPMS_ERR_OK);
    }
    { // invalid frames should be ignored and next key pos should be 2
        uint32_t pos = 0;
        TEST(spms_sub_get_next_key_pos(sub, &pos) == 0);
        TEST(pos == 2);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_pos_by_ts_empty(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // Searching for ts 10 should not find anything
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 10) == SPMS_ERR_NOT_AVAILABLE);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_pos_by_ts_single(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // write
        struct spms_msg_info info = {0};
        info.ts = 10;
        TEST(spms_pub_write_msg(pub, "test1", 5, &info) == SPMS_ERR_OK);
    }
    { // Searching for ts 15 should not find anything
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 15) == SPMS_ERR_NOT_AVAILABLE);
    }
    { // Searching for ts 10 should return pos 0
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 10) == 0);
        TEST(pos == 0);
    }
    { // Searching for ts 5 should return pos 0
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 5) == 0);
        TEST(pos == 0);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_pos_by_ts_multiple(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // write
        const char *msg = "test";
        struct spms_msg_info info = {0};
        info.ts = 10;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
        info.ts = 20;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
        info.ts = 30;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
    }
    { // Searching for ts 5 should return pos 0
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 5) == 0);
        TEST(pos == 0);
    }
    { // Searching for ts 10 should return pos 0
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 10) == 0);
        TEST(pos == 0);
    }
    { // Searching for ts 15 should return pos 1
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 15) == 0);
        TEST(pos == 1);
    }
    { // Searching for ts 30 should return pos 2
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 30) == 0);
        TEST(pos == 2);
    }
    { // Searching for ts 35 should not find anything
        uint32_t pos = 0;
        TEST(spms_sub_get_pos_by_ts(sub, &pos, 35) == SPMS_ERR_NOT_AVAILABLE);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_latest_ts_empty(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // Searching for latest ts should not find anything
        uint64_t ts = 0;
        TEST(spms_sub_get_latest_ts(sub, &ts) == SPMS_ERR_NOT_AVAILABLE);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_latest_ts_single(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // write
        const char *msg = "test";
        struct spms_msg_info info = {0};
        info.ts = 10;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
    }
    { // Searching for latest ts should return 10
        uint64_t ts = 0;
        TEST(spms_sub_get_latest_ts(sub, &ts) == 0);
        TEST(ts == 10);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_get_latest_ts_multiple(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // write
        const char *msg = "test";
        struct spms_msg_info info = {0};
        info.ts = 10;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
        info.ts = 20;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
        info.ts = 30;
        TEST(spms_pub_write_msg(pub, msg, strlen(msg), &info) == SPMS_ERR_OK);
    }
    { // Searching for latest ts should return 30
        uint64_t ts = 0;
        TEST(spms_sub_get_latest_ts(sub, &ts) == 0);
        TEST(ts == 30);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

static int test_spms_write_read_null(void)
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    { // write
        TEST(spms_pub_write_msg(pub, NULL, 0, NULL) == SPMS_ERR_OK);
    }
    { // read
        char buffer[128];
        size_t len = sizeof(buffer);
        struct spms_msg_info info = {0};
        TEST(spms_sub_read_msg(sub, buffer, &len, &info, 1000) == SPMS_ERR_OK);
        TEST(len == 0);
    }
    spms_sub_free(sub);
    spms_pub_free(pub);
    return 0;
}

int main(void)
{
    TEST(test_spms_pub_ctor_dtor() == 0);
    TEST(test_spms_sub_ctor_dtor() == 0);
    TEST(test_spms_read_write_consistency() == 0);
    TEST(test_spms_readv_writev() == 0);
    TEST(test_spms_empty_read() == 0);
    TEST(test_spms_read_exceeding_pos() == 0);
    TEST(test_spms_read_exceeding_pos_empty() == 0);
    TEST(test_spms_empty_readv() == 0);
    TEST(test_spms_get_next_key_pos() == 0);
    TEST(test_spms_get_next_key_pos_empty() == 0);
    TEST(test_spms_get_next_key_pos_invalid_pos() == 0);
    TEST(test_spms_get_pos_by_ts_empty() == 0);
    TEST(test_spms_get_pos_by_ts_single() == 0);
    TEST(test_spms_get_pos_by_ts_multiple() == 0);
    TEST(test_spms_get_latest_ts_empty() == 0);
    TEST(test_spms_get_latest_ts_single() == 0);
    TEST(test_spms_get_latest_ts_multiple() == 0);
    TEST(test_spms_write_read_null() == 0);
    printf("All tests passed\n");
    return 0;
}
