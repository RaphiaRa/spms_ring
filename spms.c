#include "spms.h"
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdalign.h>

#ifdef __linux__
#define CV_USE_FUTEX 1
#elif __APPLE__
#define CV_USE_ULOCK 1
#endif

#ifdef CV_USE_FUTEX
#include <sys/syscall.h>
#include <time.h>
#include <linux/futex.h>
#include <limits.h>
#elif CV_USE_ULOCK
extern int __ulock_wait(uint32_t operation, void *addr, uint64_t value,
                        uint32_t timeout);
extern int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);
#endif

#define SPMS_RING_HEADER_LENGTH 128
#define SPMS_MSG_RING_HEADER_LENGTH 128
#define SPMS_BUF_RING_HEADER_LENGTH 128
#define SPMS_INIT_CODE 0x00000001
#define SPMS_MEM_FLAG_INIT 1 << 0

static struct spms_config g_default_config = {.buf_length = 1 << 20,
                                              .msg_entries = 1 << 10,
                                              .nonblocking = 0};

/** spms_msg **/

struct spms_msg
{
    uint8_t is_key;
    uint8_t is_nil;
    uint8_t ver;
    uint32_t len;
    uint64_t ts;
    uint64_t offset;
};

static void spms_msg_update(struct spms_msg *msg)
{
    uint8_t ver = ++msg->ver;
    __atomic_store(&msg->ver, &ver, __ATOMIC_RELAXED);
}

static void spms_msg_release(struct spms_msg *msg)
{
    uint8_t is_key = 0;
    uint8_t is_nil = 0;
    uint32_t len = 0;
    uint64_t ts = 0;
    uint64_t offset = 0;
    __atomic_store(&msg->is_key, &is_key, __ATOMIC_RELAXED);
    __atomic_store(&msg->is_nil, &is_nil, __ATOMIC_RELAXED);
    __atomic_store(&msg->len, &len, __ATOMIC_RELAXED);
    __atomic_store(&msg->ts, &ts, __ATOMIC_RELAXED);
    __atomic_store(&msg->offset, &offset, __ATOMIC_RELAXED);
    spms_msg_update(msg);
}

static uint8_t spms_msg_version(struct spms_msg *msg)
{
    uint8_t ver = 0;
    __atomic_load(&msg->ver, &ver, __ATOMIC_RELAXED);
    return ver;
}

/** spms_msg_ring **/

typedef struct spms_msg_ring
{
    uint32_t entries;
    uint32_t mask;
    uint32_t head;
    uint32_t tail;
    uint32_t last_key;
} spms_msg_ring;

/** spms_msg_ring_mem functions **/

static uint64_t spms_msg_ring_mem_size(size_t msg_count)
{
    return msg_count * sizeof(struct spms_msg) + SPMS_MSG_RING_HEADER_LENGTH;
}

static int32_t spms_msg_ring_mem_init(void *mem, size_t msg_count)
{
    // msg_count must be power of 2
    if ((msg_count & (msg_count - 1)) != 0)
        return -1;
    spms_msg_ring *ring = (spms_msg_ring *)mem;
    ring->entries = msg_count;
    ring->mask = ring->entries - 1;
    ring->head = 0;
    ring->tail = 0;
    ring->last_key = 0;
    void *buf = (void *)((uintptr_t)mem + SPMS_MSG_RING_HEADER_LENGTH);
    memset(buf, 0, msg_count * sizeof(struct spms_msg));
    return 0;
}

/** spms_buf_ring **/

typedef struct spms_buf_ring
{
    uint64_t length;
    uint64_t mask;
    uint64_t head;
    uint64_t tail;
} spms_buf_ring;

/** spms_buf_ring_mem functions **/

static uint64_t spms_buf_ring_mem_size(size_t size)
{
    return size + SPMS_BUF_RING_HEADER_LENGTH;
}

static int32_t spms_buf_ring_mem_init(void *mem, size_t size)
{
    // size must be power of 2
    if ((size & (size - 1)) != 0)
        return -1;
    spms_buf_ring *ring = (spms_buf_ring *)mem;
    ring->length = size;
    ring->mask = ring->length - 1;
    ring->head = 0;
    ring->tail = 0;
    return 0;
}

/** spms_buf_ring functions **/

static void spms_buf_ring_release_buffer(struct spms_buf_ring *ring, uint64_t offset, uint32_t len)
{
    assert((ring->head & ring->mask) == offset && "Released buffer must match ring");
    ring->head += (uint64_t)len;
}

static uint64_t spms_buf_ring_aquire_buffer(struct spms_buf_ring *ring, uint32_t *len)
{
    uint64_t avail = ring->length - (ring->tail - ring->head);
    uint64_t index = ring->tail & ring->mask;
    uint64_t trail = ring->length - index;
    uint64_t writeable = avail < trail ? avail : trail;
    *len = (uint32_t)((uint64_t)*len < writeable ? (uint64_t)*len : writeable);
    return index;
}

/** spms ring functions **/

/** spms_ring public **/

typedef struct spms_ring
{
    uint64_t init_code;
    uint64_t msg_ring_offset;
    uint64_t buf_ring_offset;
    int8_t nonblocking;
} spms_ring;

uint64_t spms_ring_mem_needed_size(struct spms_config *config)
{
    if (config == NULL)
        config = &g_default_config;

    uint64_t msg_ring_size = spms_msg_ring_mem_size(config->msg_entries);
    uint64_t buf_ring_size = spms_buf_ring_mem_size(config->buf_length);
    return msg_ring_size + buf_ring_size + SPMS_BUF_RING_HEADER_LENGTH;
}

int32_t spms_ring_mem_init(void *mem, struct spms_config *config)
{
    if ((uintptr_t)mem % alignof(max_align_t) != 0)
        return -1;

    spms_ring *ring = (spms_ring *)mem;
    if (ring->init_code == SPMS_INIT_CODE)
        return 0;

    if (config == NULL)
        config = &g_default_config;

    int32_t ret = 0;
    ring->nonblocking = config->nonblocking;
    ring->msg_ring_offset = SPMS_RING_HEADER_LENGTH;
    ring->buf_ring_offset = ring->msg_ring_offset + spms_msg_ring_mem_size(config->msg_entries);
    if ((ret = spms_msg_ring_mem_init((void *)((uint8_t *)mem + ring->msg_ring_offset), config->msg_entries)) != 0)
        return ret;
    if ((ret = spms_buf_ring_mem_init((void *)((uint8_t *)mem + ring->buf_ring_offset), config->buf_length)) != 0)
        return ret;
    ring->init_code = SPMS_INIT_CODE;
    return 0;
}

/**
 * @brief spms publisher, writes messages to the ring
 */

struct spms_pub
{
    spms_msg_ring *msg_ring;
    struct spms_msg *msgs;
    spms_buf_ring *buf_ring;
    void *buf;
    int8_t nonblocking;
};

/** spms_pub private functions */

static void spms_pub_release_msg(spms_pub *ring, struct spms_msg *msg)
{
    spms_buf_ring_release_buffer(ring->buf_ring, msg->offset, msg->len);
    spms_msg_release(msg);
}

static void spms_pub_release_msg_from_head(spms_pub *ring)
{
    // don't need to use atomics here, we're the only ones modifying head
    uint32_t index = ring->msg_ring->head & ring->msg_ring->mask;
    spms_pub_release_msg(ring, &ring->msgs[index]);
    ++(ring->msg_ring->head);
}

static void spms_pub_ensure_buffer_space(spms_pub *ring, size_t len)
{
    uint32_t n = 0;
    uint64_t capacity = ring->buf_ring->length;
    while (capacity - (ring->buf_ring->tail - ring->buf_ring->head) < len)
    {
        spms_pub_release_msg_from_head(ring);
    }
}

static void spms_pub_flush_write_buffer_ex(spms_pub *ring, uint64_t offset, size_t len, const struct spms_msg_info *info)
{
    uint32_t tail = ring->msg_ring->tail;
    uint32_t idx = tail & ring->msg_ring->mask;
    struct spms_msg *msg = &ring->msgs[idx];
    if (msg->len)
    {
        spms_pub_release_msg(ring, msg);
    }
    assert((ring->buf_ring->tail & ring->buf_ring->mask) == offset && "Flushed buffer must match ring");
    ring->buf_ring->tail += (uint64_t)len;
    msg->offset = offset;
    msg->len = (uint32_t)len;
    if (info)
    {
        msg->ts = info->ts;
        msg->is_key = info->is_key;
        if (info->is_key)
            __atomic_store(&ring->msg_ring->last_key, &tail, __ATOMIC_RELAXED);
    }
    else
    {
        msg->is_nil = 1;
    }
    ++tail;
    __atomic_store(&ring->msg_ring->tail, &tail, __ATOMIC_RELEASE);
    if (!ring->nonblocking)
    {
#ifdef CV_USE_FUTEX
        syscall(SYS_futex, &ring->msg_ring->tail, FUTEX_WAKE, INT_MAX, NULL);
#elif CV_USE_ULOCK
        __ulock_wake(0x00000100, &ring->msg_ring->tail, 0);
#endif
    }
}

/** spms_pub public interface **/

int32_t spms_pub_create(spms_pub **out, void *mem, struct spms_config *config)
{
    if ((uintptr_t)mem % alignof(max_align_t) != 0)
        return -1;

    spms_pub *p = (spms_pub *)calloc(1, sizeof(spms_pub));
    if (!p)
        return -1;

    spms_ring *ring = (spms_ring *)mem;
    if (ring->init_code != SPMS_INIT_CODE) // first time initialization
    {
        int32_t ret = 0;
        if ((ret = spms_ring_mem_init(mem, config)) < 0)
        {
            free(p);
            return ret;
        }
    }
    p->nonblocking = ring->nonblocking;
    p->msg_ring = (spms_msg_ring *)((uint8_t *)mem + ring->msg_ring_offset);
    p->msgs = (struct spms_msg *)((uint8_t *)mem + ring->msg_ring_offset + SPMS_MSG_RING_HEADER_LENGTH);
    p->buf_ring = (spms_buf_ring *)((uint8_t *)mem + ring->buf_ring_offset);
    p->buf = (uint8_t *)mem + ring->buf_ring_offset + SPMS_BUF_RING_HEADER_LENGTH;
    *out = p;
    return 0;
}

int32_t spms_pub_get_write_buf(spms_pub *ring, void **addr, size_t len)
{
    while (1) // try until we get a suitable buffer
    {
        spms_pub_ensure_buffer_space(ring, len);
        uint32_t buf_len = (uint32_t)len;
        uint64_t offset = spms_buf_ring_aquire_buffer(ring->buf_ring, &buf_len);
        if (buf_len >= len) // got a suitable buffer
        {
            *addr = (uint8_t *)ring->buf + offset;
            return 0;
        }
        spms_pub_flush_write_buffer_ex(ring, offset, buf_len, NULL);
    }
}

int32_t spms_pub_flush_write_buf(spms_pub *ring, void *addr, size_t len, const struct spms_msg_info *info)
{
    struct spms_msg_info default_info = {0, 0};
    if (!info)
        info = &default_info;
    spms_pub_flush_write_buffer_ex(ring, (uint8_t *)addr - (uint8_t *)ring->buf, len, info);
    return 0;
}

int32_t spms_pub_write_msg(spms_pub *ring, const void *addr, size_t len, const struct spms_msg_info *info)
{
    int32_t ret = 0;
    void *ptr = NULL;
    if ((ret = spms_pub_get_write_buf(ring, &ptr, len)) < 0)
        return ret;
    memcpy(ptr, addr, len);
    spms_pub_flush_write_buf(ring, ptr, len, info);
    return 0;
}

void spms_pub_free(spms_pub *ring)
{
    free(ring);
}

/**
 * @brief spms subscriber, reads messages from the ring
 */

struct spms_sub
{
    spms_msg_ring *msg_ring;
    struct spms_msg *msgs;
    spms_buf_ring *buf_ring;
    void *buf;
    uint32_t head;
    uint32_t dropped;
    uint32_t safe_zone;
    int8_t nonblocking;
};

int32_t spms_sub_create(spms_sub **out, void *mem)
{
    if ((uintptr_t)mem % alignof(max_align_t) != 0)
        return -1;

    spms_ring *ring = (spms_ring *)mem;
    if (ring->init_code != SPMS_INIT_CODE) // not initlized
        return -1;

    spms_sub *p = (spms_sub *)calloc(1, sizeof(spms_sub));
    if (!p)
        return -1;

    p->nonblocking = ring->nonblocking;
    p->msg_ring = (spms_msg_ring *)((uint8_t *)mem + ring->msg_ring_offset);
    p->msgs = (struct spms_msg *)((uint8_t *)mem + ring->msg_ring_offset + SPMS_MSG_RING_HEADER_LENGTH);
    p->buf_ring = (spms_buf_ring *)((uint8_t *)mem + ring->buf_ring_offset);
    p->buf = (struct spms_buf *)((uint8_t *)mem + ring->buf_ring_offset + SPMS_BUF_RING_HEADER_LENGTH);
    p->safe_zone = p->msg_ring->entries - p->msg_ring->entries / 16;
    spms_sub_pos_rewind(p);
    *out = p;
    return 0;
}

void spms_sub_free(spms_sub *ring)
{
    free(ring);
}

int32_t spms_sub_get_dropped_count(spms_sub *sub, uint64_t *count)
{
    *count = sub->dropped;
    return 0;
}

int32_t spms_sub_pos_rewind(spms_sub *ring)
{
    uint32_t tail;
    __atomic_load(&ring->msg_ring->tail, &tail, __ATOMIC_RELAXED);
    ring->head = tail;
    return 0;
}

int32_t spms_sub_get_pos_by_ts(spms_sub *ring, uint32_t *pos, uint64_t ts)
{
    uint32_t head, tail;
    __atomic_load(&ring->msg_ring->tail, &tail, __ATOMIC_RELAXED);
    head = tail - ring->msg_ring->entries;
    while (head != tail)
    {
        struct spms_msg *a = &ring->msgs[(tail - 2) & ring->msg_ring->mask];
        struct spms_msg *b = &ring->msgs[(tail - 1) & ring->msg_ring->mask];

        if (a->ts <= ts && b->ts >= ts)
        {
            *pos = tail;
            return 0;
        }
        --tail;
    }
    return -1;
}

int32_t spms_sub_get_latest_ts(spms_sub *ring, uint64_t *ts)
{
    uint32_t tail;
    __atomic_load(&ring->msg_ring->tail, &tail, __ATOMIC_ACQUIRE);
    struct spms_msg *msg = &ring->msgs[(tail - 1) & ring->msg_ring->mask];
    __atomic_load(&msg->ts, ts, __ATOMIC_RELAXED);
    return 0;
}

int32_t spms_sub_get_latest_pos(spms_sub *ring, uint32_t *pos)
{
    uint32_t tail;
    __atomic_load(&ring->msg_ring->tail, &tail, __ATOMIC_RELAXED);
    *pos = (tail - 1);
    return 0;
}

int32_t spms_sub_get_latest_key_pos(spms_sub *sub, uint32_t *pos)
{
    uint32_t tail;
    __atomic_load(&sub->msg_ring->tail, &tail, __ATOMIC_ACQUIRE);
    __atomic_load(&sub->msg_ring->last_key, pos, __ATOMIC_RELAXED);
    if (*pos >= tail)
        return -1;
    return 0;
}

int32_t spms_sub_get_pos(spms_sub *ring, uint32_t *pos)
{
    *pos = ring->head;
    return 0;
}

int32_t spms_sub_set_pos(spms_sub *ring, uint32_t pos)
{
    ring->head = pos;
    return 0;
}

int32_t spms_sub_get_read_buf(spms_sub *ring, const void **out_addr, size_t *out_len, struct spms_msg_info *info, uint32_t timeout_ms)
{
    while (1)
    {
        uint32_t tail, idx;
        __atomic_load(&ring->msg_ring->tail, &tail, __ATOMIC_ACQUIRE);
        if (tail - ring->head > ring->safe_zone) // ensure safe zone
        {
            uint32_t shift = (tail - ring->head) - ring->safe_zone;
            ring->dropped += shift;
            ring->head += shift;
        }

        // skip nil packets
        while (ring->head < tail && (&ring->msgs[ring->head & ring->msg_ring->mask])->is_nil != 0)
        {
            ++ring->head;
        }
        if (ring->head == tail)
        {
            if (ring->nonblocking || timeout_ms == 0)
                return -1;
#ifdef CV_USE_FUTEX
            struct timespec ts;
            uint32_t seconds = timeout_ms / 1000;
            ts.tv_nsec = (timeout_ms - seconds * 1000) * 1000000;
            ts.tv_sec = seconds;
            syscall(SYS_futex, &ring->msg_ring->tail, FUTEX_WAIT, tail, &ts);
#elif CV_USE_ULOCK
            __ulock_wait(1, &ring->msg_ring->tail, tail, timeout_ms * 1000);
#endif
            timeout_ms = 0; // don't wait again
            continue;
        }
        struct spms_msg *msg = &ring->msgs[ring->head & ring->msg_ring->mask];
        uint32_t len = 0;
        uint8_t ver = 0;
        uint64_t offset = 0;

        __atomic_load(&msg->ver, &ver, __ATOMIC_RELAXED);
        __atomic_load(&msg->offset, &offset, __ATOMIC_RELAXED);
        __atomic_load(&msg->len, &len, __ATOMIC_RELAXED);
        if (info)
        {
            __atomic_load(&msg->is_key, &info->is_key, __ATOMIC_RELAXED);
            __atomic_load(&msg->ts, &info->ts, __ATOMIC_RELAXED);
        }
        *out_addr = (uint8_t *)ring->buf + offset;
        *out_len = len;
        return ver;
    }
}

int32_t spms_sub_finalize_read_buf(spms_sub *ring, int32_t version)
{
    struct spms_msg *msg = &ring->msgs[ring->head & ring->msg_ring->mask];
    uint8_t ver = 0;
    uint32_t tail = 0;

    // we don't really need the tail, but we need to sync with the writer
    __atomic_load(&ring->msg_ring->tail, &tail, __ATOMIC_ACQUIRE);
    __atomic_load(&msg->ver, &ver, __ATOMIC_RELAXED);
    ++ring->head;
    return (int32_t)ver == version ? 0 : -1;
}

int32_t spms_sub_read_msg(spms_sub *ring, void *addr, size_t *len, struct spms_msg_info *info, uint32_t timeout_ms)
{
    while (1)
    {
        const void *ptr = NULL;
        size_t ptr_len = 0;
        int32_t ver = spms_sub_get_read_buf(ring, &ptr, &ptr_len, info, timeout_ms);
        if (ver < 0 || ptr_len > *len)
            return -1;

        memcpy(addr, ptr, ptr_len);
        if (spms_sub_finalize_read_buf(ring, ver) == 0)
        {
            *len = ptr_len;
            return 0;
        }
    }
}
