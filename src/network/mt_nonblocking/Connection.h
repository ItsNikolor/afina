#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <afina/Storage.h>
#include <afina/execute/Command.h>

#include "protocol/Parser.h"

#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
    Connection(int s) : _socket(s) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }

    inline bool isAlive() const { return true; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead(std::shared_ptr<Afina::Storage> pStorage);
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    static const int _max_size=2048;
    char _in_buffer[_max_size]="";
    int _readed=0;
    int _offset=0;
    char _out_buffer[_max_size]="";
    int _writed=0;

    bool _is_alive=true;


    std::size_t _arg_remains;
    Protocol::Parser _parser;
    std::string _argument_for_command="";
    std::unique_ptr<Execute::Command> _command_to_execute = nullptr;
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
