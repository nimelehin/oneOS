#include <algo/ringbuffer.h>
#include <mem/vmm/vmm.h>

#define BUFFER_STD_SIZE (16 * KB)

ringbuffer_t ringbuffer_create(uint32_t size)
{
    ringbuffer_t buf;
    buf.zone = zoner_new_zone(size);
    if (!buf.zone.start) {
        return buf;
    }
    buf.start = 0;
    buf.end = 0;
    return buf;
}

ringbuffer_t ringbuffer_create_std()
{
    ringbuffer_t buf;
    buf.zone = zoner_new_zone(BUFFER_STD_SIZE);
    if (!buf.zone.start) {
        return buf;
    }
    buf.start = 0;
    buf.end = 0;
    return buf;
}

void ringbuffer_free(ringbuffer_t* buf)
{
    zoner_free_zone(buf->zone);
    buf->start = 0;
    buf->end = 0;
}

uint32_t ringbuffer_space_to_read(ringbuffer_t* buf)
{
    if (buf->start <= buf->end) {
        return buf->end - buf->start;
    } else {
        return buf->zone.len - buf->start + buf->end;
    }
}

uint32_t ringbuffer_space_to_write(ringbuffer_t* buf)
{
    if (buf->start > buf->end) {
        return buf->start - buf->end;
    } else {
        return buf->zone.len - buf->end + buf->start;
    }
}

uint32_t ringbuffer_read(ringbuffer_t* buf, uint8_t* holder, uint32_t siz)
{
    uint32_t i = 0;
    if (buf->start > buf->end) {
        for (; i < siz && buf->start < buf->zone.len; i++, buf->start++) {
            holder[i] = buf->zone.ptr[buf->start];
        }
        if (buf->start == buf->zone.len) {
            buf->start = 0;
        }
    }
    for (; i < siz && buf->start < buf->end; i++, buf->start++) {
        holder[i] = buf->zone.ptr[buf->start];
    }
    return i;
}

uint32_t ringbuffer_read_one(ringbuffer_t* buf, uint8_t* data)
{
    if (buf->start != buf->end) {
        *data = buf->zone.ptr[buf->start];
        buf->start++;
        if (buf->start == buf->zone.len) {
            buf->start = 0;
        }
        return 1;
    }
    return 0;
}

uint32_t ringbuffer_write(ringbuffer_t* buf, const uint8_t* holder, uint32_t siz)
{
    uint32_t i = 0;
    if (buf->end >= buf->start) {
        for (; i < siz && buf->end < buf->zone.len; i++, buf->end++) {
            buf->zone.ptr[buf->end] = holder[i];
        }
        if (buf->end == buf->zone.len) {
            buf->end = 0;
        }
    }
    for (; i < siz && buf->end < buf->start; i++, buf->end++) {
        buf->zone.ptr[buf->end] = holder[i];
    }
    return i;
}

uint32_t ringbuffer_write_one(ringbuffer_t* buf, uint8_t data)
{
    if (buf->end + 1 != buf->start) {
        buf->zone.ptr[buf->end] = data;
        buf->end++;
        if (buf->end == buf->zone.len) {
            buf->end = 0;
        }
        return 1;
    }
    return 0;
}