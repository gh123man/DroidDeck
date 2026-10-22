#include "../../../app/src/main/cpp/waylandcomp/src/motion_trace.h"
void dd_motion_emit(uint32_t event, uint32_t flags, uint64_t id, uint64_t related,
                    const double *values, unsigned count) {}
static const uint64_t motion_id = 1;
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

static VkDevice g_dev;
static VkQueue g_queue;
static VkSwapchainKHR g_swapchain;
static VkFence g_fence;
static VkCommandBuffer g_cmds[4];
static VkSemaphore g_acqs[4], *g_rnds;
static uint32_t g_nimg = 3;
static int g_dev_state = 1;
static unsigned made, destroyed, shown;
static int fail_at = -1;
static VkSemaphore pending[3], signalled;

static VkResult create(VkDevice dev, const VkSemaphoreCreateInfo *info,
                       const VkAllocationCallbacks *alloc, VkSemaphore *out) {
    if ((int)made == fail_at) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *out = (VkSemaphore)(uintptr_t)(100 + made++);
    return VK_SUCCESS;
}
static void destroy(VkDevice dev, VkSemaphore semaphore, const VkAllocationCallbacks *alloc) {
    assert(semaphore); destroyed++;
}
static VkResult submit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence) {
    assert(count == 1 && info->signalSemaphoreCount == 1);
    signalled = *info->pSignalSemaphores;
    assert(signalled);
    for (unsigned i = 0; i < 3; i++) assert(pending[i] != signalled);
    return VK_SUCCESS;
}
static VkResult present(VkQueue queue, const VkPresentInfoKHR *info) {
    assert(info->swapchainCount == 1 && info->waitSemaphoreCount == 1);
    assert(*info->pWaitSemaphores == signalled);
    pending[*info->pImageIndices] = signalled;
    shown++;
    return VK_SUCCESS;
}
static struct {
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkQueuePresentKHR QueuePresentKHR;
} g_vk = {create, destroy, submit, present};

#include "lifecycle.inc"

#define LOGE(...) abort()
static void destroy_swapchain(void) { abort(); }
static void device_lost(const char *where) { abort(); }
static int64_t perf_now(void) { return 0; }

static int frame(int k, uint32_t img) {
    int presents = k + 1, fence_submitted = 0;
    VkResult pr;
    pending[img] = VK_NULL_HANDLE;
    #include "submission.inc"
    return pr;
}

int main(void) {
    assert(create_present_semaphores() == 0 && made == 3);
    const uint32_t images[] = {0, 2, 1, 2, 0, 1};
    for (unsigned i = 0; i < 6; i++) assert(frame(0, images[i]) == VK_SUCCESS);
    for (unsigned i = 0; i < 6; i++) assert(frame(i % 4, images[i]) == VK_SUCCESS);
    assert(shown == 12);
    destroy_present_semaphores();
    assert(!g_rnds && destroyed == made);
    fail_at = made + 1;
    assert(create_present_semaphores() == -1 && !g_rnds && destroyed == made);
    destroy_present_semaphores();
    assert(destroyed == made);
    puts("PASS: Android presentation keeps pending semaphore waits isolated across image order and frame-generation slots; partial allocation cleans up");
}
