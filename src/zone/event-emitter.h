#ifndef _EVENT_EMITTER_H_
#define _EVENT_EMITTER_H_

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <list>
#include <algorithm>
#include <string>

template <typename T>
class Emitter
{
  public:
    typedef T Event;

    Emitter() {}

    ~Emitter() {}

    template <typename... Args>
    void Emit(const Event event, Args... args);

    template <typename... Args>
    unsigned int On(const Event event, std::function<void(Args...)> cb);

    unsigned int On(const Event event, std::function<void()> cb);

    void RemoveListener(unsigned int listener_id);

    void RemoveListenersOn(const Event event);

  private:
    struct ListenerBase
    {
        ListenerBase() = default;

        ListenerBase(unsigned int id) : _id(id) {}

        virtual ~ListenerBase() {}

        unsigned int _id;
    };

    template <typename... Args>
    struct Listener : public ListenerBase
    {
        Listener() = default;

        Listener(unsigned int id, std::function<void(Args...)> cb)
            : ListenerBase(id), _cb(cb) {}

        std::function<void(Args...)> _cb;
    };

    std::mutex _mutex;
    unsigned int _last_listener;
    std::multimap<Event, std::shared_ptr<ListenerBase>> _listeners;

    Emitter(const Emitter &) = delete;
    const Emitter &operator=(const Emitter &) = delete;
};

typedef Emitter<std::string> EventEmitter;

template <typename T>
template <typename... Args>
unsigned int Emitter<T>::On(const Emitter<T>::Event event, std::function<void(Args...)> cb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    unsigned int listener_id = ++_last_listener;
    _listeners.insert(std::make_pair(event, std::make_shared<Listener<Args...>>(listener_id, cb)));
    return listener_id;
}


template <typename T>
unsigned int Emitter<T>::On(const Emitter<T>::Event event, std::function<void()> cb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    unsigned int listener_id = ++_last_listener;
    _listeners.insert(std::make_pair(event, std::make_shared<Listener<>>(listener_id, cb)));
    return listener_id;
}

template <typename T>
template <typename... Args>
void Emitter<T>::Emit(const Emitter<T>::Event event, Args... args)
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


template <typename T>
void Emitter<T>::RemoveListener(unsigned int listener_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto i = std::find_if(_listeners.begin(), _listeners.end(), 
        [listener_id](const std::pair<Event, std::shared_ptr<ListenerBase>>& p) {
            return p.second->id == listener_id;
        }
    );
    if (i != _listeners.end())
    {
        _listeners.erase(i);
    }
}


template <typename T>
void Emitter<T>::RemoveListenersOn(const Event event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _listeners.erase(event);
}


#endif
