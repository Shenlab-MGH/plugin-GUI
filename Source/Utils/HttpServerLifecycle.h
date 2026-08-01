/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#pragma once

#include "../../JuceLibraryCode/JuceHeader.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

class ListenerStartGate
{
public:
    enum class State
    {
        pending,
        committed,
        cancelled,
        finished
    };

    bool tryCommit()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        if (state_ != State::pending)
            return false;

        state_ = State::committed;
        condition_.notify_all();
        return true;
    }

    bool tryCancel()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        if (state_ != State::pending)
            return false;

        state_ = State::cancelled;
        condition_.notify_all();
        return true;
    }

    void markFinished()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        state_ = State::finished;
        condition_.notify_all();
    }

    State state() const
    {
        std::lock_guard<std::mutex> lock (mutex_);
        return state_;
    }

    template <typename Rep, typename Period>
    bool waitForState (State expected, const std::chrono::duration<Rep, Period>& timeout) const
    {
        std::unique_lock<std::mutex> lock (mutex_);
        return condition_.wait_for (lock, timeout, [this, expected] { return state_ == expected; });
    }

    bool waitUntilFinished (std::chrono::steady_clock::time_point deadline) const
    {
        std::unique_lock<std::mutex> lock (mutex_);
        return condition_.wait_until (lock, deadline, [this] { return state_ == State::finished; });
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    State state_ = State::pending;
};

class HttpServerLifecycle
{
public:
    struct Listener
    {
        std::function<void()> beforeCommit = [] {};
        std::function<bool()> listen = [] { return false; };
        std::function<bool()> isRunning = [] { return false; };
        std::function<void()> stop = [] {};
        std::function<void()> registerRoutes = [] {};
        std::function<void(int)> onWorkerExitWait = [] (int) {};
    };

    using ListenerPtr = std::shared_ptr<Listener>;
    using Factory = std::function<ListenerPtr()>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit HttpServerLifecycle (Factory factory,
                                  std::chrono::milliseconds shutdownTimeout = std::chrono::seconds (5),
                                  Clock clock = [] { return std::chrono::steady_clock::now(); })
        : factory_ (std::move (factory)),
          shutdownTimeout_ (shutdownTimeout),
          clock_ (std::move (clock))
    {
    }

    ~HttpServerLifecycle()
    {
        stopUntilWorkerExits();
    }

    void start()
    {
        std::unique_lock<std::mutex> transitionLock (transitionMutex_);

        transitionCondition_.wait (transitionLock, [this] { return ! transitionInProgress_; });
        transitionInProgress_ = true;

        if (cycle_ != nullptr && cycle_->gate->state() != ListenerStartGate::State::finished)
        {
            transitionInProgress_ = false;
            transitionCondition_.notify_all();
            return;
        }

        try
        {
            auto completedCycle = std::move (cycle_);
            transitionLock.unlock();

            const auto completed = completedCycle == nullptr
                || completedCycle->worker->waitForThreadToExit (static_cast<int> (shutdownTimeout_.count()));

            transitionLock.lock();
            if (! completed)
            {
                cycle_ = std::move (completedCycle);
                finishTransition();
                return;
            }

            auto listener = factory_();
            listener->registerRoutes();

            auto newCycle = std::make_shared<Cycle>();
            newCycle->listener = std::move (listener);
            newCycle->gate = std::make_shared<ListenerStartGate>();
            newCycle->worker = std::make_unique<Worker> (newCycle->listener, newCycle->gate);
            if (! newCycle->worker->startThread())
                newCycle->gate->markFinished();

            cycle_ = std::move (newCycle);
            finishTransition();
        }
        catch (...)
        {
            if (! transitionLock.owns_lock())
                transitionLock.lock();
            finishTransition();
            throw;
        }
    }

    bool stop()
    {
        return stopImpl (true);
    }

private:
    bool stopImpl (bool bounded)
    {
        std::shared_ptr<Cycle> cycle;
        {
            std::unique_lock<std::mutex> transitionLock (transitionMutex_);
            transitionCondition_.wait (transitionLock, [this] { return ! transitionInProgress_; });
            transitionInProgress_ = true;
            cycle = cycle_;
        }

        if (cycle == nullptr)
        {
            std::lock_guard<std::mutex> transitionLock (transitionMutex_);
            finishTransition();
            return true;
        }

        const auto deadline = now() + shutdownTimeout_;
        if (! cycle->gate->tryCancel())
        {
            // cpp-httplib ignores stop() until listen() has published its
            // running state. Polling is intentional: the minimal listener seam
            // exposes no running-state notification, and no lifecycle lock is
            // held while waiting, stopping, or joining.
            while ((! bounded || now() < deadline)
                   && cycle->gate->state() != ListenerStartGate::State::finished)
            {
                bool running = false;
                try
                {
                    running = cycle->listener->isRunning();
                }
                catch (...)
                {
                }

                if (running)
                {
                    if (! cycle->listenerStopIssued.exchange (true))
                    {
                        try
                        {
                            cycle->listener->stop();
                        }
                        catch (...)
                        {
                            cycle->listenerStopIssued = false;
                        }
                    }
                    break;
                }

                juce::Thread::yield();
            }
        }

        const auto finished = cycle->gate->waitUntilFinished (
            bounded ? deadline : std::chrono::steady_clock::time_point::max());
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - now()).count();
        const auto workerWaitMilliseconds = bounded ? (remaining > 0 ? static_cast<int> (remaining) : 0) : -1;
        try
        {
            cycle->listener->onWorkerExitWait (workerWaitMilliseconds);
        }
        catch (...)
        {
        }
        const auto workerExited = finished && cycle->worker->waitForThreadToExit (
            workerWaitMilliseconds);

        std::lock_guard<std::mutex> transitionLock (transitionMutex_);
        if (workerExited && cycle_ == cycle)
            cycle_.reset();
        finishTransition();
        return workerExited;
    }

public:
    bool waitForState (ListenerStartGate::State expected,
                       std::chrono::milliseconds timeout) const
    {
        std::shared_ptr<Cycle> cycle;
        {
            std::lock_guard<std::mutex> transitionLock (transitionMutex_);
            cycle = cycle_;
        }

        return cycle != nullptr && cycle->gate->waitForState (expected, timeout);
    }

private:
    void finishTransition()
    {
        transitionInProgress_ = false;
        transitionCondition_.notify_all();
    }

    std::chrono::steady_clock::time_point now() const noexcept
    {
        try
        {
            return clock_();
        }
        catch (...)
        {
            return std::chrono::steady_clock::now();
        }
    }

    void stopUntilWorkerExits() noexcept
    {
        // Public stop is bounded, but destruction must retain the Cycle until
        // its worker is gone. Legacy request handlers may therefore extend
        // final teardown; making those handlers cancellable is separate work.
        for (;;)
        {
            try
            {
                if (stopImpl (false))
                    return;
            }
            catch (...)
            {
                // Retrying is safer than destroying a live listener/server.
            }
        }
    }
    class Worker final : public juce::Thread
    {
    public:
        Worker (ListenerPtr listener, std::shared_ptr<ListenerStartGate> gate)
            : juce::Thread ("HttpServerLifecycle"),
              listener_ (std::move (listener)),
              gate_ (std::move (gate))
        {
        }

        void run() override
        {
            try
            {
                listener_->beforeCommit();

                // The gate supplies the happens-before edge between stop() and
                // listen(): a cancellation that wins while pending makes this
                // commit fail. A separate pre-listen cancellation check would
                // leave a race between that check and the blocking listen call.
                if (gate_->tryCommit())
                    listener_->listen();
            }
            catch (...)
            {
            }

            gate_->markFinished();
        }

    private:
        ListenerPtr listener_;
        std::shared_ptr<ListenerStartGate> gate_;
    };

    struct Cycle
    {
        ListenerPtr listener;
        std::shared_ptr<ListenerStartGate> gate;
        std::unique_ptr<Worker> worker;
        std::atomic<bool> listenerStopIssued { false };
    };

    Factory factory_;
    const std::chrono::milliseconds shutdownTimeout_;
    Clock clock_;
    mutable std::mutex transitionMutex_;
    std::condition_variable transitionCondition_;
    bool transitionInProgress_ = false;
    std::shared_ptr<Cycle> cycle_;
};
