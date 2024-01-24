#ifdef __linux__
#define CV_USE_FUTEX 1
#define _GNU_SOURCE
#elif __APPLE__
#define CV_USE_ULOCK 1
#endif

#include "spms.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdalign.h>
#include <stdatomic.h>

#ifdef CV_USE_FUTEX
#include <sys/syscall.h>
#include <unistd.h>
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

#define atomic_u8 atomic_uint_least8_t
#define atomic_u32 atomic_uint_least32_t
#define atomic_u64 atomic_uint_least64_t

static struct spms_config g_default_config = {.buf_length = 1 << 20,
                                              .msg_entries = 1 << 10,
                                              .nonblocking = 0};

/** spms_msg **/

struct spms_msg
{
    atomic_u8 is_key;
    atomic_u8 is_nil;
    atomic_u32 len;
    atomic_u64 ts;
    atomic_u64 offset;
};

static void spms_msg_release(struct spms_msg *msg)
{
    uint8_t is_key = 0;
    uint8_t is_nil = 0;
    uint32_t len = 0;
    uint64_t ts = 0;
    uint64_t offset = 0;
    atomic_store_explicit(&msg->is_key, is_key, memory_order_relaxed);
    atomic_store_explicit(&msg->is_nil, is_nil, memory_order_relaxed);
    atomic_store_explicit(&msg->len, len, memory_order_relaxed);
    atomic_store_explicit(&msg->ts, ts, memory_order_relaxed);
    atomic_store_explicit(&msg->offset, offset, memory_order_relaxed);
}

/** spms_msg_ring **/

typedef struct spms_msg_ring
{
    atomic_u32 entries;
    atomic_u32 mask;
    atomic_u32 head;
    atomic_u32 tail;
    atomic_u32 last_key;
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
        return SPMS_ERR_INVALID_ARG;
    spms_msg_ring *ring = (spms_msg_ring *)mem;
    ring->entries = (uint32_t)msg_count;
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
    atomic_u64 length;
    atomic_u64 mask;
    atomic_u64 head;
    atomic_u64 tail;
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
        return SPMS_ERR_INVALID_ARG;
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
    (void)offset;
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
    uint8_t nonblocking;
} spms_ring;

spms_err spms_ring_mem_needed_size(struct spms_config *config, size_t *out)
{
    if (config == NULL)
        config = &g_default_config;

    uint64_t msg_ring_size = spms_msg_ring_mem_size(config->msg_entries);
    uint64_t buf_ring_size = spms_buf_ring_mem_size(config->buf_length);
    *out = msg_ring_size + buf_ring_size + SPMS_BUF_RING_HEADER_LENGTH;
    return SPMS_ERR_OK;
}

spms_err spms_ring_mem_init(void *mem, struct spms_config *config)
{
    // mem must be aligned to a multiple of alignof(max_align_t)
    if ((uintptr_t)mem % alignof(max_align_t) != 0)
        return SPMS_ERR_INVALID_ARG;

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
    return SPMS_ERR_OK;
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
    uint8_t nonblocking;
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
    uint32_t head = ring->msg_ring->head;
    uint32_t index = head & ring->msg_ring->mask;
    spms_pub_release_msg(ring, &ring->msgs[index]);
    ++head;
    atomic_store_explicit(&ring->msg_ring->head, head, memory_order_relaxed);
}

static void spms_pub_ensure_avail_buffer(spms_pub *ring, size_t len)
{
    uint64_t buffer_capacity = ring->buf_ring->length;
    uint64_t msg_capacity = ring->msg_ring->entries;
    while ((buffer_capacity - (ring->buf_ring->tail - ring->buf_ring->head) < len) || (msg_capacity == (ring->msg_ring->tail - ring->msg_ring->head)))
    {
        spms_pub_release_msg_from_head(ring);
    }
}

static void spms_pub_flush_write_buffer_ex(spms_pub *ring, uint64_t offset, size_t len, const struct spms_msg_info *info)
{
    uint32_t tail = ring->msg_ring->tail;
    uint32_t idx = tail & ring->msg_ring->mask;
    struct spms_msg *msg = &ring->msgs[idx];
    assert((msg->len == 0 || msg->is_nil) && "Message must be empty");
    assert((ring->buf_ring->tail & ring->buf_ring->mask) == offset && "Flushed buffer must match ring");
    ring->buf_ring->tail += (uint64_t)len;
    msg->offset = offset;
    msg->len = (uint32_t)len;
    if (info)
    {
        msg->ts = info->ts;
        msg->is_key = info->is_key;
        if (info->is_key)
            atomic_store_explicit(&ring->msg_ring->last_key, tail, memory_order_relaxed);
    }
    else
    {
        msg->is_nil = 1;
    }
    ++tail;
    atomic_store_explicit(&ring->msg_ring->tail, tail, __ATOMIC_RELEASE);
    if (!ring->nonblocking)
    {
#ifdef CV_USE_FUTEX
        syscall(SYS_futex, &ring->msg_ring->tail, FUTEX_WAKE, INT_MAX, NULL);
#elif CV_USE_ULOCK
        __ulock_wake(0x00000100, &ring->msg_ring->tail, 0);
#endif
    }
}

static spms_err pub_has_valid_setup(spms_pub *ring)
{
    uint32_t msg_tail = ring->msg_ring->tail;
    uint64_t buf_tail = ring->buf_ring->tail;
    uint64_t buf_head = ring->buf_ring->head;
    for (uint32_t msg_head = ring->msg_ring->head; msg_head < msg_tail; ++msg_head)
    {
        uint32_t idx = msg_head & ring->msg_ring->mask;
        struct spms_msg *msg = &ring->msgs[idx];
        if (!msg->is_nil)
        {
            uint64_t buf_offset = buf_head & ring->buf_ring->mask;
            if (msg->offset != buf_offset)
                return SPMS_ERR_INVALID_STATE;
            buf_head += msg->len;
            if (buf_head > buf_tail)
                return SPMS_ERR_INVALID_STATE;
        }
    }
    return SPMS_ERR_OK;
}

/** spms_pub public interface **/

spms_err spms_pub_create(spms_pub **out, void *mem, struct spms_config *config)
{
    if ((uintptr_t)mem % alignof(max_align_t) != 0)
        return SPMS_ERR_INVALID_ARG;

    spms_pub *p = (spms_pub *)calloc(1, sizeof(spms_pub));
    if (!p)
        return SPMS_ERR_OS;

    spms_err ret = 0;
    spms_ring *ring = (spms_ring *)mem;
    if (ring->init_code != SPMS_INIT_CODE) // first time initialization
    {
        if ((ret = spms_ring_mem_init(mem, config)) != SPMS_ERR_OK)
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
    if ((ret = pub_has_valid_setup(p)) < 0)
    {
        free(p);
        return ret;
    }
    *out = p;
    return SPMS_ERR_OK;
}

spms_err spms_pub_get_write_buf(spms_pub *ring, void **addr, size_t len)
{
    while (1) // try until we get a suitable buffer
    {
        spms_pub_ensure_avail_buffer(ring, len);
        uint32_t buf_len = (uint32_t)len;
        uint64_t offset = spms_buf_ring_aquire_buffer(ring->buf_ring, &buf_len);
        if (buf_len >= len) // got a suitable buffer
        {
            *addr = (uint8_t *)ring->buf + offset;
            return SPMS_ERR_OK;
        }
        spms_pub_flush_write_buffer_ex(ring, offset, buf_len, NULL);
    }
}

spms_err spms_pub_flush_write_buf(spms_pub *ring, void *addr, size_t len, const struct spms_msg_info *info)
{
    struct spms_msg_info default_info = {0, 0};
    if (!info)
        info = &default_info;
    spms_pub_flush_write_buffer_ex(ring, (uint64_t)((uint8_t *)addr - (uint8_t *)ring->buf), len, info);
    return SPMS_ERR_OK;
}

spms_err spms_pub_write_msg(spms_pub *ring, const void *addr, size_t len, const struct spms_msg_info *info)
{
    int32_t ret = 0;
    void *ptr = NULL;
    if ((ret = spms_pub_get_write_buf(ring, &ptr, len)) < 0)
        return ret;
    memcpy(ptr, addr, len);
    spms_pub_flush_write_buf(ring, ptr, len, info);
    return SPMS_ERR_OK;
}

spms_err spms_pub_writev_msg(spms_pub *pub, const struct spms_ovec *ovec, size_t len, const struct spms_msg_info *info)
{
    void *ptr = NULL;
    size_t total_len = 0;

    for (size_t i = 0; i < len; ++i)
        total_len += ovec[i].len;

    spms_err ret = SPMS_ERR_OK;
    if ((ret = spms_pub_get_write_buf(pub, &ptr, total_len)) < 0)
        return ret;
    size_t offset = 0;
    for (size_t i = 0; i < len; ++i)
    {
        memcpy((uint8_t *)ptr + offset, ovec[i].addr, ovec[i].len);
        offset += ovec[i].len;
    }
    spms_pub_flush_write_buf(pub, ptr, total_len, info);
    return SPMS_ERR_OK;
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
    uint8_t nonblocking;
    uint8_t ver;
};

static void ensure_valid_head(spms_sub *ring, uint32_t tail, uint32_t head);

static spms_err verify_pos(spms_sub *sub, uint32_t pos)
{
    uint32_t tail = atomic_load_explicit(&sub->msg_ring->tail, memory_order_acquire); // sync with producer
    uint32_t head = atomic_load_explicit(&sub->msg_ring->head, memory_order_relaxed);
    return head <= pos && pos < tail ? SPMS_ERR_OK : SPMS_ERR_INVALID_POS;
}

spms_err spms_sub_create(spms_sub **out, void *mem)
{
    if ((uintptr_t)mem % alignof(max_align_t) != 0)
        return SPMS_ERR_INVALID_ARG;

    spms_ring *ring = (spms_ring *)mem;
    if (ring->init_code != SPMS_INIT_CODE) // not initlized
        return SPMS_ERR_INVALID_ARG;

    spms_sub *p = (spms_sub *)calloc(1, sizeof(spms_sub));
    if (!p)
        return SPMS_ERR_OS;

    p->nonblocking = ring->nonblocking;
    p->msg_ring = (spms_msg_ring *)((uint8_t *)mem + ring->msg_ring_offset);
    p->msgs = (struct spms_msg *)((uint8_t *)mem + ring->msg_ring_offset + SPMS_MSG_RING_HEADER_LENGTH);
    p->buf_ring = (spms_buf_ring *)((uint8_t *)mem + ring->buf_ring_offset);
    p->buf = (struct spms_buf *)((uint8_t *)mem + ring->buf_ring_offset + SPMS_BUF_RING_HEADER_LENGTH);
    spms_sub_rewind(p);
    *out = p;
    return SPMS_ERR_OK;
}

void spms_sub_free(spms_sub *ring)
{
    free(ring);
}

spms_err spms_sub_get_dropped_count(spms_sub *sub, uint64_t *count)
{
    *count = sub->dropped;
    return SPMS_ERR_OK;
}

spms_err spms_sub_rewind(spms_sub *ring)
{
    uint32_t tail = atomic_load_explicit(&ring->msg_ring->tail, memory_order_relaxed);
    if (tail == 0)
        ring->head = 0;
    else
        ring->head = (tail - 1);
    return SPMS_ERR_OK;
}

spms_err spms_sub_get_pos_by_ts(spms_sub *ring, uint32_t *pos, uint64_t ts)
{
    uint32_t tail = atomic_load_explicit(&ring->msg_ring->tail, memory_order_acquire);
    uint32_t head = atomic_load_explicit(&ring->msg_ring->head, memory_order_relaxed);

    if (tail == head)
        return SPMS_ERR_NOT_AVAILABLE;

    while (head < tail)
    {
        struct spms_msg *a = &ring->msgs[head & ring->msg_ring->mask];
        if (a->ts >= ts)
        {
            *pos = head;
            if (verify_pos(ring, head) != SPMS_ERR_OK)
                return SPMS_ERR_NOT_AVAILABLE;
            return SPMS_ERR_OK;
        }
        ++head;
    }
    return SPMS_ERR_NOT_AVAILABLE;
}

spms_err spms_sub_get_latest_ts(spms_sub *ring, uint64_t *ts)
{
    uint32_t tail = atomic_load_explicit(&ring->msg_ring->tail, memory_order_acquire);
    uint32_t head = atomic_load_explicit(&ring->msg_ring->head, memory_order_relaxed);
    if (tail == head)
        return SPMS_ERR_NOT_AVAILABLE;
    struct spms_msg *msg = &ring->msgs[(tail - 1) & ring->msg_ring->mask];
    *ts = atomic_load_explicit(&msg->ts, memory_order_relaxed);
    return verify_pos(ring, tail - 1);
}

spms_err spms_sub_get_latest_pos(spms_sub *ring, uint32_t *pos)
{
    uint32_t tail = atomic_load_explicit(&ring->msg_ring->tail, memory_order_acquire);
    if (tail == 0)
        return SPMS_ERR_NOT_AVAILABLE;
    *pos = (tail - 1);
    return SPMS_ERR_OK;
}

spms_err spms_sub_get_latest_key_pos(spms_sub *sub, uint32_t *pos)
{
    (void)atomic_load_explicit(&sub->msg_ring->tail, memory_order_acquire); // sync with producer
    *pos = atomic_load_explicit(&sub->msg_ring->last_key, memory_order_relaxed);
    return verify_pos(sub, *pos);
}

spms_err spms_sub_get_cur_pos(spms_sub *ring, uint32_t *pos)
{
    *pos = ring->head;
    return SPMS_ERR_OK;
}

spms_err spms_sub_get_cur_ts(spms_sub *sub, uint64_t *ts)
{
    uint32_t pos = 0;
    int32_t ret = 0;
    if ((ret = spms_sub_get_and_verify_cur_pos(sub, &pos)) < 0)
        return ret;
    struct spms_msg *msg = &sub->msgs[pos & sub->msg_ring->mask];
    *ts = atomic_load_explicit(&msg->ts, memory_order_relaxed);
    return verify_pos(sub, pos);
}

spms_err spms_sub_get_next_key_pos(spms_sub *sub, uint32_t *out)
{
    uint32_t tail = atomic_load_explicit(&sub->msg_ring->tail, memory_order_acquire);
    uint32_t head = atomic_load_explicit(&sub->msg_ring->head, memory_order_relaxed);
    ensure_valid_head(sub, tail, head);
    uint32_t pos = sub->head;

    while (pos < tail)
    {
        struct spms_msg *msg = &sub->msgs[pos & sub->msg_ring->mask];
        uint8_t is_key = atomic_load_explicit(&msg->is_key, memory_order_relaxed);
        if (is_key)
        {
            *out = pos;
            // Check whether out read position was overwritten
            if (verify_pos(sub, pos) != SPMS_ERR_OK)
                return SPMS_ERR_NOT_AVAILABLE;
            return SPMS_ERR_OK;
        }
        ++pos;
    }
    return SPMS_ERR_NOT_AVAILABLE;
}

spms_err spms_sub_get_ts_by_pos(spms_sub *sub, uint64_t *ts, uint32_t pos)
{
    struct spms_msg *msg = &sub->msgs[pos & sub->msg_ring->mask];
    *ts = atomic_load_explicit(&msg->ts, memory_order_relaxed);
    return verify_pos(sub, pos);
}

spms_err spms_sub_set_pos(spms_sub *ring, uint32_t pos)
{
    ring->head = pos;
    return SPMS_ERR_OK;
}

spms_err spms_sub_verify_cur_pos(spms_sub *sub)
{
    return verify_pos(sub, sub->head);
}

spms_err spms_sub_get_and_verify_cur_pos(spms_sub *sub, uint32_t *pos)
{
    *pos = sub->head;
    return spms_sub_verify_cur_pos(sub);
}

static void ensure_valid_head(spms_sub *ring, uint32_t tail, uint32_t head)
{
    if (ring->head < head) // ensure safe zone
    {
        uint32_t shift = (head - ring->head) + (tail - head) / 16;
        ring->dropped += shift;
        ring->head += shift;
    }
    else if (ring->head > tail)
    {
        ring->head = tail;
        if (tail != head)
            --ring->head;
    }
}

spms_err spms_sub_ensure_valid_cur_pos(spms_sub *sub)
{
    uint32_t head, tail;
    tail = atomic_load_explicit(&sub->msg_ring->tail, memory_order_acquire); // sync with producer
    head = atomic_load_explicit(&sub->msg_ring->head, memory_order_relaxed);
    ensure_valid_head(sub, tail, head);
    return SPMS_ERR_OK;
}

static spms_err spms_wait_tail_changed(spms_sub *sub, uint32_t tail, uint32_t timeout_ms)
{
#ifdef CV_USE_FUTEX
    struct timespec ts;
    uint32_t seconds = timeout_ms / 1000;
    ts.tv_nsec = (long)(timeout_ms - seconds * 1000) * 1000000;
    ts.tv_sec = (time_t)seconds;
    syscall(SYS_futex, &sub->msg_ring->tail, FUTEX_WAIT, tail, &ts);
#elif CV_USE_ULOCK
    __ulock_wait(1, &sub->msg_ring->tail, tail, timeout_ms * 1000);
#endif
    return SPMS_ERR_OK;
}

spms_err spms_sub_get_read_buf(spms_sub *ring, const void **out_addr, size_t *out_len, struct spms_msg_info *info, uint32_t timeout_ms)
{
    while (1)
    {
        uint32_t tail = atomic_load_explicit(&ring->msg_ring->tail, memory_order_acquire);
        uint32_t head = atomic_load_explicit(&ring->msg_ring->head, memory_order_relaxed);
        ensure_valid_head(ring, tail, head);

        // skip nil packets
        while (ring->head < tail && (&ring->msgs[ring->head & ring->msg_ring->mask])->is_nil)
            ++ring->head;

        if (ring->head == tail)
        {
            if (ring->nonblocking)
                return SPMS_ERR_AGAIN;
            if (timeout_ms == 0)
                return SPMS_ERR_TIMEOUT;
            int32_t ret = spms_wait_tail_changed(ring, tail, timeout_ms);
            if (ret < 0)
                return ret;
            timeout_ms = 0; // don't wait again
            continue;
        }
        struct spms_msg *msg = &ring->msgs[ring->head & ring->msg_ring->mask];

        uint64_t offset = atomic_load_explicit(&msg->offset, memory_order_relaxed);
        uint32_t len = atomic_load_explicit(&msg->len, memory_order_relaxed);
        if (info)
        {
            info->is_key = atomic_load_explicit(&msg->is_key, memory_order_relaxed);
            info->ts = atomic_load_explicit(&msg->ts, memory_order_relaxed);
        }
        *out_addr = (uint8_t *)ring->buf + offset;
        *out_len = len;

        // We need to verify the position to guarantee that the message is valid
        if (spms_sub_verify_cur_pos(ring) == SPMS_ERR_OK)
            return SPMS_ERR_OK;
    }
}

spms_err spms_sub_finalize_read(spms_sub *sub)
{
    spms_err result = spms_sub_verify_cur_pos(sub);
    if (result == 0)
        ++sub->head;
    return result;
}

spms_err spms_sub_readv_msg(spms_sub *sub, struct spms_ivec *ivec, size_t *len, struct spms_msg_info *info, uint32_t timeout_ms)
{
    size_t avail_len = 0;
    for (size_t i = 0; i < *len; ++i)
        avail_len += ivec[i].len;

    while (1)
    {
        const void *buf = NULL;
        size_t buf_len = 0;
        spms_err ret = spms_sub_get_read_buf(sub, &buf, &buf_len, info, timeout_ms);
        if (ret != SPMS_ERR_OK)
            return ret;
        if (buf_len > avail_len)
            return SPMS_ERR_INVALID_ARG;

        size_t buf_idx = 0;
        size_t to_read = buf_len;
        while (to_read > 0)
        {
            size_t copy_len = to_read > ivec[buf_idx].len ? ivec[buf_idx].len : to_read;
            memcpy(ivec[buf_idx].addr, buf, copy_len);
            ivec[buf_idx].len = copy_len;
            buf = (uint8_t *)buf + copy_len;
            to_read -= copy_len;
            if (copy_len == ivec[buf_idx].len)
                ++buf_idx;
        }

        if (spms_sub_finalize_read(sub) == 0)
        {
            *len = buf_idx;
            return SPMS_ERR_OK;
        }
    }
}

spms_err spms_sub_read_msg(spms_sub *ring, void *addr, size_t *len, struct spms_msg_info *info, uint32_t timeout_ms)
{
    while (1)
    {
        const void *ptr = NULL;
        size_t ptr_len = 0;
        spms_err ret = spms_sub_get_read_buf(ring, &ptr, &ptr_len, info, timeout_ms);
        if (ret != SPMS_ERR_OK)
            return ret;
        if (ptr_len > *len)
            return SPMS_ERR_INVALID_ARG;

        memcpy(addr, ptr, ptr_len);
        if (spms_sub_finalize_read(ring) == 0)
        {
            *len = ptr_len;
            return SPMS_ERR_OK;
        }
    }
}

spms_err spms_sub_wait_readable(spms_sub *sub, uint32_t timeout_ms)
{
    while (1)
    {
        uint32_t tail = atomic_load_explicit(&sub->msg_ring->tail, memory_order_acquire);
        uint32_t head = atomic_load_explicit(&sub->msg_ring->head, memory_order_relaxed);
        ensure_valid_head(sub, tail, head);

        // skip nil packets
        while (sub->head < tail && (&sub->msgs[sub->head & sub->msg_ring->mask])->is_nil)
            ++sub->head;

        if (sub->head < tail)
            return SPMS_ERR_OK;
        if (sub->nonblocking)
            return SPMS_ERR_AGAIN;
        if (timeout_ms == 0)
            return SPMS_ERR_TIMEOUT;
        spms_err ret = spms_wait_tail_changed(sub, tail, timeout_ms);
        if (ret != SPMS_ERR_OK)
            return ret;
        timeout_ms = 0; // don't wait again
    }
}
