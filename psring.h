#ifndef psring_H
#define psring_H

#include <stddef.h>
#include <stdint.h>

typedef struct psring psring;

struct psring_config
{
    size_t buf_length;
    size_t msg_entries;
};

/** Basic read/write api **/

int32_t psring_pub_create(psring **ring, const char *name, struct psring_config *config, int32_t flags);
int32_t psring_write(psring *ring, const void *addr, size_t len);
int32_t psring_write_with_key(psring *ring, const void *addr, size_t len, uint64_t key);

int32_t psring_sub_create(psring **ring, const char *name);
int32_t psring_read(psring *ring, void *addr, size_t len);

void psring_free(psring *ring);
/** Control api */

/**
 * @brief get the buffer position where the key is
 */
int32_t psring_pos_rewind(psring *ring);
int32_t psring_get_pos_by_key(psring *ring, uint32_t *pos, uint64_t key);
int32_t psring_get_pos_near_key(psring *ring, uint32_t *pos, uint64_t key);

int32_t psring_get_head_key(psring *ring, uint64_t *key);
int32_t psring_get_head_pos(psring *ring, uint32_t *pos);
int32_t psring_get_pos(psring *ring, uint32_t *pos);
int32_t psring_set_pos(psring *ring, uint32_t pos);

/** Zero copy api */
int32_t psring_get_write_buf(psring *ring, void **addr, size_t len);
int32_t psring_flush_write_buf(psring *ring, void *addr, size_t len, uint64_t key);

int32_t psring_get_read_buf(psring *ring, const void **addr, size_t *len);
int32_t psring_finalize_read_buf();
int32_t psring_is_good(psring *ring, struct psring_read_buf *block);

#endif
