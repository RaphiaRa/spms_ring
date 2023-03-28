#ifndef spms_ring_H
#define spms_ring_H

#include <stddef.h>
#include <stdint.h>

typedef struct spms_ring spms_ring;

struct spms_ring_config
{
    size_t buf_length;
    size_t msg_entries;
};

/** Creators and deleters **/

int32_t spms_ring_pub_create(spms_ring **ring, const char *name, struct spms_ring_config *config, int32_t flags);
int32_t spms_ring_sub_create(spms_ring **ring, const char *name);
void spms_ring_free(spms_ring *ring);

/** Basic read/write api **/

int32_t spms_ring_write_msg(spms_ring *ring, const void *addr, size_t len);
int32_t spms_ring_write_msg_with_ts(spms_ring *ring, const void *addr, size_t len, uint64_t ts);
int64_t spms_ring_read_msg(spms_ring *ring, void *addr, size_t len);

/** Control api */

/**
 * @brief get the buffer position where the key is
 */
int32_t spms_ring_pos_rewind(spms_ring *ring);
int32_t spms_ring_get_pos_by_ts(spms_ring *ring, uint32_t *pos, uint64_t ts);
int32_t spms_ring_get_pos_after_ts(spms_ring *ring, uint32_t *pos, uint64_t ts);
int32_t spms_ring_get_front_ts(spms_ring *ring, uint64_t *ts);
int32_t spms_ring_get_front_pos(spms_ring *ring, uint32_t *pos);
int32_t spms_ring_get_back_pos(spms_ring *ring, uint32_t *pos);
int32_t spms_ring_set_back_pos(spms_ring *ring, uint32_t pos);

/** Zero copy api */
int32_t spms_ring_get_write_buf(spms_ring *ring, void **addr, size_t len);
int32_t spms_ring_flush_write_buf(spms_ring *ring, void *addr, size_t len, uint64_t ts);

int32_t spms_ring_get_read_buf(spms_ring *ring, const void **addr, size_t *len);
int32_t spms_ring_finalize_read(spms_ring *ring, int32_t ver);

#endif
