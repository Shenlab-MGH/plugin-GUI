/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#ifndef RECORD_THREAD_CLEAN_STOP_H
#define RECORD_THREAD_CLEAN_STOP_H

#include <chrono>
#include <limits>

enum class RecordThreadCleanStopOutcome
{
    alreadyStoppedClean,
    alreadyStoppedUnclean,
    cleanExit,
    uncleanExit,
    timedOut
};

template <typename Owner>
void requestRecordThreadCleanStop (Owner& owner)
{
    if (! owner.isThreadRunning())
        return;

    if (owner.exitAlreadyRequested())
        return;

    owner.signalThreadShouldExit();
    owner.notify();
}

template <typename Owner>
RecordThreadCleanStopOutcome
waitForRecordThreadCleanStopUntil (
    Owner& owner,
    std::chrono::steady_clock::time_point deadline)
{
    if (! owner.isThreadRunning())
    {
        return owner.exitedCleanly()
                   ? RecordThreadCleanStopOutcome::
                         alreadyStoppedClean
                   : RecordThreadCleanStopOutcome::
                         alreadyStoppedUnclean;
    }

    const auto remaining = deadline - owner.now();
    if (remaining
        > std::chrono::steady_clock::duration::zero())
    {
        const auto roundedMilliseconds =
            std::chrono::ceil<
                std::chrono::milliseconds> (
                remaining)
                .count();
        const auto maximumMilliseconds =
            static_cast<decltype (
                roundedMilliseconds)> (
                std::numeric_limits<int>::max());
        const auto waitMilliseconds =
            roundedMilliseconds
                    > maximumMilliseconds
                ? std::numeric_limits<int>::max()
                : static_cast<int> (
                    roundedMilliseconds);
        owner.waitForThreadToExit (
            waitMilliseconds);
    }

    if (owner.isThreadRunning())
    {
        return RecordThreadCleanStopOutcome::
            timedOut;
    }

    return owner.exitedCleanly()
               ? RecordThreadCleanStopOutcome::
                     cleanExit
               : RecordThreadCleanStopOutcome::
                     uncleanExit;
}

#endif
