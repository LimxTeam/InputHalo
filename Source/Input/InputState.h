#pragma once
#include "Input/Keys.h"
#include "Render2D/DrawList.h"
#include <array>
#include <deque>
namespace ih::input {
enum class EventType { Down, Up, Wheel, Reset };
struct Event {
    EventType type{};
    Key key{};
    float wheel{};
    double time{};
    render2d::Point position{};
};
struct Chord {
    Hotkey combination;
    double released = -1;
    double touched{};
    unsigned repeats = 1;
};
class State {
public:
    void Apply(const Event& event, bool shortcutOnly);
    void Expire(double now, double lifetime);
    void Reset();
    bool Down(Key key) const { return m_down[Index(key)]; }
    Hotkey Modifiers(Key primary = Key::Unknown) const;
    bool Held(const Hotkey& key) const;
    const std::deque<Chord>& Chords() const { return m_chords; }
    bool AnyMouse() const;
    double mouseReleased = -100;
    double wheelTime = -100;
    float wheelDirection{};
private:
    std::array<bool, KeyCount> m_down{};
    std::deque<Chord> m_chords;
};
}
