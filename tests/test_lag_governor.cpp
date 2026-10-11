#include "../src/core/lag_governor.h"
#include "test_support.h"

using namespace audiorgui;

TEST(Lag_AStreamStartsLive)
{
    LagGovernor governor;
    CHECK(governor.Observe(0.0f) == TranscriptState::Live);
    CHECK(governor.Observe(0.5f) == TranscriptState::Live);
    CHECK(governor.Observe(LagGovernor::kCatchUpLag - 0.01f) == TranscriptState::Live);
}

TEST(Lag_FallingBehindAndRecoveringHaveAGapBetweenThem)
{
    LagGovernor governor;
    CHECK(governor.Observe(LagGovernor::kCatchUpLag) == TranscriptState::CatchingUp);
    CHECK(governor.Observe(2.0f) == TranscriptState::CatchingUp);               /* better, but not recovered */
    CHECK(governor.Observe(LagGovernor::kRecoveredLag) == TranscriptState::CatchingUp);
    CHECK(governor.Observe(LagGovernor::kRecoveredLag - 0.01f) == TranscriptState::Live);
    CHECK(governor.Observe(2.0f) == TranscriptState::Live);                     /* and not behind again until 3 s */
    CHECK(governor.Observe(9.0f) == TranscriptState::CatchingUp);
}

TEST(StepDown_NeedsTheLagToStayHopelessForTheWholeHoldTime)
{
    StepDownPolicy policy(10.0);
    CHECK(!policy.Observe(7.0f, 100.0));                /* the count starts here */
    CHECK(!policy.Observe(8.0f, 105.0));
    CHECK(!policy.Observe(7.0f, 109.9));
    CHECK(policy.Observe(7.0f, 110.0));
    CHECK(!policy.Observe(7.0f, 111.0));                /* and starts again after it has spoken */
}

TEST(StepDown_AnyBetterMomentStartsTheCountAgain)
{
    StepDownPolicy policy(10.0);
    CHECK(!policy.Observe(9.0f, 0.0));
    CHECK(!policy.Observe(StepDownPolicy::kHopelessLag - 0.1f, 8.0));
    CHECK(!policy.Observe(9.0f, 9.0));                  /* hopeless again, but only since now */
    CHECK(!policy.Observe(9.0f, 18.0));
    CHECK(policy.Observe(9.0f, 19.0));
}

TEST(StepDown_ALagUnderTheLimitNeverTriggers)
{
    StepDownPolicy policy(1.0);
    for (int i = 0; i < 100; ++i)
        CHECK(!policy.Observe(StepDownPolicy::kHopelessLag - 0.01f, i * 10.0));
}

TEST(StepDown_ResetForgetsTheCount)
{
    StepDownPolicy policy(5.0);
    CHECK(!policy.Observe(9.0f, 0.0));
    policy.Reset();
    CHECK(!policy.Observe(9.0f, 6.0));                  /* counted from here, not from 0 */
    CHECK(policy.Observe(9.0f, 11.0));
}
