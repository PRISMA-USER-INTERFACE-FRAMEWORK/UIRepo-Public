#include "uirepo/Animation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace uirepo;
using namespace uirepo::anim;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

constexpr Easing kAllEasings[] = {
    Easing::Linear,
    Easing::EaseInQuad,
    Easing::EaseOutQuad,
    Easing::EaseInOutQuad,
    Easing::EaseInCubic,
    Easing::EaseOutCubic,
    Easing::EaseInOutCubic,
};

void testEasingFixedPointsAndClamping() {
    for (Easing e : kAllEasings) {
        require(approx(ease(e, 0.0f), 0.0f), "easing must map 0 -> 0");
        require(approx(ease(e, 1.0f), 1.0f), "easing must map 1 -> 1");
        // Interior stays within the unit interval.
        const float mid = ease(e, 0.5f);
        require(mid >= 0.0f && mid <= 1.0f, "eased midpoint left the unit interval");
        // Out-of-range input is clamped, not extrapolated.
        require(approx(ease(e, -1.0f), 0.0f), "easing must clamp negative input to 0");
        require(approx(ease(e, 2.0f), 1.0f), "easing must clamp input above 1 to 1");
    }
    require(approx(ease(Easing::Linear, 0.5f), 0.5f), "linear midpoint");
    require(approx(ease(Easing::EaseInQuad, 0.5f), 0.25f), "ease-in-quad midpoint");
    require(approx(ease(Easing::EaseOutQuad, 0.5f), 0.75f), "ease-out-quad midpoint");
    require(approx(ease(Easing::EaseInOutQuad, 0.5f), 0.5f), "ease-in-out-quad midpoint");
}

void testLerpValueTypes() {
    require(approx(lerp(0.0f, 10.0f, 0.5f), 5.0f), "float lerp midpoint");
    require(approx(lerp(0.0f, 10.0f, 0.0f), 0.0f), "float lerp start");
    require(approx(lerp(0.0f, 10.0f, 1.0f), 10.0f), "float lerp end");
    // t is clamped.
    require(approx(lerp(0.0f, 10.0f, 2.0f), 10.0f), "float lerp clamps t>1");
    require(approx(lerp(0.0f, 10.0f, -1.0f), 0.0f), "float lerp clamps t<0");

    const Point p = lerp(Point{0.0f, 0.0f}, Point{100.0f, 40.0f}, 0.25f);
    require(approx(p.x, 25.0f) && approx(p.y, 10.0f), "point lerp");

    const Size s = lerp(Size{10.0f, 20.0f}, Size{20.0f, 40.0f}, 0.5f);
    require(approx(s.width, 15.0f) && approx(s.height, 30.0f), "size lerp");

    const Color black{0, 0, 0, 255};
    const Color white{255, 255, 255, 255};
    const Color mid = lerp(black, white, 0.5f);
    require(mid.r == 128 && mid.g == 128 && mid.b == 128 && mid.a == 255,
            "color lerp midpoint rounds to nearest byte");
    const Color start = lerp(black, white, 0.0f);
    require(start.r == 0 && start.a == 255, "color lerp start");
    const Color end = lerp(black, white, 1.0f);
    require(end.r == 255, "color lerp end");
}

void testNonFiniteNormalizedInputs() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    for (Easing e : kAllEasings) {
        const float nanValue = ease(e, nan);
        require(std::isfinite(nanValue) && approx(nanValue, 0.0f), "NaN easing input was not normalized to 0");
        require(approx(ease(e, infinity), 1.0f), "+infinity easing input was not normalized to 1");
        require(approx(ease(e, -infinity), 0.0f), "-infinity easing input was not normalized to 0");
    }

    require(approx(lerp(10.0f, 20.0f, nan), 10.0f), "NaN float lerp did not return the start value");
    require(approx(lerp(10.0f, 20.0f, infinity), 20.0f), "+infinity float lerp did not return the end value");
    require(approx(lerp(10.0f, 20.0f, -infinity), 10.0f), "-infinity float lerp did not return the start value");

    const Point point = lerp(Point{1.0f, 2.0f}, Point{3.0f, 4.0f}, nan);
    require(approx(point.x, 1.0f) && approx(point.y, 2.0f), "NaN point lerp did not return the start value");

    const Size size = lerp(Size{5.0f, 6.0f}, Size{7.0f, 8.0f}, nan);
    require(approx(size.width, 5.0f) && approx(size.height, 6.0f), "NaN size lerp did not return the start value");

    const Color start{10, 20, 30, 40};
    const Color end{100, 110, 120, 130};
    const Color nanColor = lerp(start, end, nan);
    require(nanColor.r == start.r && nanColor.g == start.g && nanColor.b == start.b && nanColor.a == start.a,
            "NaN color lerp did not return the start value");
    const Color infiniteColor = lerp(start, end, infinity);
    require(infiniteColor.r == end.r && infiniteColor.g == end.g && infiniteColor.b == end.b && infiniteColor.a == end.a,
            "+infinity color lerp did not return the end value");
}

void testPlaybackOnce() {
    Playback p{1.0f};
    require(approx(p.progress(), 0.0f), "playback starts at 0");
    require(!p.finished(), "fresh playback is not finished");

    p.advance(0.5f);
    require(approx(p.progress(), 0.5f), "half-elapsed progress");
    require(approx(p.value(), 0.5f), "linear value equals progress");
    require(!p.finished(), "not finished at half");

    p.advance(0.5f);
    require(approx(p.progress(), 1.0f), "fully elapsed progress");
    require(p.finished(), "finished at duration");

    // Overshoot stays clamped and finished.
    p.advance(10.0f);
    require(approx(p.progress(), 1.0f), "progress clamps past duration");
    require(p.finished(), "stays finished past duration");

    // Negative dt is ignored.
    const float before = p.elapsed();
    p.advance(-5.0f);
    require(approx(p.elapsed(), before), "negative dt ignored");

    p.reset();
    require(approx(p.progress(), 0.0f) && !p.finished(), "reset returns to start");
}

void testPlaybackDelay() {
    Playback p{1.0f, Easing::Linear, Repeat::Once, /*delay=*/0.5f};
    p.advance(0.5f); // consumed entirely by the delay
    require(approx(p.progress(), 0.0f), "progress stays 0 during delay");
    require(!p.finished(), "not finished during delay");
    p.advance(0.5f); // 0.5s of real progress
    require(approx(p.progress(), 0.5f), "progress advances after delay");
    p.advance(0.5f);
    require(p.finished(), "finishes delay + duration after elapsed time");
}

void testPlaybackLoop() {
    Playback p{1.0f, Easing::Linear, Repeat::Loop};
    p.advance(1.5f);
    require(approx(p.progress(), 0.5f), "loop wraps to fractional progress");
    require(!p.finished(), "loop never finishes");
    p.advance(0.5f); // total 2.0 -> wraps to 0
    require(approx(p.progress(), 0.0f), "loop wraps at whole multiples");
    require(!p.finished(), "loop still not finished");
}

void testPlaybackPingPong() {
    Playback p{1.0f, Easing::Linear, Repeat::PingPong};
    p.advance(0.5f);
    require(approx(p.progress(), 0.5f), "pingpong forward half");
    p.advance(0.5f); // total 1.0 -> peak
    require(approx(p.progress(), 1.0f), "pingpong peak at end of forward pass");
    p.advance(0.25f); // total 1.25 -> reversing
    require(approx(p.progress(), 0.75f), "pingpong reverses after peak");
    p.advance(0.25f); // total 1.5
    require(approx(p.progress(), 0.5f), "pingpong midway back");
    require(!p.finished(), "pingpong never finishes");
}

void testPlaybackZeroDuration() {
    Playback p{0.0f};
    require(approx(p.progress(), 0.0f), "zero-duration starts at 0 before advancing");
    require(!p.finished(), "zero-duration must not finish before the first advance");
    p.advance(0.01f);
    require(approx(p.progress(), 1.0f), "zero-duration completes on first advance");
    require(p.finished(), "zero-duration finishes on first positive advance");

    // Negative duration is normalized to zero, not left to divide.
    Playback n{-5.0f};
    require(!n.finished(), "normalized negative duration finished before advancing");
    n.advance(0.01f);
    require(approx(n.progress(), 1.0f) && n.finished(), "negative duration behaves as instantaneous");
}

void testPlaybackNonFiniteInputsStayFinite() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    Playback infiniteDuration{infinity};
    require(approx(infiniteDuration.duration(), 0.0f), "infinite duration was retained");
    require(!infiniteDuration.finished(), "normalized infinite duration finished before advancing");
    infiniteDuration.advance(0.01f);
    require(std::isfinite(infiniteDuration.progress()) && approx(infiniteDuration.progress(), 1.0f),
            "normalized infinite duration produced non-finite progress");
    require(infiniteDuration.finished(), "normalized infinite duration did not finish after advancing");

    Playback nanDuration{nan};
    require(approx(nanDuration.duration(), 0.0f), "NaN duration was retained");
    require(!nanDuration.finished(), "normalized NaN duration finished before advancing");
    nanDuration.advance(0.01f);
    require(approx(nanDuration.progress(), 1.0f) && nanDuration.finished(),
            "normalized NaN duration did not complete after advancing");

    Playback infiniteDelay{1.0f, Easing::Linear, Repeat::Once, infinity};
    require(approx(infiniteDelay.delay(), 0.0f), "infinite delay was retained");
    infiniteDelay.advance(0.5f);
    require(approx(infiniteDelay.progress(), 0.5f), "normalized infinite delay changed finite playback semantics");

    Playback nanDelay{1.0f, Easing::Linear, Repeat::Once, nan};
    require(approx(nanDelay.delay(), 0.0f), "NaN delay was retained");

    Playback ignoredDelta{1.0f};
    ignoredDelta.advance(0.25f);
    const float elapsedBefore = ignoredDelta.elapsed();
    ignoredDelta.advance(infinity);
    require(approx(ignoredDelta.elapsed(), elapsedBefore), "+infinity dt changed elapsed time");
    ignoredDelta.advance(nan);
    require(approx(ignoredDelta.elapsed(), elapsedBefore), "NaN dt changed elapsed time");
    ignoredDelta.advance(-infinity);
    require(approx(ignoredDelta.elapsed(), elapsedBefore), "-infinity dt changed elapsed time");
    require(std::isfinite(ignoredDelta.progress()) && approx(ignoredDelta.progress(), 0.25f),
            "invalid dt poisoned playback progress");

    Playback saturated{1.0f};
    saturated.advance(std::numeric_limits<float>::max());
    saturated.advance(std::numeric_limits<float>::max());
    require(std::isfinite(saturated.elapsed()), "finite dt accumulation overflowed elapsed time");
    require(std::isfinite(saturated.progress()) && approx(saturated.progress(), 1.0f),
            "saturated elapsed time produced invalid Once progress");

    Playback extremeLoop{std::numeric_limits<float>::min(), Easing::Linear, Repeat::Loop};
    extremeLoop.advance(std::numeric_limits<float>::max());
    const float loopProgress = extremeLoop.progress();
    require(std::isfinite(loopProgress) && loopProgress >= 0.0f && loopProgress <= 1.0f,
            "extreme finite Loop clock produced invalid progress");

    Playback extremePingPong{std::numeric_limits<float>::min(), Easing::Linear, Repeat::PingPong};
    extremePingPong.advance(std::numeric_limits<float>::max());
    const float pingPongProgress = extremePingPong.progress();
    require(std::isfinite(pingPongProgress) && pingPongProgress >= 0.0f && pingPongProgress <= 1.0f,
            "extreme finite PingPong clock produced invalid progress");
}

void testTween() {
    Tween<float> t{0.0f, 100.0f, Playback{2.0f}};
    require(approx(t.value(), 0.0f), "tween starts at from");
    t.advance(1.0f);
    require(approx(t.value(), 50.0f), "tween midpoint");
    require(!t.finished(), "tween not finished at half");
    t.advance(1.0f);
    require(approx(t.value(), 100.0f), "tween reaches to");
    require(t.finished(), "tween finishes");

    Tween<Color> fade{Color{0, 0, 0, 0}, Color{0, 0, 0, 255}, Playback{1.0f}};
    fade.advance(0.5f);
    require(fade.value().a == 128, "color tween alpha midpoint");

    // Easing is applied through the tween.
    Tween<float> eased{0.0f, 100.0f, Playback{1.0f, Easing::EaseInQuad}};
    eased.advance(0.5f);
    require(approx(eased.value(), 25.0f), "ease-in-quad tween midpoint is 25");
}

} // namespace

int main() {
    testEasingFixedPointsAndClamping();
    testLerpValueTypes();
    testNonFiniteNormalizedInputs();
    testPlaybackOnce();
    testPlaybackDelay();
    testPlaybackLoop();
    testPlaybackPingPong();
    testPlaybackZeroDuration();
    testPlaybackNonFiniteInputsStayFinite();
    testTween();
    return 0;
}
