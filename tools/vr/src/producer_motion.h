#pragma once
#include "../../../app/src/main/cpp/waylandcomp/src/motion_trace_impl.h"

#define PM_OBJECTS 8192
#define PM_WRITES 32
#define PM_ATTACHMENTS 16
#define PM_SUBPASSES 8

enum { PM_MEMORY=1, PM_IMAGE, PM_VIEW, PM_FRAMEBUFFER, PM_PASS, PM_COMMAND };
enum { PM_CLEAR=1, PM_COPY=2, PM_BLIT=4, PM_DRAW=8, PM_RESOLVE=16, PM_LOAD_CLEAR=32 };
struct pm_write { uint64_t image; unsigned kinds, count; };
struct pm_object {
    uint64_t key, device, a, b, serial;
    unsigned type, count, subpass, active_count;
    uint64_t refs[PM_ATTACHMENTS], active[PM_ATTACHMENTS];
    uint32_t colors[PM_SUBPASSES], clears;
    struct pm_write writes[PM_WRITES];
};
static struct pm_object pm_objects[PM_OBJECTS];
static pthread_mutex_t pm_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned pm_enabled;
static uint64_t pm_gaps, pm_serial;

static void pm_gap(void) { __atomic_fetch_add(&pm_gaps, 1, __ATOMIC_RELAXED); }
static int pm_enter(void) {
    if (!pm_enabled) return 0;
    if (pthread_mutex_trylock(&pm_lock)) { pm_gap(); return 0; }
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
    if (!o) {
        unsigned begin=(unsigned)((key>>4)^key^(type*2654435761u))%PM_OBJECTS;
        for (unsigned n=0;n<PM_OBJECTS;n++) if (!pm_objects[(begin+n)%PM_OBJECTS].type || pm_objects[(begin+n)%PM_OBJECTS].type==UINT32_MAX) {
            o=&pm_objects[(begin+n)%PM_OBJECTS]; break;
        }
    }
    if (!o) { pm_gap(); return NULL; }
    memset(o,0,sizeof(*o)); o->type=type; o->key=key; o->device=device;
    return o;
}
static void pm_remove(unsigned type, uint64_t key) {
    if (!pm_enter()) return;
    struct pm_object *o=pm_find(type,key); if(o) { memset(o,0,sizeof(*o));o->type=UINT32_MAX; }
    pm_leave();
}
static void pm_start(void) {
    if (pm_enabled) return;
    const char *rt=getenv("XDG_RUNTIME_DIR"); if(!rt) rt="/run/droiddeck";
    dd_motion_start(rt,"producer");
    pm_enabled=__atomic_load_n(&dd_mt.initialized,__ATOMIC_ACQUIRE);
}
static struct pm_object *pm_image_memory(uint64_t image) {
    struct pm_object *im=pm_find(PM_IMAGE,image);
    return im ? pm_find(PM_MEMORY,im->a) : NULL;
}
static uint64_t pm_view_image(uint64_t view) {
    struct pm_object *v=pm_find(PM_VIEW,view); return v?v->a:0;
}
static void pm_write_locked(struct pm_object *cmd,uint64_t image,unsigned kind) {
    if (!cmd) { pm_gap(); return; }
    struct pm_object *m=pm_image_memory(image);
    if (!m || !m->a) return;
    for(unsigned i=0;i<cmd->count;i++) if(cmd->writes[i].image==image) {
        cmd->writes[i].kinds|=kind; cmd->writes[i].count++; return;
    }
    if(cmd->count==PM_WRITES) { pm_gap();return; }
    cmd->writes[cmd->count++]=(struct pm_write){image,kind,1};
}
static void pm_write(VkCommandBuffer cmd,VkImage image,unsigned kind) {
    if(!pm_enter())return;
    pm_write_locked(pm_find(PM_COMMAND,(uint64_t)cmd),(uint64_t)image,kind);pm_leave();
}
static void pm_draw(VkCommandBuffer cmd) {
    if(!pm_enter())return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(!c || c->b) pm_gap();
    else for(unsigned i=0;i<c->active_count;i++) pm_write_locked(c,c->active[i],PM_DRAW);
    pm_leave();
}
static void pm_pass_targets(struct pm_object *c) {
    struct pm_object *p=pm_find(PM_PASS,c->a);
    c->active_count=0;
    if(!p || c->subpass>=p->count) { pm_gap();return; }
    for(unsigned i=0;i<PM_ATTACHMENTS;i++) if(p->colors[c->subpass]&(1u<<i)) {
        uint64_t image=pm_view_image(c->refs[i]);
        if(!image) { pm_gap();continue; }
        c->active[c->active_count++]=image;
        if((p->clears&(1u<<i)) && !(c->clears&(1u<<i))) {
            pm_write_locked(c,image,PM_LOAD_CLEAR);c->clears|=1u<<i;
        }
    }
}
static void pm_begin_pass(VkCommandBuffer cmd,const VkRenderPassBeginInfo *info) {
    if(!pm_enter())return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd),*fb=pm_find(PM_FRAMEBUFFER,(uint64_t)info->framebuffer);
    if(!c || !fb) { pm_gap();pm_leave();return; }
    c->a=(uint64_t)info->renderPass;c->b=0;c->subpass=0;c->clears=0;
    memcpy(c->refs,fb->refs,sizeof(c->refs));
    if(fb->a) {
        const VkRenderPassAttachmentBeginInfo *attachments=NULL;
        for(const VkBaseInStructure *p=info->pNext;p;p=p->pNext)
            if(p->sType==VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) attachments=(const void*)p;
        if(!attachments || attachments->attachmentCount>PM_ATTACHMENTS) pm_gap();
        else for(unsigned i=0;i<attachments->attachmentCount;i++) c->refs[i]=(uint64_t)attachments->pAttachments[i];
    }
    pm_pass_targets(c);pm_leave();
}
static void pm_next_pass(VkCommandBuffer cmd) {
    if(!pm_enter())return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(c) { c->subpass++;pm_pass_targets(c); }else pm_gap();pm_leave();
}
static void pm_end_pass(VkCommandBuffer cmd) {
    if(!pm_enter())return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(c) { c->active_count=0;c->b=0; }else pm_gap();pm_leave();
}
static void pm_submitted(uint64_t serial,VkQueue queue,VkCommandBuffer cmd,int phase) {
    if(!pm_enter())return;
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(!c) pm_gap();
    else for(unsigned i=0;i<c->count;i++) {
        struct pm_write *w=&c->writes[i]; struct pm_object *m=pm_image_memory(w->image);
        if(!m) { pm_gap();continue; }
        double v[]={(double)m->b,(double)w->image,(double)(uintptr_t)cmd,(double)(uintptr_t)queue,
                    (double)w->kinds,(double)w->count,(double)__atomic_load_n(&pm_gaps,__ATOMIC_RELAXED)};
        dd_motion_emit(DD_MOTION_WRITE,phase,serial,m->a,v,7);
        if(phase==1) { m->serial=serial;m->count=w->kinds;m->refs[0]=(uint64_t)dd_motion_now(); }
    }
    pm_leave();
}
static void pm_ready(uint64_t ticket) {
    if(!pm_enter())return;
    double health[]={(double)__atomic_load_n(&pm_gaps,__ATOMIC_RELAXED)};
    dd_motion_emit(DD_MOTION_PRODUCER_LEASE,0,ticket,0,health,1);
    for(unsigned i=0;i<PM_OBJECTS;i++) {
        struct pm_object *m=&pm_objects[i];if(m->type!=PM_MEMORY || !m->a)continue;
        double v[]={(double)m->b,(double)m->serial,(double)m->count,(double)__atomic_load_n(&pm_gaps,__ATOMIC_RELAXED),(double)m->refs[0]};
        dd_motion_emit(DD_MOTION_PRODUCER_LEASE,1,ticket,m->a,v,5);
    }
    pm_leave();
}
