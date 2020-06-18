#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <afina/Storage.h>
#include <afina/execute/Command.h>

#include "protocol/Parser.h"

#include <sys/epoll.h>

#include <list>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps) : _socket(s), _pStorage(ps) {
        // std::memset(&_event, 0, sizeof(struct epoll_event)); //зачем?
        _event.data.ptr = this;
        _event.events = EPOLLIN;
    }

    inline bool isAlive() const { return true; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;
    int _offset = 0;
    static const int _max_size = 2048;
    char _in_buffer[_max_size];

    bool _is_alive = true;

    std::size_t _arg_remains;
    Protocol::Parser _parser;
    std::string _argument_for_command = "";
    std::unique_ptr<Execute::Command> _command_to_execute = nullptr;

    std::list<std::string> _output_queue;
    std::size_t _first_written;
    std::shared_ptr<Afina::Storage> _pStorage;
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
