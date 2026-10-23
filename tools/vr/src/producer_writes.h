#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PW_OBJECTS 8192
#define PW_WRITES 32
#define PW_ATTACHMENTS 16
#define PW_SUBPASSES 8

// Incomplete tracking must never certify an older texture generation as ready.
enum { PW_META_GAP, PW_COMMAND_GAP, PW_GAP_TYPES };
enum { PW_MEMORY = 1, PW_IMAGE, PW_VIEW, PW_FRAMEBUFFER, PW_PASS, PW_COMMAND };
struct pw_object {
    uint64_t key, a, b, ready_serial, recording_gap;
    unsigned ended;
    unsigned type, count, subpass, active_count;
    uint64_t refs[PW_ATTACHMENTS], active[PW_ATTACHMENTS];
    uint32_t colors[PW_SUBPASSES], clears;
    uint64_t writes[PW_WRITES];
};
static struct pw_object pw_objects[PW_OBJECTS];
static struct pw_object *pw_memories[PW_OBJECTS];
static unsigned pw_memory_count;
static pthread_mutex_t pw_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned pw_enabled;
static uint64_t pw_serial, pw_gap_counts[PW_GAP_TYPES];

static uint64_t pw_gap_count(unsigned scope) { return __atomic_load_n(&pw_gap_counts[scope], __ATOMIC_RELAXED); }
static void pw_gap(unsigned scope) { __atomic_fetch_add(&pw_gap_counts[scope], 1, __ATOMIC_RELAXED); }
static int pw_enter(void) { return pw_enabled && pthread_mutex_lock(&pw_lock) == 0; }
static void pw_leave(void) { pthread_mutex_unlock(&pw_lock); }
static struct pw_object *pw_find(unsigned type, uint64_t key) {
    if (!key)
        return NULL;
    unsigned begin = (unsigned)((key >> 4) ^ key ^ (type * 2654435761u)) % PW_OBJECTS;
    for (unsigned n = 0; n < PW_OBJECTS; n++) {
        struct pw_object *o = &pw_objects[(begin + n) % PW_OBJECTS];
        if (!o->type)
            return NULL;
        if (o->type == type && o->key == key)
            return o;
    }
    return NULL;
}
static struct pw_object *pw_put(unsigned type, uint64_t key) {
    struct pw_object *o = pw_find(type, key);
    unsigned fresh = !o;
    if (!o) {
        unsigned begin = (unsigned)((key >> 4) ^ key ^ (type * 2654435761u)) % PW_OBJECTS;
        for (unsigned n = 0; n < PW_OBJECTS; n++)
            if (!pw_objects[(begin + n) % PW_OBJECTS].type || pw_objects[(begin + n) % PW_OBJECTS].type == UINT32_MAX) {
                o = &pw_objects[(begin + n) % PW_OBJECTS];
                break;
            }
    }
    if (!o) {
        pw_gap(type == PW_COMMAND ? PW_COMMAND_GAP : PW_META_GAP);
        return NULL;
    }
    if (fresh && type == PW_MEMORY)
        pw_memories[pw_memory_count++] = o;
    memset(o, 0, sizeof(*o));
    o->type = type;
    o->key = key;
    return o;
}
static void pw_remove(unsigned type, uint64_t key) {
    if (!pw_enter())
        return;
    struct pw_object *o = pw_find(type, key);
    if (o) {
        if (type == PW_MEMORY)
            for (unsigned i = 0; i < pw_memory_count; i++)
                if (pw_memories[i] == o) {
                    pw_memories[i] = pw_memories[--pw_memory_count];
                    break;
                }
        memset(o, 0, sizeof(*o));
        o->type = UINT32_MAX;
    }
    pw_leave();
}
static void pw_start(void) {
    if (pw_enabled)
        return;
    const char *lease = getenv("DROIDDECK_VR_PRODUCER_LEASE"), *present = getenv("DROIDDECK_VR_PRESENT");
    pw_enabled = lease && !strcmp(lease, "1") && (!present || strcmp(present, "questframes"));
}
static struct pw_object *pw_image_memory(uint64_t image) {
    struct pw_object *im = pw_find(PW_IMAGE, image);
    return im ? pw_find(PW_MEMORY, im->a) : NULL;
}
static uint64_t pw_view_image(uint64_t view) {
    struct pw_object *v = pw_find(PW_VIEW, view);
    return v ? v->a : 0;
}
static void pw_write_locked(struct pw_object *cmd, uint64_t image) {
    if (!cmd) {
        pw_gap(PW_COMMAND_GAP);
        return;
    }
    struct pw_object *m = pw_image_memory(image);
    if (!m) {
        pw_gap(PW_COMMAND_GAP);
        return;
    }
    if (!m->a)
        return;
    for (unsigned i = 0; i < cmd->count; i++)
        if (cmd->writes[i] == image)
            return;
    if (cmd->count == PW_WRITES) {
        pw_gap(PW_COMMAND_GAP);
        return;
    }
    cmd->writes[cmd->count++] = image;
}
static void pw_write(VkCommandBuffer cmd, VkImage image) {
    if (!pw_enter())
        return;
    pw_write_locked(pw_find(PW_COMMAND, (uint64_t)cmd), (uint64_t)image);
    pw_leave();
}
static void pw_draw(VkCommandBuffer cmd) {
    if (!pw_enter())
        return;
    struct pw_object *c = pw_find(PW_COMMAND, (uint64_t)cmd);
    if (!c || c->b)
        pw_gap(PW_COMMAND_GAP);
    else
        for (unsigned i = 0; i < c->active_count; i++)
            pw_write_locked(c, c->active[i]);
    pw_leave();
}
static void pw_pass_targets(struct pw_object *c) {
    struct pw_object *p = pw_find(PW_PASS, c->a);
    c->active_count = 0;
    if (!p || c->subpass >= p->count) {
        pw_gap(PW_COMMAND_GAP);
        return;
    }
    for (unsigned i = 0; i < PW_ATTACHMENTS; i++)
        if (p->colors[c->subpass] & (1u << i)) {
            uint64_t image = pw_view_image(c->refs[i]);
            if (!image) {
                pw_gap(PW_COMMAND_GAP);
                continue;
            }
            c->active[c->active_count++] = image;
            if ((p->clears & (1u << i)) && !(c->clears & (1u << i))) {
                pw_write_locked(c, image);
                c->clears |= 1u << i;
            }
        }
}
static void pw_begin_pass(VkCommandBuffer cmd, const VkRenderPassBeginInfo *info) {
    if (!pw_enter())
        return;
    struct pw_object *c = pw_find(PW_COMMAND, (uint64_t)cmd),
                     *fb = pw_find(PW_FRAMEBUFFER, (uint64_t)info->framebuffer);
    if (!c || !fb) {
        pw_gap(PW_COMMAND_GAP);
        pw_leave();
        return;
    }
    c->a = (uint64_t)info->renderPass;
    c->b = 0;
    c->subpass = 0;
    c->clears = 0;
    memcpy(c->refs, fb->refs, sizeof(c->refs));
    if (fb->a) {
        const VkRenderPassAttachmentBeginInfo *attachments = NULL;
        for (const VkBaseInStructure *p = info->pNext; p; p = p->pNext)
            if (p->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO)
                attachments = (const void *)p;
        if (!attachments || attachments->attachmentCount > PW_ATTACHMENTS)
            pw_gap(PW_COMMAND_GAP);
        else
            for (unsigned i = 0; i < attachments->attachmentCount; i++)
                c->refs[i] = (uint64_t)attachments->pAttachments[i];
    }
    pw_pass_targets(c);
    pw_leave();
}
static void pw_next_pass(VkCommandBuffer cmd) {
    if (!pw_enter())
        return;
    struct pw_object *c = pw_find(PW_COMMAND, (uint64_t)cmd);
    if (c) {
        c->subpass++;
        pw_pass_targets(c);
    } else
        pw_gap(PW_COMMAND_GAP);
    pw_leave();
}
static void pw_end_pass(VkCommandBuffer cmd) {
    if (!pw_enter())
        return;
    struct pw_object *c = pw_find(PW_COMMAND, (uint64_t)cmd);
    if (c) {
        c->active_count = 0;
        c->b = 0;
    } else
        pw_gap(PW_COMMAND_GAP);
    pw_leave();
}
static void pw_submitted(uint64_t serial, VkCommandBuffer cmd) {
    if (!pw_enter())
        return;
    struct pw_object *c = pw_find(PW_COMMAND, (uint64_t)cmd);
    int valid = c && c->ended && c->recording_gap == pw_gap_count(PW_COMMAND_GAP) && !pw_gap_count(PW_META_GAP);
    if (valid) {
        for (unsigned i = 0; i < c->count; i++)
            if (!pw_image_memory(c->writes[i])) {
                valid = 0;
                break;
            }
        if (valid)
            for (unsigned i = 0; i < c->count; i++)
                pw_image_memory(c->writes[i])->ready_serial = serial;
    }
    pw_leave();
}

// A texture handed back for rendering needs a submission newer than this reservation.
static uint64_t pw_reserve(void) { return pw_enabled ? __atomic_load_n(&pw_serial, __ATOMIC_ACQUIRE) + 1 : 0; }

static int pw_targets_ready(const struct droiddeck_producer_target targets[2]) {
    if (!pw_enabled || pw_gap_count(PW_META_GAP))
        return -1;
    for (unsigned eye = 0; eye < 2; eye++)
        if (!targets[eye].inode || !targets[eye].minimum)
            return -1;
    int ready = 1;
    pthread_mutex_lock(&pw_lock);
    if (pw_gap_count(PW_META_GAP)) {
        pw_leave();
        return -1;
    }
    for (unsigned eye = 0; eye < 2; eye++) {
        uint64_t latest = 0;
        for (unsigned i = 0; i < pw_memory_count; i++) {
            const struct pw_object *m = pw_memories[i];
            if (m->a == targets[eye].inode && m->b == targets[eye].device && m->ready_serial > latest)
                latest = m->ready_serial;
        }
        if (latest < targets[eye].minimum)
            ready = 0;
    }
    pthread_mutex_unlock(&pw_lock);
    return ready;
}
