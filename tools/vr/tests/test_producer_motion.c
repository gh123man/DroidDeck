#define _GNU_SOURCE
#include <assert.h>
#include "../src/vk_layer_droiddeck_vr.c"
static int draws, begins, copies;
static VkResult VKAPI_CALL begin(VkCommandBuffer c,const VkCommandBufferBeginInfo *i) {begins++;return VK_SUCCESS;}
static void VKAPI_CALL pass(VkCommandBuffer c,const VkRenderPassBeginInfo *i,VkSubpassContents s) {}
static void VKAPI_CALL draw(VkCommandBuffer c,uint32_t v,uint32_t n,uint32_t f,uint32_t instance) {assert(v==3 && n==1);draws++;}
static void VKAPI_CALL copy(VkCommandBuffer c,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t n,const VkImageCopy *r) {assert(dst==(VkImage)12 && n==1);copies++;}
static PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice d,const char *name) {
    if(!strcmp(name,"vkBeginCommandBuffer"))return (PFN_vkVoidFunction)begin;
    if(!strcmp(name,"vkCmdBeginRenderPass"))return (PFN_vkVoidFunction)pass;
    if(!strcmp(name,"vkCmdDraw"))return (PFN_vkVoidFunction)draw;
    if(!strcmp(name,"vkCmdCopyImage"))return (PFN_vkVoidFunction)copy;
    abort();
}
int main(void) {
    void *dispatch=(void*)1;
    VkDevice d=(VkDevice)&dispatch;VkCommandBuffer cmd=(VkCommandBuffer)&dispatch;
    g_dev[0]=(struct dev){.handle=d,.gdpa=gdpa};
    pm_enabled=1;
    struct pm_object *m=pm_put(PM_MEMORY,11,(uint64_t)d);m->a=12345;m->b=678;
    struct pm_object *im=pm_put(PM_IMAGE,12,(uint64_t)d);im->a=11;
    struct pm_object *v=pm_put(PM_VIEW,13,(uint64_t)d);v->a=12;
    struct pm_object *rp=pm_put(PM_PASS,14,(uint64_t)d);rp->count=1;rp->colors[0]=1;
    struct pm_object *fb=pm_put(PM_FRAMEBUFFER,15,(uint64_t)d);fb->refs[0]=13;
    VkCommandBufferBeginInfo start={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    assert(PM_BeginCommandBuffer(cmd,&start)==VK_SUCCESS);
    VkRenderPassBeginInfo render={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=(VkRenderPass)14,.framebuffer=(VkFramebuffer)15};
    PM_CmdBeginRenderPass(cmd,&render,VK_SUBPASS_CONTENTS_INLINE);
    PM_CmdDraw(cmd,3,1,0,0);
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    assert(c->count==1 && c->writes[0].image==12 && c->writes[0].kinds==PM_DRAW);
    struct dd_motion_record records[64];
    dd_mt.rings[0].records=records;dd_mt_slot=0;dd_mt.end=INT64_MAX;dd_mt.active=1;
    pm_submitted(7,(VkQueue)99,cmd,0);
    pm_ready(1);
    assert(m->serial==0);
    assert(records[0].event==DD_MOTION_WRITE && records[0].flags==0 && records[0].related==12345);
    assert(records[2].event==DD_MOTION_PRODUCER_LEASE && records[2].v[1]==0);
    pm_submitted(7,(VkQueue)99,cmd,1);
    pm_ready(2);
    assert(m->serial==7 && records[5].v[1]==7);
    pm_submitted(8,(VkQueue)99,cmd,2);
    assert(m->serial==7);
    assert(PM_BeginCommandBuffer(cmd,&start)==VK_SUCCESS && c->count==0);
    PM_CmdCopyImage(cmd,(VkImage)16,VK_IMAGE_LAYOUT_GENERAL,(VkImage)12,VK_IMAGE_LAYOUT_GENERAL,1,NULL);
    assert(c->count==1 && c->writes[0].kinds==PM_COPY && copies==1);
    assert(!pm_gaps && draws==1 && begins==2);
    pm_remove(PM_VIEW,13);assert(!pm_find(PM_VIEW,13));
    v=pm_put(PM_VIEW,13,(uint64_t)d);assert(v && !v->a);
    assert(!pthread_mutex_lock(&pm_lock));
    pm_write(cmd,(VkImage)12,PM_CLEAR);
    assert(pm_gaps==1);assert(!pthread_mutex_unlock(&pm_lock));
    dd_mt.active=0;
    puts("PASS: producer image write identity, command reset, queued versus completed lease generations, failed submit, and nonblocking coverage gaps");
}
