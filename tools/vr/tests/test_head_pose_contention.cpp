#include "../src/driver_droiddeck.cpp"
#include <cassert>

void droiddeck::Presenter::AddTextures(const uint64_t[3], const int[3], uint32_t, uint32_t, uint32_t, uint32_t) {}
void droiddeck::Presenter::RemoveTexture(uint64_t) {}
void droiddeck::Presenter::Submit(const EyeFrame &) {}
void droiddeck::Presenter::Shutdown() {}

struct TestContext : IVRDriverContext {
    void *GetGenericInterface(const char *, EVRInitError *) override { return nullptr; }
    DriverHandle_t GetDriverHandle() override { return 1; }
};

int main() {
    TestContext context;
    VRDriverContext() = &context;
    char path[] = "/tmp/droiddeck-headpose-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && ftruncate(fd, sizeof(droiddeck_headpose)) == 0);
    auto *shared = static_cast<droiddeck_headpose *>(mmap(nullptr, sizeof(droiddeck_headpose), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    assert(shared != MAP_FAILED);
    close(fd);
    *shared = {};
    shared->magic = DROIDDECK_HEADPOSE_MAGIC;
    shared->version = DROIDDECK_HEADPOSE_VERSION;
    shared->seq = 2;
    shared->flags = DROIDDECK_HEADPOSE_LIVE | DROIDDECK_HEADPOSE_ENABLED;
    shared->time_ns = XrTrackingFile::Now();
    const float yaw90[] = {0,0,1, 0,1,0, -1,0,0};
    std::memcpy(shared->rot, yaw90, sizeof(yaw90));
    {
        CardboardHmd hmd(path);
        const auto before = hmd.GetPose();
        assert(std::abs(before.qRotation.y - std::sqrt(.5)) < 1e-5);
        __atomic_store_n(&shared->seq, 3u, __ATOMIC_RELEASE);
        std::fill_n(shared->rot, 9, 0.f);
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto during = hmd.GetPose();
            assert(during.qRotation.w == before.qRotation.w);
            assert(during.qRotation.y == before.qRotation.y);
        }
        const float yaw180[] = {-1,0,0, 0,1,0, 0,0,-1};
        std::memcpy(shared->rot, yaw180, sizeof(yaw180));
        __atomic_store_n(&shared->seq, 4u, __ATOMIC_RELEASE);
        const auto after = hmd.GetPose();
        assert(std::abs(after.qRotation.w) < 1e-5 && after.qRotation.y == 1.0);
        shared->magic = 0;
        const auto malformed = hmd.GetPose();
        assert(malformed.qRotation.y == after.qRotation.y);
    }
    munmap(shared, sizeof(*shared));
    unlink(path);
    std::puts("PASS: head orientation survives concurrent writes and invalid snapshots, then resumes new poses");
}
