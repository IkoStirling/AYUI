#pragma once
#ifndef AY_EVENT_H
#define AY_EVENT_H

// AYEvent - Event system for AYUI

namespace ayui::events
{

enum class EventType
{
    None,
    Click,
    Hover,
    KeyPress,
    Resize,
    Focus,
    Blur
};

struct Event
{
    EventType type = EventType::None;
    void* sender = nullptr;
};

}

#endif
