#include "Input/InputState.h"
#include <algorithm>
namespace ih::input {
Hotkey State::Modifiers(Key primary) const {
    return {Down(Key::LeftCtrl) || Down(Key::RightCtrl), Down(Key::LeftShift) || Down(Key::RightShift),
        Down(Key::LeftAlt) || Down(Key::RightAlt), Down(Key::LeftWin) || Down(Key::RightWin), primary};
}
bool State::Held(const Hotkey& key) const {
    const auto current = Modifiers(key.primary);
    return key.Valid() && Down(key.primary) && (!key.ctrl || current.ctrl) && (!key.shift || current.shift) &&
        (!key.alt || current.alt) && (!key.win || current.win);
}
bool State::AnyMouse() const {
    for (auto key : {Key::MouseLeft,Key::MouseRight,Key::MouseMiddle,Key::Mouse4,Key::Mouse5}) if (Down(key)) return true;
    return false;
}
void State::Reset() { m_down.fill(false); m_chords.clear(); mouseReleased = wheelTime = -100; wheelDirection = 0; }
void State::Apply(const Event& event, bool shortcutOnly) {
    if (event.type == EventType::Reset) { Reset(); return; }
    if (event.type == EventType::Wheel) { wheelDirection = event.wheel; wheelTime = event.time; return; }
    if (event.key == Key::Unknown || Index(event.key) >= KeyCount) return;
    const bool repeated = Down(event.key);
    m_down[Index(event.key)] = event.type == EventType::Down;
    if (event.key >= Key::MouseLeft) { mouseReleased = event.time; return; }
    if (event.type == EventType::Up) {
        for (auto& chord : m_chords) if (chord.released < 0 && !Held(chord.combination)) chord.released = event.time;
        return;
    }
    if (IsModifier(event.key)) return;
    const auto combination = Modifiers(event.key);
    if (shortcutOnly && !combination.ctrl && !combination.shift && !combination.alt && !combination.win && !IsShortcutKey(event.key)) return;
    if (!m_chords.empty() && m_chords.back().combination == combination &&
        (repeated || event.time - m_chords.back().touched <= .25)) {
        auto& chord = m_chords.back(); chord.touched = event.time; chord.released = -1; ++chord.repeats;
    } else {
        m_chords.push_back({combination, -1, event.time, 1});
        while (m_chords.size() > 4) m_chords.pop_front();
    }
}
void State::Expire(double now, double lifetime) {
    std::erase_if(m_chords, [&](const Chord& c) { return c.released >= 0 && now - c.released >= lifetime; });
}
}
