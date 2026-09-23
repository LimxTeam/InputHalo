#pragma once
#include "Input/Keys.h"
namespace ih {
enum class UiEventType { Move, Down, Up, Wheel, KeyDown, KeyUp, Blur };
struct UiEvent { UiEventType type{}; float x{}, y{}, wheel{}; input::Key key{}; bool shift{}; };
}
