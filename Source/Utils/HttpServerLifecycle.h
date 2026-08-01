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
    };

    using ListenerPtr = std::shared_ptr<Listener>;
    using Factory = std::function<ListenerPtr()>;

    explicit HttpServerLifecycle (Factory factory,
                                  std::chrono::milliseconds shutdownTimeout = std::chrono::seconds (5))
        : factory_ (std::move (factory)),
          shutdownTimeout_ (shutdownTimeout)
    {
    }

    ~HttpServerLifecycle()
    {
        stop();
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

        auto completedCycle = std::move (cycle_);
        transitionLock.unlock();

        if (completedCycle != nullptr)
            completedCycle->worker->stopThread (static_cast<int> (shutdownTimeout_.count()));

        transitionLock.lock();

        auto listener = factory_();
        listener->registerRoutes();

        cycle_ = std::make_shared<Cycle>();
        cycle_->listener = std::move (listener);
        cycle_->gate = std::make_shared<ListenerStartGate>();

        const auto listenerForWorker = cycle_->listener;
        const auto gateForWorker = cycle_->gate;
        cycle_->worker = std::make_unique<Worker> (listenerForWorker, gateForWorker);
        if (! cycle_->worker->startThread())
            gateForWorker->markFinished();

        transitionInProgress_ = false;
        transitionCondition_.notify_all();
    }

    void stop()
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
            transitionInProgress_ = false;
            transitionCondition_.notify_all();
            return;
        }

        const auto deadline = std::chrono::steady_clock::now() + shutdownTimeout_;
        if (! cycle->gate->tryCancel())
        {
            // cpp-httplib ignores stop() until listen() has published its
            // running state. Polling is intentional: the minimal listener seam
            // exposes no running-state notification, and no lifecycle lock is
            // held while waiting, stopping, or joining.
            while (std::chrono::steady_clock::now() < deadline
                   && cycle->gate->state() != ListenerStartGate::State::finished)
            {
                if (cycle->listener->isRunning())
                {
                    if (! cycle->listenerStopIssued.exchange (true))
                        cycle->listener->stop();
                    break;
                }

                juce::Thread::yield();
            }
        }

        cycle->gate->waitUntilFinished (deadline);
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - std::chrono::steady_clock::now()).count();
        cycle->worker->stopThread (remaining > 0 ? static_cast<int> (remaining) : 0);

        std::lock_guard<std::mutex> transitionLock (transitionMutex_);
        if (cycle_ == cycle)
            cycle_.reset();
        transitionInProgress_ = false;
        transitionCondition_.notify_all();
    }

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
    mutable std::mutex transitionMutex_;
    std::condition_variable transitionCondition_;
    bool transitionInProgress_ = false;
    std::shared_ptr<Cycle> cycle_;
};
