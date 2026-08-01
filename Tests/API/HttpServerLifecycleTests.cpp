#include "../../Source/Utils/HttpServerLifecycle.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

using namespace std::chrono_literals;

class ScopeExit
{
public:
    explicit ScopeExit (std::function<void()> operation) : operation_ (std::move (operation)) {}
    ~ScopeExit() { operation_(); }

private:
    std::function<void()> operation_;
};

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
    ScopeExit releaseCommit ([&]
                             {
                                 try { allowCommit.set_value(); } catch (...) {}
                             });
    const auto reachedCommit = workerReachedCommit.get_future().wait_for (1s);
    EXPECT_EQ (reachedCommit, std::future_status::ready);
    if (reachedCommit != std::future_status::ready)
        return;

    auto stopped = std::async (std::launch::async, [&] { lifecycle.stop(); });
    const auto cancelled = lifecycle.waitForState (ListenerStartGate::State::cancelled, 1s);

    allowCommit.set_value();
    EXPECT_TRUE (cancelled);
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
    std::vector<int> registrations;

    HttpServerLifecycle lifecycle (
        [&]
        {
            const auto generationIndex = generations.size();
            auto fake = std::make_unique<BlockingFakeListener>();
            auto* fakePointer = fake.get();
            generations.push_back (std::move (fake));
            registrations.push_back (0);

            auto listener = fakePointer->makeListener();
            listener->registerRoutes = [&, generationIndex] { ++registrations[generationIndex]; };
            return listener;
        });

    lifecycle.start();
    ASSERT_TRUE (generations[0]->waitUntilRunning (1s));
    lifecycle.stop();

    lifecycle.start();
    ASSERT_EQ (generations.size(), 2u);
    ASSERT_TRUE (generations[1]->waitUntilRunning (1s));
    lifecycle.stop();

    ASSERT_EQ (registrations.size(), 2u);
    EXPECT_EQ (registrations[0], 1);
    EXPECT_EQ (registrations[1], 1);
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
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));
    lifecycle.start();
    secondListenReturned.get_future().wait();
    lifecycle.stop();
    lifecycle.stop();

    EXPECT_EQ (generations.load(), 2);
    EXPECT_EQ (listens.load(), 2);
}

TEST (HttpServerLifecycleTests, TimedOutStopRetainsItsGenerationUntilTheWorkerExits)
{
    std::promise<void> running;
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    std::atomic<int> generations { 0 };
    std::atomic<bool> isRunning { false };

    HttpServerLifecycle lifecycle (
        [&]
        {
            ++generations;
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [&]
            {
                isRunning = true;
                running.set_value();
                releaseFuture.wait();
                isRunning = false;
                return true;
            };
            listener->isRunning = [&] { return isRunning.load(); };
            listener->stop = [] {};
            return listener;
        },
        50ms);

    lifecycle.start();
    ASSERT_EQ (running.get_future().wait_for (1s), std::future_status::ready);

    EXPECT_FALSE (lifecycle.stop());
    lifecycle.start();
    EXPECT_EQ (generations.load(), 1);

    release.set_value();
    EXPECT_TRUE (lifecycle.stop());
}

TEST (HttpServerLifecycleTests, FactoryAndRegistrarFailuresDoNotWedgeLaterStarts)
{
    std::atomic<int> factoryCalls { 0 };
    std::atomic<int> registrarCalls { 0 };
    std::promise<void> listened;

    HttpServerLifecycle lifecycle (
        [&]
        {
            const auto call = ++factoryCalls;
            if (call == 1)
                throw std::runtime_error ("factory failed");

            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->registerRoutes = [&, call]
            {
                ++registrarCalls;
                if (call == 2)
                    throw std::runtime_error ("registrar failed");
            };
            listener->listen = [&]
            {
                listened.set_value();
                return false;
            };
            return listener;
        });

    EXPECT_THROW (lifecycle.start(), std::runtime_error);
    EXPECT_THROW (lifecycle.start(), std::runtime_error);
    lifecycle.start();
    EXPECT_EQ (listened.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_TRUE (lifecycle.stop());
    EXPECT_EQ (factoryCalls.load(), 3);
    EXPECT_EQ (registrarCalls.load(), 2);
}

TEST (HttpServerLifecycleTests, ConcurrentStartAndStopTransitionsAreLinearized)
{
    std::vector<std::unique_ptr<BlockingFakeListener>> generations;
    std::vector<int> registrations;

    HttpServerLifecycle lifecycle (
        [&]
        {
            const auto generationIndex = generations.size();
            auto fake = std::make_unique<BlockingFakeListener>();
            auto* fakePointer = fake.get();
            generations.push_back (std::move (fake));
            registrations.push_back (0);

            auto listener = fakePointer->makeListener();
            listener->registerRoutes = [&, generationIndex] { ++registrations[generationIndex]; };
            return listener;
        });

    lifecycle.start();
    ASSERT_TRUE (generations[0]->waitUntilRunning (1s));

    std::promise<void> go;
    auto goFuture = go.get_future().share();
    auto starter = std::async (std::launch::async, [&]
                               {
                                   goFuture.wait();
                                   lifecycle.start();
                               });
    auto stopper = std::async (std::launch::async, [&]
                               {
                                   goFuture.wait();
                                   return lifecycle.stop();
                               });
    go.set_value();

    EXPECT_EQ (starter.wait_for (1s), std::future_status::ready);
    EXPECT_EQ (stopper.wait_for (1s), std::future_status::ready);
    EXPECT_TRUE (stopper.get());
    ASSERT_TRUE (generations.size() == 1u || generations.size() == 2u);
    ASSERT_EQ (registrations.size(), generations.size());
    for (auto calls : registrations)
        EXPECT_EQ (calls, 1);

    if (generations.size() == 2u)
    {
        EXPECT_TRUE (generations[1]->waitUntilRunning (1s));
        EXPECT_TRUE (lifecycle.stop());
    }
}

TEST (HttpServerLifecycleTests, HandshakeAndWorkerExitShareOneDeadline)
{
    std::atomic<int> clockCalls { 0 };
    std::chrono::steady_clock::time_point firstStopClockRead;
    std::promise<void> listenerRunning;
    std::promise<void> listenerStopped;
    auto listenerStoppedFuture = listenerStopped.get_future().share();
    std::promise<int> workerWaitBudget;

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [&]
            {
                listenerRunning.set_value();
                listenerStoppedFuture.wait();
                return true;
            };
            listener->isRunning = [] { return true; };
            listener->stop = [&] { listenerStopped.set_value(); };
            listener->onWorkerExitWait = [&] (int remainingMilliseconds)
            {
                workerWaitBudget.set_value (remainingMilliseconds);
            };
            return listener;
        },
        500ms,
        [&]
        {
            if (clockCalls.fetch_add (1) == 0)
            {
                firstStopClockRead = std::chrono::steady_clock::now();
                return firstStopClockRead;
            }

            return firstStopClockRead + 300ms;
        });

    lifecycle.start();
    ASSERT_EQ (listenerRunning.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_TRUE (lifecycle.stop());
    EXPECT_LE (workerWaitBudget.get_future().get(), 200);
}

TEST (HttpServerLifecycleTests, TransientTransportStopFailureIsRetriedByFinalTeardown)
{
    std::promise<void> running;
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    std::atomic<int> stopCalls { 0 };

    auto lifecycle = std::make_unique<HttpServerLifecycle> (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [&]
            {
                running.set_value();
                releaseFuture.wait();
                return true;
            };
            listener->isRunning = [] { return true; };
            listener->stop = [&]
            {
                if (++stopCalls == 1)
                    throw std::runtime_error ("transient stop failure");
                release.set_value();
            };
            return listener;
        },
        50ms);

    lifecycle->start();
    const auto listenerRunning = running.get_future().wait_for (1s);
    EXPECT_EQ (listenerRunning, std::future_status::ready);
    if (listenerRunning != std::future_status::ready)
    {
        try { release.set_value(); } catch (...) {}
        return;
    }

    std::future<void> destroyed;
    ScopeExit releaseOnFailure ([&]
                                {
                                    try { release.set_value(); } catch (...) {}
                                });
    destroyed = std::async (std::launch::async, [&] { lifecycle.reset(); });

    auto completed = destroyed.wait_for (1s);
    if (completed != std::future_status::ready)
    {
        try { release.set_value(); } catch (...) {}
        completed = destroyed.wait_for (1s);
    }

    EXPECT_EQ (completed, std::future_status::ready);
    EXPECT_EQ (stopCalls.load(), 2);
}

TEST (HttpServerLifecycleTests, DestructorCompletesAfterAPublicTimeoutAndControlledRelease)
{
    std::promise<void> running;
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    std::atomic<bool> listenerRunning { false };

    auto lifecycle = std::make_unique<HttpServerLifecycle> (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [&]
            {
                listenerRunning = true;
                running.set_value();
                releaseFuture.wait();
                listenerRunning = false;
                return true;
            };
            listener->isRunning = [&] { return listenerRunning.load(); };
            listener->stop = [] {};
            return listener;
        },
        50ms);

    lifecycle->start();
    const auto started = running.get_future().wait_for (1s);
    EXPECT_EQ (started, std::future_status::ready);
    if (started != std::future_status::ready)
    {
        try { release.set_value(); } catch (...) {}
        return;
    }

    EXPECT_FALSE (lifecycle->stop());
    std::future<void> destroyed;
    ScopeExit releaseOnFailure ([&]
                                {
                                    try { release.set_value(); } catch (...) {}
                                });
    destroyed = std::async (std::launch::async, [&] { lifecycle.reset(); });

    EXPECT_EQ (destroyed.wait_for (50ms), std::future_status::timeout);
    release.set_value();
    EXPECT_EQ (destroyed.wait_for (1s), std::future_status::ready);
}
