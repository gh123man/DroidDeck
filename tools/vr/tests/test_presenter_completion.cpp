#include "../../../app/src/main/cpp/waylandcomp/src/motion_trace_impl.h"
#include "../src/presenter.cpp"
#include <cassert>
#include <future>

namespace droiddeck {
namespace {
struct PresenterTest {
    static void Run() {
        unsetenv("DROIDDECK_VR_PRESENT");
        Impl presenter;
        presenter.started_ = true;
        EyeFrame frame{};
        auto submitted = std::async(std::launch::async, [&] { presenter.Submit(frame); });
        {
            std::unique_lock<std::mutex> lock(presenter.mutex_);
            assert(presenter.cv_.wait_for(lock, std::chrono::seconds(2), [&] { return presenter.haveFrame_; }));
            presenter.haveFrame_ = false;
            presenter.inFlight_ = true;
        }
        assert(submitted.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
        {
            std::lock_guard<std::mutex> lock(presenter.mutex_);
            presenter.inFlight_ = false;
            presenter.completed_ = presenter.ticket_;
            presenter.cv_.notify_all();
        }
        assert(submitted.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        submitted.get();

        auto interrupted = std::async(std::launch::async, [&] { presenter.Submit(frame); });
        {
            std::unique_lock<std::mutex> lock(presenter.mutex_);
            assert(presenter.cv_.wait_for(lock, std::chrono::seconds(2), [&] { return presenter.haveFrame_; }));
        }
        presenter.Shutdown();
        assert(interrupted.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        interrupted.get();
    }
};
}
}

int main() {
    droiddeck::PresenterTest::Run();
    std::puts("PASS: flat presentation retains the frame until copying completes and shutdown wakes the submitter");
}
