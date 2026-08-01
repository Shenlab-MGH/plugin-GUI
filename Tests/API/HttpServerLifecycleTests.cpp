#include "../../Source/Utils/HttpServerLifecycle.h"
#include "../../Source/Utils/MessageThreadCall.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

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
            listener->registerRoutes = [] (MessageThreadCallGeneration&) {};
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
            listener->registerRoutes = [] (MessageThreadCallGeneration&) {};
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
                                       listener->registerRoutes = [] (MessageThreadCallGeneration&) {};
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

TEST (HttpServerLifecycleTests, StopClosesAdmissionAndQuiescesPendingCallBeforeTransportStop)
{
    struct Probe
    {
        MessageThreadCallGeneration generation;
        MessageThreadCallGeneration::Registration orderingRegistration;
        std::promise<void> queued;
        std::shared_future<void> queuedFuture = queued.get_future().share();
        std::promise<void> callerFinished;
        std::shared_future<void> callerFinishedFuture = callerFinished.get_future().share();
        std::function<void()> lateCallback;
        std::vector<int> order;
        std::atomic<int> operationCalls { 0 };
        MessageThreadCallResult<int> pendingResult;
        MessageThreadCallResult<int> rejectedDuringTransportStop;
        bool rejectedDispatcherCalled = false;
    };

    auto probe = std::make_shared<Probe>();
    std::thread pendingCaller;
    std::promise<void> listenerRunning;
    std::promise<void> transportStopped;
    auto transportStoppedFuture = transportStopped.get_future().share();
    std::atomic<bool> running { false };

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->registerRoutes = [&, probe] (MessageThreadCallGeneration& generation)
            {
                probe->generation = generation;
                probe->orderingRegistration = generation.registerCancellation (
                    [probe] { probe->order.push_back (2); });
                pendingCaller = std::thread (
                    [probe]
                    {
                        probe->pendingResult = runDispatchedCall (
                            [probe]
                            {
                                ++probe->operationCalls;
                                return 42;
                            },
                            [probe] (std::function<void()> callback)
                            {
                                probe->lateCallback = std::move (callback);
                                probe->queued.set_value();
                                return true;
                            },
                            5s,
                            probe->generation);
                        probe->callerFinished.set_value();
                    });

                if (probe->queuedFuture.wait_for (1s) != std::future_status::ready)
                    throw std::runtime_error ("pending call queue handshake failed");
            };
            listener->closeAdmission = [probe] { probe->order.push_back (1); };
            listener->listen = [&]
            {
                running = true;
                listenerRunning.set_value();
                transportStoppedFuture.wait();
                running = false;
                return true;
            };
            listener->isRunning = [&] { return running.load(); };
            listener->stop = [&, probe]
            {
                probe->order.push_back (3);
                probe->rejectedDuringTransportStop = runDispatchedCall (
                    [] { return 7; },
                    [probe] (std::function<void()>)
                    {
                        probe->rejectedDispatcherCalled = true;
                        return true;
                    },
                    100ms,
                    probe->generation);
                transportStopped.set_value();
            };
            return listener;
        });
    ScopeExit cleanup ([&]
                       {
                           probe->generation.quiesce();
                           try { transportStopped.set_value(); } catch (...) {}
                           if (pendingCaller.joinable())
                               pendingCaller.join();
                       });

    lifecycle.start();
    ASSERT_EQ (listenerRunning.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_TRUE (lifecycle.stop());
    ASSERT_EQ (probe->callerFinishedFuture.wait_for (1s), std::future_status::ready);
    pendingCaller.join();

    EXPECT_EQ (probe->order, (std::vector<int> { 1, 2, 3 }));
    EXPECT_EQ (probe->pendingResult.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (probe->rejectedDuringTransportStop.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_FALSE (probe->rejectedDispatcherCalled);
    EXPECT_EQ (probe->operationCalls.load(), 0);
    ASSERT_TRUE (probe->lateCallback);
    probe->lateCallback();
    EXPECT_EQ (probe->operationCalls.load(), 0);
}

TEST (HttpServerLifecycleTests, PendingBeforeListenStillClosesAdmissionAndQuiesces)
{
    struct Probe
    {
        MessageThreadCallGeneration::Registration cancellationRegistration;
        std::vector<int> order;
    };

    auto probe = std::make_shared<Probe>();
    std::promise<void> workerBeforeCommit;
    std::promise<void> allowCommit;
    auto allowCommitFuture = allowCommit.get_future().share();
    std::promise<void> admissionClosed;
    std::promise<void> generationQuiesced;
    std::atomic<int> listenCalls { 0 };
    std::atomic<int> transportStopCalls { 0 };

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->registerRoutes = [probe, &generationQuiesced] (MessageThreadCallGeneration& generation)
            {
                probe->cancellationRegistration = generation.registerCancellation (
                    [probe, &generationQuiesced]
                    {
                        probe->order.push_back (2);
                        generationQuiesced.set_value();
                    });
            };
            listener->closeAdmission = [probe, &admissionClosed]
            {
                probe->order.push_back (1);
                admissionClosed.set_value();
            };
            listener->beforeCommit = [&]
            {
                workerBeforeCommit.set_value();
                allowCommitFuture.wait();
            };
            listener->listen = [&]
            {
                ++listenCalls;
                return true;
            };
            listener->stop = [&] { ++transportStopCalls; };
            return listener;
        });
    ScopeExit releaseCommit ([&]
                             {
                                 try { allowCommit.set_value(); } catch (...) {}
                             });

    lifecycle.start();
    ASSERT_EQ (workerBeforeCommit.get_future().wait_for (1s), std::future_status::ready);
    auto stopped = std::async (std::launch::async, [&] { return lifecycle.stop(); });

    EXPECT_EQ (admissionClosed.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_EQ (generationQuiesced.get_future().wait_for (1s), std::future_status::ready);
    allowCommit.set_value();
    ASSERT_EQ (stopped.wait_for (1s), std::future_status::ready);
    EXPECT_TRUE (stopped.get());
    EXPECT_EQ (probe->order, (std::vector<int> { 1, 2 }));
    EXPECT_EQ (listenCalls.load(), 0);
    EXPECT_EQ (transportStopCalls.load(), 0);
}

TEST (HttpServerLifecycleTests, RestartCreatesFreshGenerationAndRegistersRoutesOnce)
{
    std::vector<std::unique_ptr<BlockingFakeListener>> listeners;
    std::vector<MessageThreadCallGeneration> generations;
    std::vector<MessageThreadCallResult<int>> registrationResults;
    std::vector<int> registrations;

    HttpServerLifecycle lifecycle (
        [&]
        {
            const auto generationIndex = listeners.size();
            auto fake = std::make_unique<BlockingFakeListener>();
            auto* fakePointer = fake.get();
            listeners.push_back (std::move (fake));
            registrations.push_back (0);

            auto listener = fakePointer->makeListener();
            listener->registerRoutes = [&, generationIndex] (MessageThreadCallGeneration& generation)
            {
                ++registrations[generationIndex];
                generations.push_back (generation);
                registrationResults.push_back (runDispatchedCall (
                    [generationIndex] { return static_cast<int> (generationIndex + 1); },
                    [] (std::function<void()> callback)
                    {
                        callback();
                        return true;
                    },
                    100ms,
                    generation));
            };
            return listener;
        });

    lifecycle.start();
    ASSERT_TRUE (listeners[0]->waitUntilRunning (1s));
    ASSERT_EQ (generations.size(), 1u);
    generations[0].quiesce();
    bool oldDispatcherCalled = false;
    const auto explicitlyQuiescedResult = runDispatchedCall (
        [] { return 99; },
        [&] (std::function<void()>)
        {
            oldDispatcherCalled = true;
            return true;
        },
        100ms,
        generations[0]);
    lifecycle.stop();

    lifecycle.start();
    ASSERT_EQ (listeners.size(), 2u);
    ASSERT_TRUE (listeners[1]->waitUntilRunning (1s));
    lifecycle.stop();

    ASSERT_EQ (registrations.size(), 2u);
    ASSERT_EQ (registrationResults.size(), 2u);
    ASSERT_EQ (generations.size(), 2u);
    EXPECT_EQ (registrations[0], 1);
    EXPECT_EQ (registrations[1], 1);
    EXPECT_EQ (registrationResults[0].status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (registrationResults[0].value.has_value());
    EXPECT_EQ (*registrationResults[0].value, 1);
    EXPECT_EQ (registrationResults[1].status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (registrationResults[1].value.has_value());
    EXPECT_EQ (*registrationResults[1].value, 2);
    EXPECT_EQ (explicitlyQuiescedResult.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_FALSE (oldDispatcherCalled);
    EXPECT_EQ (listeners[0]->stopCalls, 1);
    EXPECT_EQ (listeners[1]->stopCalls, 1);
}

TEST (HttpServerLifecycleTests, NaturalFinishRetriesAdmissionCloseBeforeCreatingFreshGeneration)
{
    struct Probe
    {
        MessageThreadCallGeneration::Registration cancellationRegistration;
        std::vector<int> order;
        std::atomic<int> closeAttempts { 0 };
        std::thread waitForStateHelper;
        std::promise<bool> helperFinished;
        std::shared_future<bool> helperFinishedFuture = helperFinished.get_future().share();
        std::atomic<bool> enableLockProbe { false };
        bool transitionMutexAvailableDuringClose = false;
    };

    auto probe = std::make_shared<Probe>();
    ScopeExit joinHelper ([probe]
                          {
                              if (probe->waitForStateHelper.joinable())
                                  probe->waitForStateHelper.join();
                          });
    std::atomic<int> factoryCalls { 0 };
    std::atomic<int> listens { 0 };
    std::promise<void> firstListenReturned;
    std::promise<void> secondListenReturned;
    MessageThreadCallResult<int> freshGenerationResult;
    bool freshGenerationDispatcherCalled = false;
    HttpServerLifecycle* lifecyclePointer = nullptr;

    HttpServerLifecycle lifecycle (
        [&]
        {
            const auto generationNumber = ++factoryCalls;
            if (generationNumber == 2)
                probe->order.push_back (3);

            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->registerRoutes = [&, generationNumber] (MessageThreadCallGeneration& generation)
            {
                if (generationNumber == 1)
                {
                    probe->cancellationRegistration = generation.registerCancellation (
                        [probe] { probe->order.push_back (2); });
                    return;
                }

                freshGenerationResult = runDispatchedCall (
                    [] { return 17; },
                    [&] (std::function<void()> callback)
                    {
                        freshGenerationDispatcherCalled = true;
                        callback();
                        return true;
                    },
                    100ms,
                    generation);
            };
            listener->listen = [&, generationNumber]
            {
                ++listens;
                (generationNumber == 1 ? firstListenReturned : secondListenReturned).set_value();
                return false;
            };
            if (generationNumber == 1)
            {
                listener->closeAdmission = [&, probe]
                {
                    if (++probe->closeAttempts == 1)
                        throw std::runtime_error ("transient admission close failure");

                    probe->order.push_back (1);
                    if (! probe->enableLockProbe.load())
                        return;

                    probe->waitForStateHelper = std::thread (
                        [probe, &lifecyclePointer]
                        {
                            probe->helperFinished.set_value (
                                lifecyclePointer->waitForState (
                                    ListenerStartGate::State::finished,
                                    100ms));
                        });
                    if (probe->helperFinishedFuture.wait_for (250ms) == std::future_status::ready)
                        probe->transitionMutexAvailableDuringClose = probe->helperFinishedFuture.get();
                };
            }
            return listener;
        });
    lifecyclePointer = &lifecycle;

    lifecycle.start();
    ASSERT_EQ (firstListenReturned.get_future().wait_for (1s), std::future_status::ready);
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));

    lifecycle.start();
    EXPECT_EQ (factoryCalls.load(), 1);
    EXPECT_EQ (probe->closeAttempts.load(), 1);
    EXPECT_TRUE (probe->order.empty());

    {
        ScopeExit closeLockProbe ([probe]
                                  {
                                      probe->enableLockProbe = false;
                                      if (probe->waitForStateHelper.joinable())
                                          probe->waitForStateHelper.join();
                                  });
        probe->enableLockProbe = true;
        lifecycle.start();
    }
    ASSERT_EQ (secondListenReturned.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_TRUE (lifecycle.stop());
    EXPECT_TRUE (lifecycle.stop());

    EXPECT_EQ (probe->order, (std::vector<int> { 1, 2, 3 }));
    EXPECT_EQ (probe->closeAttempts.load(), 2);
    EXPECT_TRUE (probe->transitionMutexAvailableDuringClose);
    EXPECT_EQ (factoryCalls.load(), 2);
    EXPECT_EQ (listens.load(), 2);
    EXPECT_TRUE (freshGenerationDispatcherCalled);
    EXPECT_EQ (freshGenerationResult.status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (freshGenerationResult.value.has_value());
    EXPECT_EQ (*freshGenerationResult.value, 17);
}

TEST (HttpServerLifecycleTests, TimedOutStopRetainsItsGenerationUntilTheWorkerExits)
{
    std::promise<void> running;
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    std::atomic<int> generations { 0 };
    std::atomic<bool> isRunning { false };
    std::atomic<int> closeAdmissionCalls { 0 };
    std::atomic<int> generationCancellationCalls { 0 };
    std::atomic<int> transportStopCalls { 0 };
    std::vector<int> workerWaitBudgets;
    MessageThreadCallGeneration retainedGeneration;
    MessageThreadCallGeneration::Registration retainedCancellation;
    auto fakeNow = std::chrono::steady_clock::now() - 1s;

    HttpServerLifecycle lifecycle (
        [&]
        {
            ++generations;
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->registerRoutes = [&] (MessageThreadCallGeneration& generation)
            {
                retainedGeneration = generation;
                retainedCancellation = generation.registerCancellation (
                    [&] { ++generationCancellationCalls; });
            };
            listener->closeAdmission = [&] { ++closeAdmissionCalls; };
            listener->listen = [&]
            {
                isRunning = true;
                running.set_value();
                releaseFuture.wait();
                isRunning = false;
                return true;
            };
            listener->isRunning = [&] { return isRunning.load(); };
            listener->stop = [&] { ++transportStopCalls; };
            listener->onWorkerExitWait = [&] (int remainingMilliseconds)
            {
                workerWaitBudgets.push_back (remainingMilliseconds);
            };
            return listener;
        },
        10ms,
        [&] { return fakeNow; });
    ScopeExit releaseOnFailure ([&]
                                {
                                    try { release.set_value(); } catch (...) {}
                                });

    lifecycle.start();
    ASSERT_EQ (running.get_future().wait_for (1s), std::future_status::ready);

    EXPECT_FALSE (lifecycle.stop());
    EXPECT_EQ (closeAdmissionCalls.load(), 1);
    EXPECT_EQ (generationCancellationCalls.load(), 1);
    EXPECT_EQ (transportStopCalls.load(), 1);
    ASSERT_EQ (workerWaitBudgets.size(), 1u);
    EXPECT_EQ (workerWaitBudgets[0], 10);

    bool retainedDispatcherCalled = false;
    const auto retainedGenerationResult = runDispatchedCall (
        [] { return 99; },
        [&] (std::function<void()>)
        {
            retainedDispatcherCalled = true;
            return true;
        },
        100ms,
        retainedGeneration);
    EXPECT_EQ (retainedGenerationResult.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_FALSE (retainedDispatcherCalled);

    fakeNow += 10ms;
    EXPECT_FALSE (lifecycle.stop());
    lifecycle.start();
    EXPECT_EQ (generations.load(), 1);
    EXPECT_EQ (closeAdmissionCalls.load(), 1);
    EXPECT_EQ (generationCancellationCalls.load(), 1);
    EXPECT_EQ (transportStopCalls.load(), 1);

    release.set_value();
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));

    auto workerReaped = false;
    const auto reapWatchdog = std::chrono::steady_clock::now() + 1s;
    do
    {
        workerReaped = lifecycle.stop();
        if (! workerReaped)
            juce::Thread::yield();
    }
    while (! workerReaped && std::chrono::steady_clock::now() < reapWatchdog);

    ASSERT_TRUE (workerReaped);
    ASSERT_GE (workerWaitBudgets.size(), 2u);
    for (size_t index = 1; index < workerWaitBudgets.size(); ++index)
        EXPECT_EQ (workerWaitBudgets[index], 0);
    EXPECT_EQ (closeAdmissionCalls.load(), 1);
    EXPECT_EQ (generationCancellationCalls.load(), 1);
    EXPECT_EQ (transportStopCalls.load(), 1);
}

TEST (HttpServerLifecycleTests, TransientAdmissionCloseFailureIsRetriedBeforeQuiesceAndTransportStop)
{
    struct Probe
    {
        MessageThreadCallGeneration::Registration cancellationRegistration;
        std::vector<int> order;
        std::atomic<int> closeAttempts { 0 };
        std::atomic<int> transportStopCalls { 0 };
    };

    auto probe = std::make_shared<Probe>();
    std::promise<void> listenerRunning;
    std::promise<void> releaseListener;
    auto releaseListenerFuture = releaseListener.get_future().share();
    std::atomic<bool> running { false };

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->registerRoutes = [probe] (MessageThreadCallGeneration& generation)
            {
                probe->cancellationRegistration = generation.registerCancellation (
                    [probe] { probe->order.push_back (2); });
            };
            listener->closeAdmission = [probe]
            {
                if (++probe->closeAttempts == 1)
                    throw std::runtime_error ("transient admission close failure");
                probe->order.push_back (1);
            };
            listener->listen = [&]
            {
                running = true;
                listenerRunning.set_value();
                releaseListenerFuture.wait();
                running = false;
                return true;
            };
            listener->isRunning = [&] { return running.load(); };
            listener->stop = [&, probe]
            {
                probe->order.push_back (3);
                ++probe->transportStopCalls;
                releaseListener.set_value();
            };
            return listener;
        });
    ScopeExit releaseOnFailure ([&]
                                {
                                    try { releaseListener.set_value(); } catch (...) {}
                                });

    lifecycle.start();
    ASSERT_EQ (listenerRunning.get_future().wait_for (1s), std::future_status::ready);

    EXPECT_FALSE (lifecycle.stop());
    EXPECT_EQ (probe->closeAttempts.load(), 1);
    EXPECT_EQ (probe->transportStopCalls.load(), 0);
    EXPECT_TRUE (probe->order.empty());

    EXPECT_TRUE (lifecycle.stop());
    EXPECT_EQ (probe->closeAttempts.load(), 2);
    EXPECT_EQ (probe->transportStopCalls.load(), 1);
    EXPECT_EQ (probe->order, (std::vector<int> { 1, 2, 3 }));
    EXPECT_TRUE (lifecycle.stop());
}

TEST (HttpServerLifecycleTests, FactoryAndRegistrarFailuresDoNotWedgeLaterStarts)
{
    struct RegistrarFailureProbe
    {
        MessageThreadCallGeneration generation;
        MessageThreadCallGeneration::Registration retirementRegistration;
        std::promise<void> queued;
        std::shared_future<void> queuedFuture = queued.get_future().share();
        std::promise<void> finished;
        std::shared_future<void> finishedFuture = finished.get_future().share();
        std::function<void()> lateCallback;
        std::atomic<bool> retirementObserved { false };
        std::atomic<int> operationCount { 0 };
        MessageThreadCallResult<int> result;
    };

    std::atomic<int> factoryCalls { 0 };
    std::atomic<int> registrarCalls { 0 };
    auto failedRegistrar = std::make_shared<RegistrarFailureProbe>();
    std::thread pendingCaller;
    MessageThreadCallResult<int> freshGenerationResult;
    bool freshGenerationDispatcherCalled = false;
    std::promise<void> listened;
    ScopeExit joinPendingCaller ([&]
                                 {
                                     if (pendingCaller.joinable())
                                     {
                                         failedRegistrar->generation.quiesce();
                                         pendingCaller.join();
                                     }
                                 });

    HttpServerLifecycle lifecycle (
        [&]
        {
            const auto call = ++factoryCalls;
            if (call == 1)
                throw std::runtime_error ("factory failed");

            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->registerRoutes = [&, call] (MessageThreadCallGeneration& generation)
            {
                ++registrarCalls;
                if (call == 2)
                {
                    failedRegistrar->generation = generation;
                    failedRegistrar->retirementRegistration = generation.registerCancellation (
                        [failedRegistrar] { failedRegistrar->retirementObserved = true; });
                    pendingCaller = std::thread (
                        [failedRegistrar]
                        {
                            failedRegistrar->result = runDispatchedCall (
                                [failedRegistrar]
                                {
                                    ++failedRegistrar->operationCount;
                                    return 42;
                                },
                                [failedRegistrar] (std::function<void()> callback)
                                {
                                    failedRegistrar->lateCallback = std::move (callback);
                                    failedRegistrar->queued.set_value();
                                    return true;
                                },
                                5s,
                                failedRegistrar->generation);
                            failedRegistrar->finished.set_value();
                        });

                    if (failedRegistrar->queuedFuture.wait_for (1s) != std::future_status::ready)
                    {
                        failedRegistrar->generation.quiesce();
                        pendingCaller.join();
                        throw std::runtime_error ("registrar queue handshake failed");
                    }

                    throw std::runtime_error ("registrar failed");
                }

                freshGenerationResult = runDispatchedCall (
                    [] { return 7; },
                    [&] (std::function<void()> callback)
                    {
                        freshGenerationDispatcherCalled = true;
                        callback();
                        return true;
                    },
                    100ms,
                    generation);
            };
            listener->listen = [&]
            {
                listened.set_value();
                return false;
            };
            return listener;
        });

    EXPECT_THROW (lifecycle.start(), std::runtime_error);
    try
    {
        lifecycle.start();
        FAIL() << "Expected registrar failure";
    }
    catch (const std::runtime_error& exception)
    {
        EXPECT_STREQ (exception.what(), "registrar failed");
    }

    EXPECT_TRUE (failedRegistrar->retirementObserved.load());
    const auto retiredBeforeTestCleanup =
        failedRegistrar->finishedFuture.wait_for (1s) == std::future_status::ready;
    if (! retiredBeforeTestCleanup)
        failedRegistrar->generation.quiesce();
    ASSERT_EQ (failedRegistrar->finishedFuture.wait_for (1s), std::future_status::ready);
    pendingCaller.join();

    EXPECT_TRUE (retiredBeforeTestCleanup);
    EXPECT_EQ (failedRegistrar->result.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (failedRegistrar->operationCount.load(), 0);
    ASSERT_TRUE (failedRegistrar->lateCallback);
    failedRegistrar->lateCallback();
    EXPECT_EQ (failedRegistrar->operationCount.load(), 0);

    lifecycle.start();
    EXPECT_EQ (listened.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_TRUE (lifecycle.stop());
    EXPECT_TRUE (freshGenerationDispatcherCalled);
    EXPECT_EQ (freshGenerationResult.status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (freshGenerationResult.value.has_value());
    EXPECT_EQ (*freshGenerationResult.value, 7);
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
            listener->registerRoutes = [&, generationIndex] (MessageThreadCallGeneration&)
            {
                ++registrations[generationIndex];
            };
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

TEST (HttpServerLifecycleTests, RepeatedStopCannotExtendOneListenerGenerationDeadline)
{
    std::promise<void> listenerRunning;
    std::promise<void> releaseListener;
    auto releaseListenerFuture = releaseListener.get_future().share();
    std::vector<int> workerWaitBudgets;
    auto fakeNow = std::chrono::steady_clock::now() - 1s;

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [&]
            {
                listenerRunning.set_value();
                releaseListenerFuture.wait();
                return true;
            };
            listener->isRunning = [] { return true; };
            listener->stop = [] {};
            listener->onWorkerExitWait = [&] (int remainingMilliseconds)
            {
                workerWaitBudgets.push_back (remainingMilliseconds);
            };
            return listener;
        },
        10ms,
        [&] { return fakeNow; });
    ScopeExit releaseOnFailure ([&]
                                {
                                    try { releaseListener.set_value(); } catch (...) {}
                                });

    lifecycle.start();
    ASSERT_EQ (listenerRunning.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_FALSE (lifecycle.stop());
    ASSERT_EQ (workerWaitBudgets.size(), 1u);
    EXPECT_EQ (workerWaitBudgets[0], 10);

    fakeNow += 10ms;
    releaseListener.set_value();
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));

    auto workerReaped = false;
    const auto reapWatchdog = std::chrono::steady_clock::now() + 1s;
    do
    {
        workerReaped = lifecycle.stop();
        if (! workerReaped)
            juce::Thread::yield();
    }
    while (! workerReaped && std::chrono::steady_clock::now() < reapWatchdog);

    ASSERT_TRUE (workerReaped);
    ASSERT_GE (workerWaitBudgets.size(), 2u);
    for (size_t index = 1; index < workerWaitBudgets.size(); ++index)
        EXPECT_EQ (workerWaitBudgets[index], 0);
}

TEST (HttpServerLifecycleTests, FreshListenerGenerationGetsFreshDeadlineOnlyAfterPriorWorkerRetires)
{
    struct Generation
    {
        std::promise<void> running;
        std::promise<void> release;
        std::shared_future<void> releaseFuture = release.get_future().share();
        std::vector<int> workerWaitBudgets;
    };

    std::vector<std::unique_ptr<Generation>> generations;
    auto fakeNow = std::chrono::steady_clock::now() - 1s;

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto generation = std::make_unique<Generation>();
            auto* generationPointer = generation.get();
            generations.push_back (std::move (generation));

            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [generationPointer]
            {
                generationPointer->running.set_value();
                generationPointer->releaseFuture.wait();
                return true;
            };
            listener->isRunning = [] { return true; };
            listener->stop = [] {};
            listener->onWorkerExitWait = [generationPointer] (int remainingMilliseconds)
            {
                generationPointer->workerWaitBudgets.push_back (remainingMilliseconds);
            };
            return listener;
        },
        10ms,
        [&] { return fakeNow; });
    ScopeExit releaseOnFailure ([&]
                                {
                                    for (auto& generation : generations)
                                        try { generation->release.set_value(); } catch (...) {}
                                });

    lifecycle.start();
    ASSERT_EQ (generations.size(), 1u);
    ASSERT_EQ (generations[0]->running.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_FALSE (lifecycle.stop());
    ASSERT_EQ (generations[0]->workerWaitBudgets.size(), 1u);
    EXPECT_EQ (generations[0]->workerWaitBudgets[0], 10);

    lifecycle.start();
    EXPECT_EQ (generations.size(), 1u);

    fakeNow += 10ms;
    generations[0]->release.set_value();
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));

    auto firstWorkerReaped = false;
    const auto firstReapWatchdog = std::chrono::steady_clock::now() + 1s;
    do
    {
        firstWorkerReaped = lifecycle.stop();
        if (! firstWorkerReaped)
            juce::Thread::yield();
    }
    while (! firstWorkerReaped && std::chrono::steady_clock::now() < firstReapWatchdog);

    ASSERT_TRUE (firstWorkerReaped);
    ASSERT_GE (generations[0]->workerWaitBudgets.size(), 2u);
    for (size_t index = 1; index < generations[0]->workerWaitBudgets.size(); ++index)
        EXPECT_EQ (generations[0]->workerWaitBudgets[index], 0);

    lifecycle.start();

    ASSERT_EQ (generations.size(), 2u);
    ASSERT_EQ (generations[1]->running.get_future().wait_for (1s), std::future_status::ready);
    EXPECT_FALSE (lifecycle.stop());
    ASSERT_EQ (generations[1]->workerWaitBudgets.size(), 1u);
    EXPECT_EQ (generations[1]->workerWaitBudgets[0], 10);

    fakeNow += 10ms;
    generations[1]->release.set_value();
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));

    auto secondWorkerReaped = false;
    const auto secondReapWatchdog = std::chrono::steady_clock::now() + 1s;
    do
    {
        secondWorkerReaped = lifecycle.stop();
        if (! secondWorkerReaped)
            juce::Thread::yield();
    }
    while (! secondWorkerReaped && std::chrono::steady_clock::now() < secondReapWatchdog);

    ASSERT_TRUE (secondWorkerReaped);
    ASSERT_GE (generations[1]->workerWaitBudgets.size(), 2u);
    for (size_t index = 1; index < generations[1]->workerWaitBudgets.size(); ++index)
        EXPECT_EQ (generations[1]->workerWaitBudgets[index], 0);
}

TEST (HttpServerLifecycleTests, SaturatedDeadlineClampsWorkerWaitBudgetToIntMaximum)
{
    std::promise<int> workerWaitBudget;

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [] { return false; };
            listener->onWorkerExitWait = [&] (int remainingMilliseconds)
            {
                workerWaitBudget.set_value (remainingMilliseconds);
            };
            return listener;
        },
        std::chrono::milliseconds::max(),
        [] { return std::chrono::steady_clock::time_point::min(); });

    lifecycle.start();
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));
    EXPECT_TRUE (lifecycle.stop());
    EXPECT_EQ (workerWaitBudget.get_future().get(), std::numeric_limits<int>::max());
}

TEST (HttpServerLifecycleTests, NearMaximumClockPreservesExactWorkerWaitBudget)
{
    std::promise<int> workerWaitBudget;
    const auto fakeNow = std::chrono::steady_clock::time_point::max() - 20ms;

    HttpServerLifecycle lifecycle (
        [&]
        {
            auto listener = std::make_shared<HttpServerLifecycle::Listener>();
            listener->listen = [] { return false; };
            listener->onWorkerExitWait = [&] (int remainingMilliseconds)
            {
                workerWaitBudget.set_value (remainingMilliseconds);
            };
            return listener;
        },
        10ms,
        [&] { return fakeNow; });

    lifecycle.start();
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::finished, 1s));
    EXPECT_TRUE (lifecycle.stop());
    EXPECT_EQ (workerWaitBudget.get_future().get(), 10);
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
