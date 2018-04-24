#ifndef _EVENT_EMITTER_H_
#define _EVENT_EMITTER_H_

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <list>
#include <algorithm>
#include <string>

class EventEmitter
{
  public:
    typedef std::string Event;

    EventEmitter() {}

    ~EventEmitter() {}

    template <typename... Args>
    void Emit(const Event& event, Args... args);

    template <typename... Args>
    int On(const Event& event, std::function<void(Args...)> cb);

    inline void RemoveListener(int listener_id) {
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

    inline void RemoveListenersOn(const Event& event){
        std::lock_guard<std::mutex> lock(_mutex);
        _listeners.erase(event);
    }

  private:
    struct ListenerBase
    {
        ListenerBase(int id = 0) : _id(id) {}

        virtual ~ListenerBase() {}

        int _id;
    };

    template <typename... Args>
    struct Listener : public ListenerBase
    {
        Listener() = default;

        Listener(int id, std::function<void (Args...)> cb)
            : ListenerBase(id), _cb(cb) {}

        std::function<void(Args...)> _cb;
    };

    std::mutex _mutex;
    int _last_listener;
    std::multimap<Event, std::shared_ptr<ListenerBase>> _listeners;

    EventEmitter(const EventEmitter &) = delete;
    const EventEmitter &operator=(const EventEmitter &) = delete;
};


template <typename... Args>
int EventEmitter::On(const EventEmitter::Event& event, std::function<void(Args...)> cb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    unsigned int listener_id = ++_last_listener;
    _listeners.insert(std::make_pair(event, std::make_shared<Listener<Args...>>(listener_id, cb)));
    return listener_id;
}


template <typename... Args>
void EventEmitter::Emit(const EventEmitter::Event& event, Args... args)
{
    std::list<std::shared_ptr<Listener<Args...>>> handlers;

    do {
        std::lock_guard<std::mutex> lock(_mutex);
        auto range = _listeners.equal_range(event);
        handlers.resize(std::distance(range.first, range.second));
        std::transform(
            range.first, range.second, handlers.begin(), 
            [](std::pair<Event, std::shared_ptr<ListenerBase>> p) {
                auto l = std::dynamic_pointer_cast<Listener<Args...>>(p.second);
                if (l)
                {
                    return l;
                }
                else
                {
                    throw std::logic_error("Emitter::Emit: Invalid event signature.");
                }
            });
    } while (false);

    for (auto &h : handlers)
    {
        h->_cb(args...);
    }
}


#endif
