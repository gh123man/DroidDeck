#pragma once
#include "../../../app/src/main/cpp/waylandcomp/src/motion_trace_impl.h"

#define PM_OBJECTS 8192
#define PM_WRITES 32
#define PM_ATTACHMENTS 16
#define PM_SUBPASSES 8

enum { PM_META_GAP, PM_COMMAND_GAP, PM_SUBMIT_GAP, PM_LEASE_GAP, PM_GAP_TYPES };
enum { PM_MEMORY=1, PM_IMAGE, PM_VIEW, PM_FRAMEBUFFER, PM_PASS, PM_COMMAND };
enum { PM_CLEAR=1, PM_COPY=2, PM_BLIT=4, PM_DRAW=8, PM_RESOLVE=16, PM_LOAD_CLEAR=32 };
struct pm_write { uint64_t image; unsigned kinds, count; };
struct pm_object {
    uint64_t key, device, a, b, serial, ready_serial, recording_gap;
    unsigned ended;
    unsigned type, count, subpass, active_count;
    uint64_t refs[PM_ATTACHMENTS], active[PM_ATTACHMENTS];
    uint32_t colors[PM_SUBPASSES], clears;
    struct pm_write writes[PM_WRITES];
};
static struct pm_object pm_objects[PM_OBJECTS];
static struct pm_object *pm_memories[PM_OBJECTS];
static unsigned pm_memory_count;
static pthread_mutex_t pm_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned pm_enabled, pm_required;
static uint64_t pm_gaps, pm_serial, pm_gap_counts[PM_GAP_TYPES];

static uint64_t pm_gap_count(unsigned scope) { return __atomic_load_n(&pm_gap_counts[scope],__ATOMIC_RELAXED); }
static void pm_gap(unsigned scope) {
    __atomic_fetch_add(&pm_gaps, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&pm_gap_counts[scope], 1, __ATOMIC_RELAXED);
}
static int pm_enter(unsigned scope) {
    if (!pm_enabled) return 0;
    if (pm_required) return pthread_mutex_lock(&pm_lock)==0;
    if (pthread_mutex_trylock(&pm_lock)) { pm_gap(scope); return 0; }
    return 1;
}
static void pm_leave(void) { pthread_mutex_unlock(&pm_lock); }
static struct pm_object *pm_find(unsigned type, uint64_t key) {
    if (!key) return NULL;
    unsigned begin = (unsigned)((key >> 4) ^ key ^ (type * 2654435761u)) % PM_OBJECTS;
    for (unsigned n=0; n<PM_OBJECTS; n++) {
        struct pm_object *o=&pm_objects[(begin+n)%PM_OBJECTS];
        if (!o->type) return NULL;
        if (o->type==type && o->key==key) return o;
    }
    return NULL;
}
static struct pm_object *pm_put(unsigned type, uint64_t key, uint64_t device) {
    struct pm_object *o=pm_find(type,key);
    unsigned fresh=!o;
    if (!o) {
        unsigned begin=(unsigned)((key>>4)^key^(type*2654435761u))%PM_OBJECTS;
        for (unsigned n=0;n<PM_OBJECTS;n++) if (!pm_objects[(begin+n)%PM_OBJECTS].type || pm_objects[(begin+n)%PM_OBJECTS].type==UINT32_MAX) {
            o=&pm_objects[(begin+n)%PM_OBJECTS]; break;
        }
    }
    if (!o) { pm_gap(type==PM_COMMAND?PM_COMMAND_GAP:PM_META_GAP); return NULL; }
    if(fresh && type==PM_MEMORY)pm_memories[pm_memory_count++]=o;
    memset(o,0,sizeof(*o)); o->type=type; o->key=key; o->device=device;
    return o;
}
static void pm_remove(unsigned type, uint64_t key) {
    if (!pm_enter(type==PM_COMMAND?PM_COMMAND_GAP:PM_META_GAP)) return;
    struct pm_object *o=pm_find(type,key);
    if(o) {
        if(type==PM_MEMORY)for(unsigned i=0;i<pm_memory_count;i++)if(pm_memories[i]==o) {
            pm_memories[i]=pm_memories[--pm_memory_count];break;
        }
        memset(o,0,sizeof(*o));o->type=UINT32_MAX;
    }
    pm_leave();
}
static void pm_start(void) {
    if (pm_enabled) return;
    const char *rt=getenv("XDG_RUNTIME_DIR"); if(!rt) rt="/run/droiddeck";
    dd_motion_start(rt,"producer");
    const char *lease=getenv("DROIDDECK_VR_PRODUCER_LEASE"), *present=getenv("DROIDDECK_VR_PRESENT");
    pm_required=lease && !strcmp(lease,"1") && (!present || strcmp(present,"questframes"));
    pm_enabled=pm_required || __atomic_load_n(&dd_mt.initialized,__ATOMIC_ACQUIRE);
}
static struct pm_object *pm_image_memory(uint64_t image) {
    struct pm_object *im=pm_find(PM_IMAGE,image);
    return im ? pm_find(PM_MEMORY,im->a) : NULL;
}
static uint64_t pm_view_image(uint64_t view) {
    struct pm_object *v=pm_find(PM_VIEW,view); return v?v->a:0;
}
static void pm_write_locked(struct pm_object *cmd,uint64_t image,unsigned kind) {
    if (!cmd) { pm_gap(PM_COMMAND_GAP); return; }
    struct pm_object *m=pm_image_memory(image);
    if (!m) { pm_gap(PM_COMMAND_GAP); return; }
    if (!m->a) return;
    for(unsigned i=0;i<cmd->count;i++) if(cmd->writes[i].image==image) {
        cmd->writes[i].kinds|=kind; cmd->writes[i].count++; return;
    }
    if(cmd->count==PM_WRITES) { pm_gap(PM_COMMAND_GAP);return; }
    cmd->writes[cmd->count++]=(struct pm_write){image,kind,1};
}
static void pm_write(VkCommandBuffer cmd,VkImage image,unsigned kind) {
    if(!pm_enter(PM_COMMAND_GAP))return;
    pm_write_locked(pm_find(PM_COMMAND,(uint64_t)cmd),(uint64_t)image,kind);pm_leave();
}
static void pm_draw(VkCommandBuffer cmd) {
    if(!pm_enter(PM_COMMAND_GAP))return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(!c || c->b) pm_gap(PM_COMMAND_GAP);
    else for(unsigned i=0;i<c->active_count;i++) pm_write_locked(c,c->active[i],PM_DRAW);
    pm_leave();
}
static void pm_pass_targets(struct pm_object *c) {
    struct pm_object *p=pm_find(PM_PASS,c->a);
    c->active_count=0;
    if(!p || c->subpass>=p->count) { pm_gap(PM_COMMAND_GAP);return; }
    for(unsigned i=0;i<PM_ATTACHMENTS;i++) if(p->colors[c->subpass]&(1u<<i)) {
        uint64_t image=pm_view_image(c->refs[i]);
        if(!image) { pm_gap(PM_COMMAND_GAP);continue; }
        c->active[c->active_count++]=image;
        if((p->clears&(1u<<i)) && !(c->clears&(1u<<i))) {
            pm_write_locked(c,image,PM_LOAD_CLEAR);c->clears|=1u<<i;
        }
    }
}
static void pm_begin_pass(VkCommandBuffer cmd,const VkRenderPassBeginInfo *info) {
    if(!pm_enter(PM_COMMAND_GAP))return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd),*fb=pm_find(PM_FRAMEBUFFER,(uint64_t)info->framebuffer);
    if(!c || !fb) { pm_gap(PM_COMMAND_GAP);pm_leave();return; }
    c->a=(uint64_t)info->renderPass;c->b=0;c->subpass=0;c->clears=0;
    memcpy(c->refs,fb->refs,sizeof(c->refs));
    if(fb->a) {
        const VkRenderPassAttachmentBeginInfo *attachments=NULL;
        for(const VkBaseInStructure *p=info->pNext;p;p=p->pNext)
            if(p->sType==VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) attachments=(const void*)p;
        if(!attachments || attachments->attachmentCount>PM_ATTACHMENTS) pm_gap(PM_COMMAND_GAP);
        else for(unsigned i=0;i<attachments->attachmentCount;i++) c->refs[i]=(uint64_t)attachments->pAttachments[i];
    }
    pm_pass_targets(c);pm_leave();
}
static void pm_next_pass(VkCommandBuffer cmd) {
    if(!pm_enter(PM_COMMAND_GAP))return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(c) { c->subpass++;pm_pass_targets(c); }else pm_gap(PM_COMMAND_GAP);pm_leave();
}
static void pm_end_pass(VkCommandBuffer cmd) {
    if(!pm_enter(PM_COMMAND_GAP))return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(c) { c->active_count=0;c->b=0; }else pm_gap(PM_COMMAND_GAP);pm_leave();
}
static void pm_submitted(uint64_t serial,VkQueue queue,VkCommandBuffer cmd,int phase) {
    struct { uint64_t inode; double v[7]; } writes[PM_WRITES];
    unsigned count=0,valid=0;
    uint64_t recording_gap=0;
    if(pm_enter(PM_SUBMIT_GAP)) {
        struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
        if(c) {
            recording_gap=c->recording_gap;
            valid=c->ended && recording_gap==pm_gap_count(PM_COMMAND_GAP);
            for(unsigned i=0;i<c->count;i++) {
                struct pm_write *w=&c->writes[i];struct pm_object *m=pm_image_memory(w->image);
                if(!m) { valid=0;pm_gap(PM_SUBMIT_GAP);continue; }
                writes[count].inode=m->a;
                double v[]={(double)m->b,(double)w->image,(double)(uintptr_t)cmd,(double)(uintptr_t)queue,
                            (double)w->kinds,(double)w->count,(double)__atomic_load_n(&pm_gaps,__ATOMIC_RELAXED)};
                memcpy(writes[count++].v,v,sizeof(v));
                if(phase==1) { m->serial=serial;m->count=w->kinds;m->refs[0]=(uint64_t)dd_motion_now(); }
            }
            if(phase==1 && valid && !pm_gap_count(PM_META_GAP))
                for(unsigned i=0;i<c->count;i++)pm_image_memory(c->writes[i].image)->ready_serial=serial;
        }else pm_gap(PM_SUBMIT_GAP);
        pm_leave();
    }
    double summary[]={(double)(uintptr_t)queue,valid,(double)pm_gap_count(PM_META_GAP),
                      (double)recording_gap,count,(double)pm_gap_count(PM_COMMAND_GAP)};
    dd_motion_emit(DD_MOTION_WRITE,phase+3,serial,(uint64_t)(uintptr_t)cmd,summary,6);
    for(unsigned i=0;i<count;i++)dd_motion_emit(DD_MOTION_WRITE,phase,serial,writes[i].inode,writes[i].v,7);
}
#define PM_SHARED_MEMORIES 256
static void pm_ready(uint64_t ticket) {
    struct { uint64_t inode; double v[5]; } memories[PM_SHARED_MEMORIES];
    unsigned count=0;
    if(!pm_enter(PM_LEASE_GAP))return;
    for(unsigned i=0;i<pm_memory_count;i++) {
        struct pm_object *m=pm_memories[i];if(!m->a)continue;
        if(count==PM_SHARED_MEMORIES) {pm_gap(PM_LEASE_GAP);break;}
        memories[count].inode=m->a;
        double v[]={(double)m->b,(double)m->serial,(double)m->count,(double)__atomic_load_n(&pm_gaps,__ATOMIC_RELAXED),(double)m->refs[0]};
        memcpy(memories[count++].v,v,sizeof(v));
    }
    pm_leave();
    double health[]={(double)__atomic_load_n(&pm_gaps,__ATOMIC_RELAXED),
                     (double)pm_gap_count(PM_META_GAP),(double)pm_gap_count(PM_COMMAND_GAP),
                     (double)pm_gap_count(PM_SUBMIT_GAP),(double)pm_gap_count(PM_LEASE_GAP),2,count};
    dd_motion_emit(DD_MOTION_PRODUCER_LEASE,0,ticket,0,health,7);
    for(unsigned i=0;i<count;i++)dd_motion_emit(DD_MOTION_PRODUCER_LEASE,1,ticket,memories[i].inode,memories[i].v,5);
}

static uint64_t pm_reserve(void) {
    return pm_required ? __atomic_load_n(&pm_serial,__ATOMIC_ACQUIRE)+1 : 0;
}

static int pm_targets_ready(const struct droiddeck_producer_target targets[2]) {
    if(!pm_required || pm_gap_count(PM_META_GAP))return -1;
    for(unsigned eye=0;eye<2;eye++)if(!targets[eye].inode || !targets[eye].minimum)return -1;
    int ready=1;
    pthread_mutex_lock(&pm_lock);
    for(unsigned eye=0;eye<2;eye++) {
        uint64_t latest=0;
        for(unsigned i=0;i<pm_memory_count;i++) {
            const struct pm_object *m=pm_memories[i];
            if(m->a==targets[eye].inode && m->b==targets[eye].device && m->ready_serial>latest)latest=m->ready_serial;
        }
        if(latest<targets[eye].minimum)ready=0;
    }
    pthread_mutex_unlock(&pm_lock);
    return ready;
}
