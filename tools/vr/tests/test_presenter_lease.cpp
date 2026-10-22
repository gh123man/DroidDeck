#include "../../../app/src/main/cpp/waylandcomp/src/motion_trace_impl.h"
#define DROIDDECK_PRODUCER_SERVER
#include "../src/presenter.cpp"
#include <cassert>

namespace droiddeck {
namespace {
static pthread_mutex_t producerLock = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<bool> available{true}, copying{false}, copyFailed{false};
static unsigned copies = 0, presents = 0;
static uint32_t nextImage = 0;
static VkSemaphore pendingPresent[3]{}, submittedSignal{};
static unsigned createdSemaphores = 0, destroyedSemaphores = 0;
static int failSemaphoreAt = -1;
static unsigned pixelBlits = 0, pixelCopies = 0;
static unsigned char pixelData[DD_MOTION_PIXEL_BYTES];

static int Acquire(int64_t) {
    if (!available) return 0;
    return pthread_mutex_lock(&producerLock) == 0;
}
static void Release() {
    assert(!copying);
    assert(pthread_mutex_unlock(&producerLock) == 0);
}
static void Quarantine() { std::abort(); }
static void Retained() { assert(pthread_mutex_trylock(&producerLock) == EBUSY); }

struct PresenterTest {
    static void Run() {
        char directory[] = "/tmp/dd-presenter-XXXXXX";
        assert(mkdtemp(directory));
        assert(setenv("XDG_RUNTIME_DIR", directory, 1) == 0);
        std::string parent = std::string(directory) + "/droiddeck-steamvr";
        assert(mkdir(parent.c_str(), 0700) == 0);
        droiddeck_producer_server server{};
        server.acquire = Acquire; server.release = Release; server.quarantine = Quarantine;
        assert(droiddeck_producer_start(&server));

        Impl p;
        p.producerLease_ = true;
        p.dev_ = (VkDevice)1; p.queue_ = (VkQueue)2; p.cmd_ = (VkCommandBuffer)3;
        p.fence_ = (VkFence)4; p.swapchain_ = (VkSwapchainKHR)5;
        p.images_ = {(VkImage)6, (VkImage)9, (VkImage)11}; p.extent_ = {3840, 1080};
        p.vk_.CreateSemaphore = [](VkDevice, const VkSemaphoreCreateInfo *, const VkAllocationCallbacks *, VkSemaphore *out) {
            if ((int)createdSemaphores == failSemaphoreAt) return VK_ERROR_OUT_OF_HOST_MEMORY;
            *out = (VkSemaphore)(uintptr_t)(100 + createdSemaphores++); return VK_SUCCESS;
        };
        p.vk_.DestroySemaphore = [](VkDevice, VkSemaphore semaphore, const VkAllocationCallbacks *) {
            assert(semaphore); destroyedSemaphores++;
        };
        p.vk_.DestroySwapchainKHR = [](VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *) {};
        assert(p.CreatePresentSemaphores() && createdSemaphores == 3);
        p.textures_.push_back({10, (VkImage)7, VK_NULL_HANDLE, 1920, 1080, (uint32_t)getpid(), VK_FORMAT_R8G8B8A8_UNORM});
        p.textures_.push_back({20, (VkImage)8, VK_NULL_HANDLE, 1920, 1080, (uint32_t)getpid(), VK_FORMAT_R8G8B8A8_UNORM});
        p.vk_.WaitForFences = [](VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t timeout) {
            Retained(); assert(timeout == UINT64_MAX);
            if (copying && copyFailed) return VK_ERROR_DEVICE_LOST;
            copying = false; return VK_SUCCESS;
        };
        p.vk_.AcquireNextImageKHR = [](VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *index) {
            Retained(); *index = nextImage; pendingPresent[nextImage] = VK_NULL_HANDLE; return VK_SUCCESS;
        };
        p.vk_.ResetFences = [](VkDevice, uint32_t, const VkFence *) { return VK_SUCCESS; };
        p.vk_.ResetCommandBuffer = [](VkCommandBuffer, VkCommandBufferResetFlags) { return VK_SUCCESS; };
        p.vk_.BeginCommandBuffer = [](VkCommandBuffer, const VkCommandBufferBeginInfo *) { return VK_SUCCESS; };
        p.vk_.CmdPipelineBarrier = [](VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
            uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *) {};
        p.vk_.CmdClearColorImage = [](VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *) {};
        p.vk_.CmdBlitImage = [](VkCommandBuffer, VkImage src, VkImageLayout layout, VkImage dst, VkImageLayout,
                               uint32_t count, const VkImageBlit *blit, VkFilter) {
            if (dst != (VkImage)71) return;
            Retained(); assert(src == (VkImage)7 && layout == VK_IMAGE_LAYOUT_GENERAL && count == 1);
            assert(blit->srcOffsets[1].x == 1920 && blit->srcOffsets[1].y == 1080);
            assert(blit->dstOffsets[1].x == 128 && blit->dstOffsets[1].y == 72);
            pixelBlits++;
        };
        p.vk_.CmdCopyImageToBuffer = [](VkCommandBuffer, VkImage image, VkImageLayout layout,
                                       VkBuffer buffer, uint32_t count, const VkBufferImageCopy *copy) {
            Retained(); assert(pixelBlits == 1 && image == (VkImage)71 && buffer == (VkBuffer)72);
            assert(layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && count == 1);
            assert(copy->imageExtent.width == 128 && copy->imageExtent.height == 72);
            pixelCopies++;
        };
        p.vk_.InvalidateMappedMemoryRanges = [](VkDevice, uint32_t count, const VkMappedMemoryRange *range) {
            assert(!copying && pixelCopies == 1 && count == 1 && range->memory == (VkDeviceMemory)73);
            return VK_SUCCESS;
        };
        p.vk_.EndCommandBuffer = [](VkCommandBuffer) { return VK_SUCCESS; };
        p.vk_.QueueSubmit = [](VkQueue, uint32_t, const VkSubmitInfo *info, VkFence) {
            assert(info->signalSemaphoreCount == 1);
            submittedSignal = *info->pSignalSemaphores;
            assert(submittedSignal);
            for (auto pending : pendingPresent) assert(pending != submittedSignal);
            Retained(); copying = true; copies++; return VK_SUCCESS;
        };
        p.vk_.DeviceWaitIdle = [](VkDevice) { Retained(); copying = false; return VK_SUCCESS; };
        p.vk_.QueuePresentKHR = [](VkQueue, const VkPresentInfoKHR *info) {
            assert(!copying && !copyFailed);
            assert(info->waitSemaphoreCount == 1 && *info->pWaitSemaphores == submittedSignal);
            pendingPresent[*info->pImageIndices] = submittedSignal;
            assert(pthread_mutex_trylock(&producerLock) == 0);
            assert(pthread_mutex_unlock(&producerLock) == 0);
            presents++; return VK_SUCCESS;
        };
        EyeFrame frame{}; frame.texture[0] = 10; frame.texture[1] = 20;
        for (int eye = 0; eye < 2; eye++) frame.bounds[eye][2] = frame.bounds[eye][3] = 1;
        for (auto index : {0u, 2u, 1u, 2u, 0u}) {
            nextImage = index;
            if (copies == 4) {
                p.captureWidth_ = 128; p.captureHeight_ = 72; p.captureImage_ = (VkImage)71;
                p.readbackBuffer_ = (VkBuffer)72; p.readbackMemory_ = (VkDeviceMemory)73;
                memset(pixelData, 123, sizeof(pixelData)); p.motionMapping_ = pixelData;
                dd_mt.pixels = (dd_motion_pixels *)calloc(1, sizeof(dd_motion_pixels));
                assert(dd_mt.pixels); dd_mt.pixels_enabled = 1; dd_mt.end = INT64_MAX;
                dd_mt.active = 1; dd_mt_slot = -2; frame.render.frame_id = 123;
            }
            assert(p.Draw(frame));
        }
        assert(copies == 5 && presents == 5);
        assert(dd_mt.pixels_count == 1 && !dd_mt.pixels_dropped);
        assert(dd_mt.pixels[0].frame == 123 && dd_mt.pixels[0].texture == 10);
        assert(!memcmp(dd_mt.pixels[0].rgba, pixelData, sizeof(pixelData)));
        dd_mt.active = dd_mt.pixels_enabled = 0; free(dd_mt.pixels); dd_mt.pixels = nullptr;
        p.motionMapping_ = nullptr; p.captureImage_ = VK_NULL_HANDLE;
        p.readbackBuffer_ = VK_NULL_HANDLE; p.readbackMemory_ = VK_NULL_HANDLE;
        available = false;
        assert(!p.Draw(frame) && copies == 5 && presents == 5 && p.leaseMisses_ == 1);
        available = true; copyFailed = true;
        assert(!p.Draw(frame) && copies == 6 && presents == 5 && p.failed_);
        assert(pthread_mutex_trylock(&producerLock) == 0);
        assert(pthread_mutex_unlock(&producerLock) == 0);
        p.DestroySwapchain();
        assert(destroyedSemaphores == 3 && p.rendered_.empty() && p.images_.empty() && !p.swapchain_);
        p.images_.resize(3);
        failSemaphoreAt = createdSemaphores + 1;
        assert(!p.CreatePresentSemaphores());
        p.DestroySwapchain();
        assert(destroyedSemaphores == 4);

        assert(pthread_cancel(server.thread) == 0);
        assert(pthread_join(server.thread, nullptr) == 0);
        close(server.listener); unlink(server.path);
        assert(rmdir(parent.c_str()) == 0 && rmdir(directory) == 0);
    }
};
}
}

int main() {
    droiddeck::PresenterTest::Run();
    std::puts("PASS: glasses copy retains producer ownership, preserves pending presentation semaphores across non-sequential image reuse, and rejects failed GPU work");
}
