#include "../../Source/Processors/RecordNode/RecordThreadCleanStop.h"

#include "gtest/gtest.h"

#include <chrono>
#include <limits>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
using Outcome = RecordThreadCleanStopOutcome;

struct FakeClock
{
    std::vector<Clock::time_point> readings;
    std::size_t nextReading = 0;

    Clock::time_point now()
    {
        EXPECT_LT (nextReading, readings.size());
        if (nextReading >= readings.size())
            return {};

        return readings[nextReading++];
    }
};

struct FakeOwner
{
    bool running = false;
    bool exitRequested = false;
    bool clean = false;
    bool waitResult = false;
    bool runningAfterWait = false;
    std::vector<bool> runningReadings;
    std::size_t nextRunningReading = 0;
    FakeClock* clock = nullptr;

    int runningCount = 0;
    int exitRequestedCount = 0;
    int signalCount = 0;
    int notifyCount = 0;
    int cleanCount = 0;
    int nowCount = 0;
    int waitCount = 0;
    int waitedMilliseconds = -1;
    std::vector<std::string> trace;

    bool isThreadRunning()
    {
        ++runningCount;
        trace.push_back ("running");
        if (nextRunningReading
            < runningReadings.size())
        {
            return runningReadings[
                nextRunningReading++];
        }
        return running;
    }

    bool exitAlreadyRequested()
    {
        ++exitRequestedCount;
        trace.push_back ("exit_requested");
        return exitRequested;
    }

    void signalThreadShouldExit()
    {
        ++signalCount;
        trace.push_back ("signal");
        exitRequested = true;
    }

    void notify()
    {
        ++notifyCount;
        trace.push_back ("notify");
    }

    bool exitedCleanly()
    {
        ++cleanCount;
        trace.push_back ("clean");
        return clean;
    }

    Clock::time_point now()
    {
        ++nowCount;
        trace.push_back ("now");
        return clock->now();
    }

    bool waitForThreadToExit (
        int milliseconds)
    {
        ++waitCount;
        waitedMilliseconds = milliseconds;
        trace.push_back ("wait");
        running = runningAfterWait;
        return waitResult;
    }
};

Clock::time_point atMilliseconds (
    long long milliseconds)
{
    return Clock::time_point (
        Milliseconds (milliseconds));
}

} // namespace

TEST (RecordThreadCleanStopTests,
      RequestSignalsAndNotifiesRunningThreadOnce)
{
    auto owner = FakeOwner {};
    owner.running = true;

    requestRecordThreadCleanStop (owner);
    requestRecordThreadCleanStop (owner);

    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "exit_requested",
            "signal",
            "notify",
            "running",
            "exit_requested"
        }));
    EXPECT_EQ (owner.runningCount, 2);
    EXPECT_EQ (owner.exitRequestedCount, 2);
    EXPECT_EQ (owner.signalCount, 1);
    EXPECT_EQ (owner.notifyCount, 1);
    EXPECT_EQ (owner.cleanCount, 0);
    EXPECT_EQ (owner.nowCount, 0);
    EXPECT_EQ (owner.waitCount, 0);
}

TEST (RecordThreadCleanStopTests,
      RequestIsIdempotentWhenExitWasAlreadyRequested)
{
    auto owner = FakeOwner {};
    owner.running = true;
    owner.exitRequested = true;

    requestRecordThreadCleanStop (owner);

    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "exit_requested"
        }));
    EXPECT_EQ (owner.signalCount, 0);
    EXPECT_EQ (owner.notifyCount, 0);
    EXPECT_EQ (owner.cleanCount, 0);
    EXPECT_EQ (owner.nowCount, 0);
    EXPECT_EQ (owner.waitCount, 0);
}

TEST (RecordThreadCleanStopTests,
      RequestIsNoOpWhenThreadIsAlreadyStopped)
{
    auto owner = FakeOwner {};
    owner.running = false;

    requestRecordThreadCleanStop (owner);

    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running"
        }));
    EXPECT_EQ (owner.exitRequestedCount, 0);
    EXPECT_EQ (owner.signalCount, 0);
    EXPECT_EQ (owner.notifyCount, 0);
    EXPECT_EQ (owner.cleanCount, 0);
    EXPECT_EQ (owner.nowCount, 0);
    EXPECT_EQ (owner.waitCount, 0);
}

TEST (RecordThreadCleanStopTests,
      WaitReturnsAlreadyStoppedCleanWithoutReadingClock)
{
    auto owner = FakeOwner {};
    owner.running = false;
    owner.clean = true;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (500));

    EXPECT_EQ (
        outcome,
        Outcome::alreadyStoppedClean);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "clean"
        }));
    EXPECT_EQ (owner.nowCount, 0);
    EXPECT_EQ (owner.waitCount, 0);
}

TEST (RecordThreadCleanStopTests,
      WaitReturnsAlreadyStoppedUncleanWithoutReadingClock)
{
    auto owner = FakeOwner {};
    owner.running = false;
    owner.clean = false;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (500));

    EXPECT_EQ (
        outcome,
        Outcome::alreadyStoppedUnclean);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "clean"
        }));
    EXPECT_EQ (owner.nowCount, 0);
    EXPECT_EQ (owner.waitCount, 0);
}

TEST (RecordThreadCleanStopTests,
      ExpiredDeadlineNeverWaitsAndTimesOutIfStillRunning)
{
    auto clock = FakeClock {
        { atMilliseconds (500) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.runningReadings = { true, true };
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (500));

    EXPECT_EQ (outcome, Outcome::timedOut);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "running"
        }));
    EXPECT_EQ (owner.waitCount, 0);
    EXPECT_EQ (owner.cleanCount, 0);
}

TEST (RecordThreadCleanStopTests,
      ExpiredDeadlineRechecksAndReportsCleanExit)
{
    auto clock = FakeClock {
        { atMilliseconds (501) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.runningReadings = { true, false };
    owner.clean = true;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (500));

    EXPECT_EQ (outcome, Outcome::cleanExit);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "running",
            "clean"
        }));
    EXPECT_EQ (owner.waitCount, 0);
}

TEST (RecordThreadCleanStopTests,
      PositiveSubMillisecondRemainingClampsToOne)
{
    auto clock = FakeClock {
        { Clock::time_point {} }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.runningAfterWait = false;
    owner.waitResult = true;
    owner.clean = true;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            Clock::time_point {}
                + std::chrono::microseconds (1));

    EXPECT_EQ (outcome, Outcome::cleanExit);
    EXPECT_EQ (owner.waitCount, 1);
    EXPECT_EQ (owner.waitedMilliseconds, 1);
}

TEST (RecordThreadCleanStopTests,
      FractionalMillisecondRemainingRoundsUp)
{
    auto clock = FakeClock {
        { Clock::time_point {} }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.runningAfterWait = false;
    owner.waitResult = true;
    owner.clean = true;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            Clock::time_point {}
                + std::chrono::microseconds (1001));

    EXPECT_EQ (outcome, Outcome::cleanExit);
    EXPECT_EQ (owner.waitCount, 1);
    EXPECT_EQ (owner.waitedMilliseconds, 2);
}

TEST (RecordThreadCleanStopTests,
      OversizedRemainingClampsToIntMaximum)
{
    auto clock = FakeClock {
        { Clock::time_point {} }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.runningAfterWait = true;
    owner.waitResult = false;
    owner.clock = &clock;
    const auto oversized =
        static_cast<long long> (
            std::numeric_limits<int>::max())
        + 47LL;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (oversized));

    EXPECT_EQ (outcome, Outcome::timedOut);
    EXPECT_EQ (owner.waitCount, 1);
    EXPECT_EQ (
        owner.waitedMilliseconds,
        std::numeric_limits<int>::max());
}

TEST (RecordThreadCleanStopTests,
      RunningThreadWaitsEvenIfCleanFlagIsAlreadyTrue)
{
    auto clock = FakeClock {
        { atMilliseconds (100) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.clean = true;
    owner.waitResult = true;
    owner.runningAfterWait = false;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (150));

    EXPECT_EQ (outcome, Outcome::cleanExit);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "wait",
            "running",
            "clean"
        }));
    EXPECT_EQ (owner.waitedMilliseconds, 50);
}

TEST (RecordThreadCleanStopTests,
      WaitTimeoutRechecksRunningBeforeReturningTimeout)
{
    auto clock = FakeClock {
        { atMilliseconds (100) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.waitResult = false;
    owner.runningAfterWait = true;
    owner.clean = true;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (125));

    EXPECT_EQ (outcome, Outcome::timedOut);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "wait",
            "running"
        }));
    EXPECT_EQ (owner.cleanCount, 0);
}

TEST (RecordThreadCleanStopTests,
      FalseWaitResultButStoppedReportsUncleanExit)
{
    auto clock = FakeClock {
        { atMilliseconds (100) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.waitResult = false;
    owner.runningAfterWait = false;
    owner.clean = false;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (125));

    EXPECT_EQ (outcome, Outcome::uncleanExit);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "wait",
            "running",
            "clean"
        }));
}

TEST (RecordThreadCleanStopTests,
      FalseWaitResultButStoppedReportsCleanExit)
{
    auto clock = FakeClock {
        { atMilliseconds (100) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.waitResult = false;
    owner.runningAfterWait = false;
    owner.clean = true;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (125));

    EXPECT_EQ (outcome, Outcome::cleanExit);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "wait",
            "running",
            "clean"
        }));
}

TEST (RecordThreadCleanStopTests,
      SuccessfulWaitRechecksRunningAndReportsCleanExit)
{
    auto clock = FakeClock {
        { atMilliseconds (100) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.waitResult = true;
    owner.runningAfterWait = false;
    owner.clean = true;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (125));

    EXPECT_EQ (outcome, Outcome::cleanExit);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "wait",
            "running",
            "clean"
        }));
}

TEST (RecordThreadCleanStopTests,
      SuccessfulWaitButStoppedUncleanReportsUncleanExit)
{
    auto clock = FakeClock {
        { atMilliseconds (100) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.waitResult = true;
    owner.runningAfterWait = false;
    owner.clean = false;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (125));

    EXPECT_EQ (outcome, Outcome::uncleanExit);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "wait",
            "running",
            "clean"
        }));
}

TEST (RecordThreadCleanStopTests,
      SuccessfulWaitButStillRunningReportsTimeout)
{
    auto clock = FakeClock {
        { atMilliseconds (100) }
    };
    auto owner = FakeOwner {};
    owner.running = true;
    owner.waitResult = true;
    owner.runningAfterWait = true;
    owner.clean = true;
    owner.clock = &clock;

    const auto outcome =
        waitForRecordThreadCleanStopUntil (
            owner,
            atMilliseconds (125));

    EXPECT_EQ (outcome, Outcome::timedOut);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "running",
            "now",
            "wait",
            "running"
        }));
    EXPECT_EQ (owner.cleanCount, 0);
}

TEST (RecordThreadCleanStopTests,
      SharedAbsoluteDeadlineReducesSecondNodeBudget)
{
    auto clock = FakeClock {
        {
            atMilliseconds (100),
            atMilliseconds (140)
        }
    };
    auto first = FakeOwner {};
    first.running = true;
    first.waitResult = true;
    first.runningAfterWait = false;
    first.clean = true;
    first.clock = &clock;
    auto second = FakeOwner {};
    second.running = true;
    second.waitResult = true;
    second.runningAfterWait = false;
    second.clean = true;
    second.clock = &clock;
    const auto sharedDeadline =
        atMilliseconds (200);

    EXPECT_EQ (
        waitForRecordThreadCleanStopUntil (
            first,
            sharedDeadline),
        Outcome::cleanExit);
    EXPECT_EQ (
        waitForRecordThreadCleanStopUntil (
            second,
            sharedDeadline),
        Outcome::cleanExit);

    EXPECT_EQ (first.waitedMilliseconds, 100);
    EXPECT_EQ (second.waitedMilliseconds, 60);
    EXPECT_EQ (clock.nextReading, 2u);
}
