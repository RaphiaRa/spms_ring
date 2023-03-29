#ifndef spms_H
#define spms_H

#include <stddef.h>
#include <stdint.h>

typedef struct spms spms;

struct spms_config
{
    size_t buf_length;
    size_t msg_entries;
};

#define SPMS_FLAG_PERSISTENT 0x01

/** Creators and deleters **/

int32_t spms_pub_create(spms **ring, const char *name, struct spms_config *config, int32_t flags);
int32_t spms_sub_create(spms **ring, const char *name);
void spms_free(spms *ring);

/** Basic read/write api **/

int32_t spms_write_msg(spms *ring, const void *addr, size_t len);
int32_t spms_write_msg_with_ts(spms *ring, const void *addr, size_t len, uint64_t ts);
int64_t spms_read_msg(spms *ring, void *addr, size_t len);

/** Control api */

/**
 * @brief get the buffer position where the key is
 */
int32_t spms_pos_rewind(spms *ring);
int32_t spms_get_pos_by_ts(spms *ring, uint32_t *pos, uint64_t ts);
int32_t spms_get_back_ts(spms *ring, uint64_t *ts);
int32_t spms_get_back_pos(spms *ring, uint32_t *pos);
int32_t spms_get_front_pos(spms *ring, uint32_t *pos);
int32_t spms_set_front_pos(spms *ring, uint32_t pos);

/** Zero copy api */
int32_t spms_get_write_buf(spms *ring, void **addr, size_t len);
int32_t spms_flush_write_buf(spms *ring, void *addr, size_t len, uint64_t ts);

int32_t spms_get_read_buf(spms *ring, const void **addr, size_t *len);
int32_t spms_finalize_read(spms *ring, int32_t ver);

#endif
