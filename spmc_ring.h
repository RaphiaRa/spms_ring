#ifndef spmc_ring_H
#define spmc_ring_H

#include <stddef.h>
#include <stdint.h>

typedef struct spmc_ring spmc_ring;

struct spmc_ring_config
{
    size_t buf_length;
    size_t msg_entries;
};

/** Basic read/write api **/

int32_t spmc_ring_pub_create(spmc_ring **ring, const char *name, struct spmc_ring_config *config, int32_t flags);
int32_t spmc_ring_write_msg(spmc_ring *ring, const void *addr, size_t len);
int32_t spmc_ring_write_msg_with_ts(spmc_ring *ring, const void *addr, size_t len, uint64_t ts);

int32_t spmc_ring_sub_create(spmc_ring **ring, const char *name);
int32_t spmc_ring_read_msg(spmc_ring *ring, void *addr, size_t len);

void spmc_ring_free(spmc_ring *ring);
/** Control api */

/**
 * @brief get the buffer position where the key is
 */
int32_t spmc_ring_pos_rewind(spmc_ring *ring);
int32_t spmc_ring_get_pos_by_ts(spmc_ring *ring, uint32_t *pos, uint64_t ts);
int32_t spmc_ring_get_pos_after_ts(spmc_ring *ring, uint32_t *pos, uint64_t ts);
int32_t spmc_ring_get_head_key(spmc_ring *ring, uint64_t *key);
int32_t spmc_ring_get_head_pos(spmc_ring *ring, uint32_t *pos);
int32_t spmc_ring_get_tail_pos(spmc_ring *ring, uint32_t *pos);
int32_t spmc_ring_set_tail_pos(spmc_ring *ring, uint32_t pos);

/** Zero copy api */
int32_t spmc_ring_get_write_buf(spmc_ring *ring, void **addr, size_t len);
int32_t spmc_ring_flush_write_buf(spmc_ring *ring, void *addr, size_t len, uint64_t key);

int32_t spmc_ring_get_msg_version(spmc_ring *ring);
int32_t spmc_ring_get_read_buf(spmc_ring *ring, const void **addr, size_t *len);
int32_t spmc_ring_finalize_read_buf();

#endif
