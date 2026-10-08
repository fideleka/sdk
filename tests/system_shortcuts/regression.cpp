#include "host.h"
#include <cstdio>
#include <vector>
#include <utility>
#include <limits>
#define private public
#include "controller.h"
#include "audio.h"
#include "display_settings.h"
#undef private

uint32_t hostNow = 0, hostStoredVolume = 50;
int hostNextMutex = 0, hostLocks[64] = {}, hostReads = 0, hostWrites = 0, hostTaskCount = 0;
bool hostHotScan = false, hostWriteFailure = false;
void (*hostWriteHook)() = nullptr;
MockI2S I2S;
namespace lilka {
MockSerial serial;
}
using namespace lilka;
using Event = std::pair<Button, bool>;
std::vector<Event> events;
int localEvents = 0;
Controller* peekController = nullptr;
void global(Button button, bool pressed) {
    events.push_back(Event(button, pressed));
    if (peekController) {
        State snapshot = peekController->peekState();
        assert(snapshot.select.pressed && snapshot.start.pressed && snapshot.a.pressed);
    }
}
void local(bool) { ++localEvents; }
constexpr uint16_t S = 1 << SELECT, T = 1 << START;
int scan(Controller& controller, uint16_t mask, uint32_t now) {
    hostNow = now;
    hostHotScan = true;
    const int delta = controller.scanInputs(mask, now);
    // Mirrors production inputTask: mutex is already released before RAM adjustment.
    assert(hostLocks[controller.semaphore] == 0);
    if (delta) audio.stepVolumeShortcut(delta);
    hostHotScan = false;
    return delta;
}
void callbacks(Controller& controller) {
    events.clear();
    localEvents = 0;
    controller.setGlobalHandler(global);
    for (int i = 0; i < ANY; ++i) controller.setHandler(static_cast<Button>(i), local);
}
void invisible(const ButtonState& state) {
    assert(!state.pressed && !state.justPressed && !state.justReleased && !state.nextRepeatTime);
}
void chordTests() {
    for (int direction = 0; direction < 2; ++direction) {
        Controller c;
        callbacks(c);
        c.setAutoRepeat(static_cast<Button>(direction), 100, 10);
        const uint16_t bit = 1 << direction;
        const int step = direction == UP ? 5 : (direction == DOWN ? -5 : 0);
        assert(scan(c, S, 100) == 0);
        assert(c.getState().select.justPressed); // No new Select delay.
        assert(events.size() == 1 && events[0] == Event(SELECT, true));
        assert(scan(c, S | bit, 120) == step);
        _StateButtons& states = *reinterpret_cast<_StateButtons*>(&c.state);
        invisible(states[direction]);
        assert(events.size() == 2 && localEvents == 2);
        assert(events.back() == Event(SELECT, false));
        assert(c.peekState().selectConsumed && c.peekState().selectHeld);
        invisible(c.peekState().select);
        assert(scan(c, S | bit, 519) == 0);
        assert(scan(c, S | bit, 520) == step);
        assert(scan(c, S | bit, 619) == 0);
        assert(scan(c, S | bit, 620) == step);
        assert(scan(c, bit, 640) == 0); // Select released FIRST.
        assert(!c.getState().select.justReleased);
        assert(c.peekState().selectConsumed && !c.peekState().selectHeld);
        assert(scan(c, bit, 1200) == 0);
        invisible(states[direction]);
        assert(events.size() == 2 && localEvents == 2);
        assert(scan(c, 0, 1220) == 0); // Direction release is consumed too.
        invisible(states[direction]);
        assert(events.size() == 2);
        assert(scan(c, bit, 1240) == 0); // Fresh press is ordinary.
        assert(states[direction].pressed && states[direction].justPressed);
        assert(events.back() == Event(static_cast<Button>(direction), true));
        scan(c, 0, 1260);
        assert(events.back() == Event(static_cast<Button>(direction), false));
    }
    // Every enum direction resolves from the same snapshot, not dispatch order.
    for (int direction = 0; direction < 4; ++direction) {
        Controller simultaneous, directionFirst, startFirst;
        const uint16_t bit = 1 << direction;
        assert(scan(simultaneous, S | bit, 100) == 0);
        assert((*reinterpret_cast<_StateButtons*>(&simultaneous.state))[direction].pressed);
        assert(scan(simultaneous, S | bit, 600) == 0);
        scan(directionFirst, bit, 100);
        assert(scan(directionFirst, S | bit, 120) == 0);
        assert((*reinterpret_cast<_StateButtons*>(&directionFirst.state))[direction].pressed);
        scan(startFirst, S, 100);
        assert(scan(startFirst, S | T | bit, 120) == 0);
        State state = startFirst.getState();
        assert(state.start.justPressed && state.select.pressed);
        assert((*reinterpret_cast<_StateButtons*>(&startFirst.state))[direction].pressed);
    }
    // Boot and launch/menu scans with raw Select high cannot form a volume chord.
    for (int direction = UP; direction <= DOWN; ++direction) {
        Controller boot;
        const uint16_t bit = 1 << direction;
        for (uint32_t now : {10u, 20u, 500u, 10000u}) {
            assert(scan(boot, bit, now) == 0);
            assert(!boot.peekState().selectHeld && !boot.peekState().selectConsumed);
        }
        Controller launch;
        scan(launch, S, 100);
        // Select has physically released, but its debounced level remains stale.
        assert(scan(launch, bit, 105) == 0);
        assert(launch.peekState().selectHeld);
        assert(!launch.peekState().selectConsumed);
        assert(scan(launch, bit, 106) == 0);
        assert(scan(launch, bit, 110) == 0);
        assert(!launch.peekState().selectHeld);
        assert(scan(launch, bit, 10000) == 0);
        scan(launch, 0, 10020);
        assert(scan(launch, bit, 10040) == 0);
        assert((*reinterpret_cast<_StateButtons*>(&launch.state))[direction].justPressed);
    }
    // Raw Start wins even while a recent Start release blocks its debounce.
    Controller debounceStart;
    scan(debounceStart, S | T, 100);
    scan(debounceStart, S, 110);
    assert(scan(debounceStart, S | T | 1, 115) == 0);
    assert(!debounceStart.peekState().start.pressed);
    // A physical Select release stops adjustment before its visible release event.
    Controller debounceSelect;
    scan(debounceSelect, S, 100);
    assert(scan(debounceSelect, 1, 105) == 0);
    assert(debounceSelect.peekState().select.pressed);
    Controller c;
    callbacks(c);
    scan(c, S, 100);
    scan(c, S | (1 << UP), 120);
    assert(scan(c, S | T | (1 << UP), 520) == 0); // Priority over due repeat.
    assert(c.getState().start.justPressed);
    assert(scan(c, S | (1 << UP), 620) == 0); // Start cancels repeat for this hold.
    assert(scan(c, S | (1 << UP), 1200) == 0);
    c.setSystemShortcutsEnabled(false);
    scan(c, 1 << UP, 1220);
    invisible(c.getState().up); // Disabling does not leak an already consumed hold.
    scan(c, 0, 1240);
    scan(c, S, 1260);
    assert(scan(c, S | (1 << UP), 1280) == 0);
    assert(c.getState().up.pressed);
    c.clearHandlers();
    const size_t count = events.size();
    scan(c, 0, 1300);
    assert(events.size() == count);

    detail::SystemShortcuts opposite;
    opposite.scan(S, 100, true);
    auto result = opposite.scan(S | 15, 120, true);
    assert(result.suppressed == (3 | S) && result.volumeSteps == 0);
    detail::SystemShortcuts wrap;
    wrap.scan(S, UINT32_MAX - 200, true);
    assert(wrap.scan(S | 1, UINT32_MAX - 100, true).volumeSteps == 1);
    assert(wrap.scan(S | 1, 298, true).volumeSteps == 0);
    assert(wrap.scan(S | 1, 299, true).volumeSteps == 1);
    assert(wrap.scan(S | 1, 399, true).volumeSteps == 1);
    // Unsupported brightness chords remain ordinary polling/callback input.
    for (int direction = LEFT; direction <= RIGHT; ++direction) {
        Controller c;
        callbacks(c);
        scan(c, S, 100);
        assert(scan(c, S | (1 << direction), 120) == 0);
        assert((*reinterpret_cast<_StateButtons*>(&c.state))[direction].justPressed);
        assert(events.back() == Event(static_cast<Button>(direction), true));
        scan(c, S, 140);
        assert((*reinterpret_cast<_StateButtons*>(&c.state))[direction].justReleased);
        assert(events.back() == Event(static_cast<Button>(direction), false));
    }
    puts("Chord arbitration, repeats, callbacks and release suppression PASS");
}
void ordinaryTests() {
    Controller simultaneous;
    callbacks(simultaneous);
    peekController = &simultaneous;
    scan(simultaneous, S | T | (1 << A), 100);
    assert(events.size() == 3 && localEvents == 3);
    peekController = nullptr;
    Controller c;
    callbacks(c);
    c.setAutoRepeat(UP, 5, 500);
    scan(c, 1 << UP, 100);
    assert(c.getState().up.justPressed);
    scan(c, 1 << UP, 599);
    assert(!c.getState().up.justPressed);
    scan(c, 1 << UP, 600);
    assert(c.getState().up.justPressed);
    scan(c, 1 << UP, 800);
    assert(c.getState().up.justPressed);
    scan(c, 0, 820);
    assert(c.getState().up.justReleased);
    assert(events.size() == 4 && localEvents == 4);
    for (int button = DOWN; button < ANY; ++button) {
        scan(c, 1 << button, 1000 + button * 40);
        assert((*reinterpret_cast<_StateButtons*>(&c.state))[button].justPressed);
        State peek = c.peekState();
        assert(peek.any.pressed && peek.any.justPressed);
        assert(c.peekState().any.justPressed);
        assert(c.getState().any.justPressed);
        assert(!c.peekState().any.justPressed);
        scan(c, 0, 1020 + button * 40);
        assert((*reinterpret_cast<_StateButtons*>(&c.state))[button].justReleased);
        c.resetState();
    }
    puts("ordinary controls/polling flags/peek/reset/auto-repeat/callbacks PASS");
}
void gentleShortcutTests() {
    for (bool held : {false, true}) {
        audio.setVolume(0);
        hostWrites = 0;
        Controller c;
        scan(c, S, 2000);
        assert(scan(c, S | 1, 2020) == 5 && audio.getVolume() == 1);
        const int expected[] = {2, 3, 4, 5, 10, 15};
        uint32_t now = 2420;
        for (int level : expected) {
            if (!held) { scan(c, S, now - 20); }
            assert(scan(c, S | 1, now) == 5);
            assert(audio.getVolume() == level);
            assert(audio.getVolumeOverlay().adjustedAt == now);
            now += held ? 100 : 40;
        }
        assert(hostWrites == 0);
    }
    for (int level = 0; level < 5; ++level) {
        audio.setVolume(level);
        audio.stepVolumeShortcut(1);
        assert(audio.getVolume() == level + 1);
    }
    for (int level : {0, 1, 2, 3, 4, 5, 10, 100}) {
        audio.setVolume(level);
        audio.stepVolumeShortcut(-1);
        assert(audio.getVolume() == (level == 0 ? 0 : (level <= 4 ? level - 1 : level - 5)));
    }
    // Exact descending sequences through real controller taps AND held repeats.
    for (bool held : {false, true}) {
        for (int initial : {4, 10}) {
            Controller c;
            audio.setVolume(initial);
            hostWrites = 0;
            scan(c, S, 20000);
            uint32_t now = 20020;
            const std::vector<int> expected = initial == 4 ? std::vector<int>{3, 2, 1, 0, 0} :
                                                           std::vector<int>{5, 0, 0};
            for (size_t i = 0; i < expected.size(); ++i) {
                if (i && !held) scan(c, S, now - 20);
                assert(scan(c, S | (1 << DOWN), now) == -5);
                assert(audio.getVolume() == expected[i]);
                if (expected[i] == 0) {
                    int16_t sample = 1000;
                    audio.adjustVolume(&sample, sizeof(sample), 16, audio.getVolume());
                    assert(sample == 0);
                }
                now += held ? (i == 0 ? 400 : 100) : 40;
            }
            assert(hostWrites == 0);
        }
    }
    audio.setVolume(100);
    hostWrites = 0;
    hostNow = 10000;
    audio.stepVolumeShortcut(1);
    auto feedback = audio.getVolumeOverlay();
    assert(strcmp(feedback.muteLabel, "Mute") == 0);
    strcpy(feedback.muteLabel, "Без звуку");
    assert(strcmp(audio.getVolumeOverlay().muteLabel, "Mute") == 0);
    assert(audio.getVolume() == 100 && feedback.level == 100 && feedback.visible(11199));
    assert(!feedback.visible(11200));
    hostNow = 11000;
    audio.stepVolumeShortcut(1);
    assert(audio.getVolumeOverlay().visible(12199));
    assert(!audio.getVolumeOverlay().visible(12200));
    audio.serviceVolumePersistence();
    assert(hostWrites == 0); // Limit feedback does not dirty NVS.
    audio.setVolume(0);
    hostNow = UINT32_MAX - 100;
    audio.stepVolumeShortcut(-1);
    feedback = audio.getVolumeOverlay();
    assert(feedback.level == 0 && feedback.visible(1098) && !feedback.visible(1099));
    audio.stepVolumeShortcut(0);
    assert(audio.getVolumeOverlay().adjustedAt == feedback.adjustedAt);
    Controller opposite;
    scan(opposite, S, 13000);
    scan(opposite, S | 3, 13020);
    assert(audio.getVolume() == 0 && audio.getVolumeOverlay().adjustedAt == feedback.adjustedAt);
    // Public additive API remains +5 from mute, not shortcut-specific +1.
    audio.changeVolumeLive(5);
    assert(audio.getVolume() == 5);
    assert(audio.getVolumeOverlay().adjustedAt == feedback.adjustedAt);
    puts("gentle taps/held repeats/descending/saturation/cancellation/feedback renewal+wrap PASS");
}

void persistenceTests() {
    hostNow = 5000;
    audio.setVolume(50);
    hostWrites = 0;
    hostNow = 5010;
    hostHotScan = true;
    audio.changeVolumeLive(5);
    hostHotScan = false;
    assert(audio.getVolume() == 55 && hostStoredVolume == 50 && hostWrites == 0);
    hostNow = 5410;
    audio.changeVolumeLive(5);
    hostNow = 5910;
    audio.changeVolumeLive(-5);
    audio.serviceVolumePersistence();
    assert(hostWrites == 0);
    hostNow = 6509;
    audio.serviceVolumePersistence();
    assert(hostWrites == 0);
    hostNow = 6510;
    audio.serviceVolumePersistence();
    assert(hostWrites == 1 && hostStoredVolume == 55);
    hostNow = 9000;
    audio.serviceVolumePersistence();
    assert(hostWrites == 1);
    audio.changeVolumeLive(-1000);
    assert(audio.getVolume() == 0);
    int16_t samples[] = {32767, -32768, 1000, -1000};
    audio.adjustVolume(samples, sizeof(samples), 16, audio.getVolume());
    for (auto sample : samples) assert(sample == 0);
    audio.changeVolumeLive(-5);
    assert(audio.getVolume() == 0);
    audio.changeVolumeLive(1000);
    assert(audio.getVolume() == 100);
    audio.changeVolumeLive(5);
    assert(audio.getVolume() == 100);
    int32_t wide[] = {INT32_MAX, INT32_MIN};
    audio.adjustVolume(wide, sizeof(wide), 32, 100);
    assert(wide[0] == INT32_MAX && wide[1] == INT32_MIN);
    audio.setVolume(135); // Public setter compatibility: no new clamping or key migration.
    assert(audio.getVolume() == 135 && hostStoredVolume == 135);
    audio.changeVolumeLive(-5);
    assert(audio.getVolume() == 95);
    hostNow += 600;
    hostWriteFailure = true;
    audio.serviceVolumePersistence();
    assert(hostStoredVolume == 135);
    hostWriteFailure = false;
    audio.serviceVolumePersistence();
    assert(hostStoredVolume == 135); // Failed NVS writes back off, not every 50ms tick.
    hostNow += 1000;
    audio.serviceVolumePersistence();
    assert(hostStoredVolume == 95);
    audio.changeVolumeLive(-5);
    hostNow += 600;
    hostWriteHook = []() { audio.changeVolumeLive(-5); };
    audio.serviceVolumePersistence();
    assert(hostStoredVolume == 90 && audio.getVolume() == 85);
    hostNow += 600;
    audio.serviceVolumePersistence();
    assert(hostStoredVolume == 85); // Adjustment during NVS write must stay dirty.
    audio.changeVolumeLive(-5);
    audio.setVolume(30); // An explicit save supersedes a pending shortcut save.
    const int writes = hostWrites;
    hostNow += 1000;
    audio.serviceVolumePersistence();
    assert(hostWrites == writes && hostStoredVolume == 30);
    hostWriteHook = []() { audio.changeVolumeLive(-5); };
    audio.setVolume(40);
    assert(hostStoredVolume == 40 && audio.getVolume() == 35);
    hostNow += 600;
    audio.serviceVolumePersistence();
    assert(hostStoredVolume == 35);
    hostNow = UINT32_MAX - 100;
    audio.changeVolumeLive(5);
    hostNow = 499;
    audio.serviceVolumePersistence();
    assert(hostStoredVolume == 40);
    puts("RAM gain/mute/bounds/public setters/NVS coalescing/failure/concurrent write/wrap PASS");
}
void idleWakeTests() {
    Controller controller;
    hostNow = 1000;
    assert(displaySettings.begin());
    assert(displaySettings.getTimeoutSeconds() == 0);
    assert(displaySettings.getDimTimeoutSeconds() == 0);
    assert(displaySettings.setTimeoutSeconds(120));
    hostNow = 121000;
    displaySettings.serviceIdle(false); // Active application restarts the idle clock.
    assert(!displaySettings.isSleeping());
    hostNow = 240999;
    displaySettings.serviceIdle(true);
    assert(!displaySettings.isSleeping());
    ++hostNow;
    displaySettings.serviceIdle(true);
    assert(displaySettings.isSleeping());
    events.clear();
    scan(controller, 1 << A, hostNow + 20);
    assert(displaySettings.wakePending() && !controller.peekState().a.pressed);
    assert(displaySettings.serviceIdle(true) && !displaySettings.isSleeping());
    scan(controller, 1 << A, hostNow + 20);
    assert(!controller.peekState().a.pressed); // Held wake key cannot activate anything.
    scan(controller, (1 << A) | (1 << RIGHT), hostNow + 20);
    assert(!controller.peekState().a.pressed && !controller.peekState().right.pressed);
    scan(controller, 1 << RIGHT, hostNow + 20);
    assert(!controller.peekState().right.pressed); // Entire compound wake gesture stays consumed.
    scan(controller, 0, hostNow + 20);
    scan(controller, 1 << A, hostNow + 20);
    assert(controller.peekState().a.justPressed);
    assert(displaySettings.setTimeoutSeconds(0));
    hostNow += 1000000;
    displaySettings.serviceIdle(true);
    assert(!displaySettings.isSleeping());
    puts("Actual Controller idle eligibility, timeout boundary, held wake-key consumption and disabled timeout PASS");
}
int main() {
    assert(audio.getVolume() == 50 && hostReads == 1);
    for (int i = 0; i < 100; ++i) assert(audio.getVolume() == 50);
    assert(hostReads == 1);
    audio.begin();
    audio.begin();
    assert(hostTaskCount == 1);
    chordTests();
    ordinaryTests();
    gentleShortcutTests();
    persistenceTests();
    assert(hostReads == 1);
    idleWakeTests();
    puts("real Controller + Audio host regression PASS");
}
