#include "../../Source/Utils/HttpServerLifecycle.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

using namespace std::chrono_literals;

class BlockingFakeListener
{
public:
    std::shared_ptr<HttpServerLifecycle::Listener> makeListener()
    {
        auto listener = std::make_shared<HttpServerLifecycle::Listener>();
        listener->listen = [this]
        {
            {
                std::lock_guard<std::mutex> lock (mutex_);
                running_ = true;
            }
            condition_.notify_all();

            std::unique_lock<std::mutex> lock (mutex_);
            condition_.wait (lock, [this] { return stopped_; });
            running_ = false;
            return true;
        };
        listener->isRunning = [this]
        {
            std::lock_guard<std::mutex> lock (mutex_);
            return running_;
        };
        listener->stop = [this]
        {
            {
                std::lock_guard<std::mutex> lock (mutex_);
                ++stopCalls;
                stopped_ = true;
            }
            condition_.notify_all();
        };
        return listener;
    }

    bool waitUntilRunning (std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock (mutex_);
        return condition_.wait_for (lock, timeout, [this] { return running_; });
    }

    int stopCalls = 0;

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool running_ = false;
    bool stopped_ = false;
};

TEST (HttpServerLifecycleTests, CancellationBeforeCommitPreventsListen)
{
    std::promise<void> workerReachedCommit;
    std::promise<void> allowCommit;
    auto allowCommitFuture = allowCommit.get_future().share();
    std::atomic<int> listenCalls { 0 };

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->beforeCommit = [&]
            {
                workerReachedCommit.set_value();
                allowCommitFuture.wait();
            };
            listener->listen = [&]
            {
                ++listenCalls;
                return true;
            };
            listener->isRunning = [] { return false; };
            listener->stop = [] {};
            listener->registerRoutes = [] {};
            return listener;
        });

    lifecycle.start();
    workerReachedCommit.get_future().wait();

    auto stopped = std::async (std::launch::async, [&] { lifecycle.stop(); });
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::cancelled, 1s));

    allowCommit.set_value();
    EXPECT_EQ (stopped.wait_for (1s), std::future_status::ready);
    EXPECT_EQ (listenCalls.load(), 0);
}

TEST (HttpServerLifecycleTests, StopAfterCommitBeforeRunningWaitsThenStopsOnce)
{
    std::promise<void> listenEntered;
    std::promise<void> allowRunning;
    auto allowRunningFuture = allowRunning.get_future().share();
    std::promise<void> runningPublished;
    std::promise<void> isRunningObserved;
    std::promise<void> transportStopped;
    auto transportStoppedFuture = transportStopped.get_future().share();
    std::atomic<bool> running { false };
    std::atomic<int> stopCalls { 0 };
    std::atomic<bool> observedIsRunning { false };

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [&]
            {
                listenEntered.set_value();
                allowRunningFuture.wait();
                running = true;
                runningPublished.set_value();
                transportStoppedFuture.wait();
                running = false;
                return true;
            };
            listener->isRunning = [&]
            {
                if (! observedIsRunning.exchange (true))
                    isRunningObserved.set_value();
                return running.load();
            };
            listener->stop = [&]
            {
                ++stopCalls;
                transportStopped.set_value();
            };
            listener->registerRoutes = [] {};
            return listener;
        });

    lifecycle.start();
    listenEntered.get_future().wait();

    auto stopped = std::async (std::launch::async, [&] { lifecycle.stop(); });
    EXPECT_EQ (isRunningObserved.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_EQ (stopCalls.load(), 0);

    allowRunning.set_value();
    EXPECT_EQ (runningPublished.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_EQ (stopped.wait_for (1s), std::future_status::ready);
    EXPECT_EQ (stopCalls.load(), 1);
}

TEST (HttpServerLifecycleTests, RunningListenerStopsAndJoinsBoundedly)
{
    BlockingFakeListener fake;
    HttpServerLifecycle lifecycle ([&]
                                   {
                                       auto listener = fake.makeListener();
                                       listener->registerRoutes = [] {};
                                       return listener;
                                   });

    lifecycle.start();
    ASSERT_TRUE (fake.waitUntilRunning (1s));

    const auto startedStopping = std::chrono::steady_clock::now();
    lifecycle.stop();
    const auto elapsed = std::chrono::steady_clock::now() - startedStopping;

    EXPECT_EQ (fake.stopCalls, 1);
    EXPECT_LT (elapsed, 1s);
}

TEST (HttpServerLifecycleTests, RestartCreatesFreshGenerationAndRegistersRoutesOnce)
{
    std::vector<std::unique_ptr<BlockingFakeListener>> generations;
    std::atomic<int> registrations { 0 };

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto fake = std::make_unique<BlockingFakeListener>();
            auto* fakePointer = fake.get();
            generations.push_back (std::move (fake));

            auto listener = fakePointer->makeListener();
            listener->registerRoutes = [&] { ++registrations; };
            return listener;
        });

    lifecycle.start();
    ASSERT_TRUE (generations[0]->waitUntilRunning (1s));
    lifecycle.stop();

    lifecycle.start();
    ASSERT_EQ (generations.size(), 2u);
    ASSERT_TRUE (generations[1]->waitUntilRunning (1s));
    lifecycle.stop();

    EXPECT_EQ (registrations.load(), 2);
    EXPECT_EQ (generations[0]->stopCalls, 1);
    EXPECT_EQ (generations[1]->stopCalls, 1);
}

TEST (HttpServerLifecycleTests, ListenFailureIsRestartableAndRepeatedStopIsSafe)
{
    std::atomic<int> generations { 0 };
    std::atomic<int> listens { 0 };
    std::promise<void> firstListenReturned;
    std::promise<void> secondListenReturned;

    HttpServerLifecycle lifecycle (
        [&]
        {
            const auto generation = ++generations;
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [&, generation]
            {
                ++listens;
                (generation == 1 ? firstListenReturned : secondListenReturned).set_value();
                return false;
            };
            listener->registerRoutes = [] {};
            return listener;
        });

    lifecycle.start();
    firstListenReturned.get_future().wait();
    lifecycle.stop();
    lifecycle.stop();

    lifecycle.start();
    secondListenReturned.get_future().wait();
    lifecycle.stop();

    EXPECT_EQ (generations.load(), 2);
    EXPECT_EQ (listens.load(), 2);
}
