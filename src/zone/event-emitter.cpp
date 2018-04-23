#include "event-emitter.h"

void EventEmitter::RemoveListener(int listener_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto i = std::find_if(_listeners.begin(), _listeners.end(), 
        [listener_id](const std::pair<Event, std::shared_ptr<ListenerBase>>& p) {
            return p.second->_id == listener_id;
        }
    );
    if (i != _listeners.end())
    {
        _listeners.erase(i);
    }
}


void EventEmitter::RemoveListenersOn(const EventEmitter::Event& event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _listeners.erase(event);
}

