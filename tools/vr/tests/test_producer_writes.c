#define _GNU_SOURCE
#include <assert.h>
#include "../src/vk_layer_droiddeck_vr.c"
static int draws, begins, copies;
static VkResult submit_result;
static VkResult VKAPI_CALL submit(VkQueue q, uint32_t n, const VkSubmitInfo *s, VkFence f) { return submit_result; }
static VkResult VKAPI_CALL begin(VkCommandBuffer c, const VkCommandBufferBeginInfo *i) {
    begins++;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL end(VkCommandBuffer c) { return VK_SUCCESS; }
static void VKAPI_CALL pass(VkCommandBuffer c, const VkRenderPassBeginInfo *i, VkSubpassContents s) {}
static void VKAPI_CALL draw(VkCommandBuffer c, uint32_t v, uint32_t n, uint32_t f, uint32_t instance) {
    assert(v == 3 && n == 1);
    draws++;
}
static void VKAPI_CALL copy(VkCommandBuffer c, VkImage src, VkImageLayout sl, VkImage dst, VkImageLayout dl, uint32_t n,
                            const VkImageCopy *r) {
    assert(dst == (VkImage)12 && n == 1);
    copies++;
}
static PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice d, const char *name) {
    if (!strcmp(name, "vkQueueSubmit"))
        return (PFN_vkVoidFunction)submit;
    if (!strcmp(name, "vkEndCommandBuffer"))
        return (PFN_vkVoidFunction)end;
    if (!strcmp(name, "vkBeginCommandBuffer"))
        return (PFN_vkVoidFunction)begin;
    if (!strcmp(name, "vkCmdBeginRenderPass"))
        return (PFN_vkVoidFunction)pass;
    if (!strcmp(name, "vkCmdDraw"))
        return (PFN_vkVoidFunction)draw;
    if (!strcmp(name, "vkCmdCopyImage"))
        return (PFN_vkVoidFunction)copy;
    abort();
}
int main(void) {
    void *dispatch = (void *)1;
    VkDevice d = (VkDevice)&dispatch;
    VkCommandBuffer cmd = (VkCommandBuffer)&dispatch;
    g_dev[0] = (struct dev){.handle = d, .gdpa = gdpa};
    pw_enabled = 1;
    struct pw_object *m = pw_put(PW_MEMORY, 11);
    m->a = 12345;
    m->b = 678;
    struct pw_object *im = pw_put(PW_IMAGE, 12);
    im->a = 11;
    struct pw_object *v = pw_put(PW_VIEW, 13);
    v->a = 12;
    struct pw_object *rp = pw_put(PW_PASS, 14);
    rp->count = 1;
    rp->colors[0] = 1;
    struct pw_object *fb = pw_put(PW_FRAMEBUFFER, 15);
    fb->refs[0] = 13;
    VkCommandBufferBeginInfo start = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    assert(PW_BeginCommandBuffer(cmd, &start) == VK_SUCCESS);
    VkRenderPassBeginInfo render = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                    .renderPass = (VkRenderPass)14,
                                    .framebuffer = (VkFramebuffer)15};
    PW_CmdBeginRenderPass(cmd, &render, VK_SUBPASS_CONTENTS_INLINE);
    PW_CmdDraw(cmd, 3, 1, 0, 0);
    struct pw_object *c = pw_find(PW_COMMAND, (uint64_t)cmd);
    assert(c->count == 1 && c->writes[0] == 12);
    assert(PW_EndCommandBuffer(cmd) == VK_SUCCESS && c->ended);
    struct droiddeck_producer_target targets[2] = {{12345, 678, pw_reserve()}, {12345, 678, pw_reserve()}};
    assert(targets[0].minimum == 1 && !pw_targets_ready(targets));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    submit_result = VK_ERROR_DEVICE_LOST;
    assert(QueueSubmit((VkQueue)&dispatch, 1, &submit, VK_NULL_HANDLE) == VK_ERROR_DEVICE_LOST);
    assert(m->ready_serial == 0 && !pw_targets_ready(targets));
    submit_result = VK_SUCCESS;
    assert(QueueSubmit((VkQueue)&dispatch, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
    assert(m->ready_serial == 2 && pw_targets_ready(targets) == 1);
    targets[0].minimum = targets[1].minimum = pw_reserve();
    assert(!pw_targets_ready(targets));
    assert(PW_BeginCommandBuffer(cmd, &start) == VK_SUCCESS && c->count == 0);
    PW_CmdCopyImage(cmd, (VkImage)16, VK_IMAGE_LAYOUT_GENERAL, (VkImage)12, VK_IMAGE_LAYOUT_GENERAL, 1, NULL);
    PW_CmdCopyImage(cmd, (VkImage)16, VK_IMAGE_LAYOUT_GENERAL, (VkImage)12, VK_IMAGE_LAYOUT_GENERAL, 1, NULL);
    assert(c->count == 1 && copies == 2);
    pw_submitted(3, cmd);
    assert(m->ready_serial == 2);
    assert(PW_EndCommandBuffer(cmd) == VK_SUCCESS && c->ended);
    assert(QueueSubmit((VkQueue)&dispatch, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
    assert(pw_targets_ready(targets) == 1);
    targets[1].inode = 99999;
    assert(!pw_targets_ready(targets));
    targets[1].inode = 12345;
    targets[1].device = 999;
    assert(!pw_targets_ready(targets));
    targets[1].device = 678;
    assert(PW_BeginCommandBuffer(cmd, &start) == VK_SUCCESS);
    PW_CmdCopyImage(cmd, (VkImage)16, VK_IMAGE_LAYOUT_GENERAL, (VkImage)12, VK_IMAGE_LAYOUT_GENERAL, 1, NULL);
    pw_write(cmd, (VkImage)999);
    assert(PW_EndCommandBuffer(cmd) == VK_SUCCESS && !c->ended);
    pw_submitted(4, cmd);
    assert(m->ready_serial == 3);
    assert(PW_BeginCommandBuffer(cmd, &start) == VK_SUCCESS);
    PW_CmdCopyImage(cmd, (VkImage)16, VK_IMAGE_LAYOUT_GENERAL, (VkImage)12, VK_IMAGE_LAYOUT_GENERAL, 1, NULL);
    assert(PW_EndCommandBuffer(cmd) == VK_SUCCESS && c->ended);
    pw_submitted(5, cmd);
    assert(m->ready_serial == 5);
    pw_remove(PW_VIEW, 13);
    assert(!pw_find(PW_VIEW, 13));
    v = pw_put(PW_VIEW, 13);
    assert(v && !v->a);
    pw_gap(PW_META_GAP);
    assert(pw_targets_ready(targets) == -1);
    pw_enabled = 0;
    assert(pw_reserve() == 0 && pw_targets_ready(targets) == -1);
    puts("PASS: texture readiness, failed submissions, command reset, identity matching, and incomplete tracking");
}
