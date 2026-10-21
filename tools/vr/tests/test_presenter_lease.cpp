#define DROIDDECK_PRODUCER_SERVER
#include "../src/presenter.cpp"
#include <cassert>

namespace droiddeck {
namespace {
static pthread_mutex_t producerLock = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<bool> available{true}, copying{false}, copyFailed{false};
static unsigned copies = 0, presents = 0;

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
        p.images_.push_back((VkImage)6); p.extent_ = {3840, 1080};
        p.textures_.push_back({10, (VkImage)7, VK_NULL_HANDLE, 1920, 1080, (uint32_t)getpid(), VK_FORMAT_R8G8B8A8_UNORM});
        p.textures_.push_back({20, (VkImage)8, VK_NULL_HANDLE, 1920, 1080, (uint32_t)getpid(), VK_FORMAT_R8G8B8A8_UNORM});
        p.vk_.WaitForFences = [](VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t timeout) {
            Retained(); assert(timeout == UINT64_MAX);
            if (copying && copyFailed) return VK_ERROR_DEVICE_LOST;
            copying = false; return VK_SUCCESS;
        };
        p.vk_.AcquireNextImageKHR = [](VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *index) {
            Retained(); *index = 0; return VK_SUCCESS;
        };
        p.vk_.ResetFences = [](VkDevice, uint32_t, const VkFence *) { return VK_SUCCESS; };
        p.vk_.ResetCommandBuffer = [](VkCommandBuffer, VkCommandBufferResetFlags) { return VK_SUCCESS; };
        p.vk_.BeginCommandBuffer = [](VkCommandBuffer, const VkCommandBufferBeginInfo *) { return VK_SUCCESS; };
        p.vk_.CmdPipelineBarrier = [](VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
            uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *) {};
        p.vk_.CmdClearColorImage = [](VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *) {};
        p.vk_.CmdBlitImage = [](VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageBlit *, VkFilter) {};
        p.vk_.EndCommandBuffer = [](VkCommandBuffer) { return VK_SUCCESS; };
        p.vk_.QueueSubmit = [](VkQueue, uint32_t, const VkSubmitInfo *, VkFence) {
            Retained(); copying = true; copies++; return VK_SUCCESS;
        };
        p.vk_.DeviceWaitIdle = [](VkDevice) { Retained(); copying = false; return VK_SUCCESS; };
        p.vk_.QueuePresentKHR = [](VkQueue, const VkPresentInfoKHR *) {
            assert(!copying && !copyFailed);
            assert(pthread_mutex_trylock(&producerLock) == 0);
            assert(pthread_mutex_unlock(&producerLock) == 0);
            presents++; return VK_SUCCESS;
        };
        EyeFrame frame{}; frame.texture[0] = 10; frame.texture[1] = 20;
        for (int eye = 0; eye < 2; eye++) frame.bounds[eye][2] = frame.bounds[eye][3] = 1;
        assert(p.Draw(frame) && copies == 1 && presents == 1);
        available = false;
        assert(!p.Draw(frame) && copies == 1 && presents == 1 && p.leaseMisses_ == 1);
        available = true; copyFailed = true;
        assert(!p.Draw(frame) && copies == 2 && presents == 1 && p.failed_);
        assert(pthread_mutex_trylock(&producerLock) == 0);
        assert(pthread_mutex_unlock(&producerLock) == 0);

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
    std::puts("PASS: glasses copy retains producer ownership, skips unavailable leases, and never presents failed GPU work");
}
