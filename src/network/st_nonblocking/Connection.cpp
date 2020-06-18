#include "Connection.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

#include <string>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() {}

// See Connection.h
void Connection::OnError() { _is_alive = false; }

// See Connection.h
void Connection::OnClose() { _is_alive = false; }

// See Connection.h
void Connection::DoRead() {
    while (true) {
        int readed = read(_socket, _in_buffer + _offset, _max_size - _offset);
        if (readed == 0) {
            // OnClose();
            _is_alive = false;
            return;
        }
        if (readed == -1) {
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
                OnError();
            return;
        }
        readed += _offset;
        _offset = 0;
        while (readed > 0) {
            if (!_command_to_execute) {
                size_t parsed;
                if (_parser.Parse(_in_buffer, readed, parsed)) {
                    _command_to_execute = _parser.Build(_arg_remains);

                    if (_arg_remains > 0) {
                        _arg_remains += 2;
                    }
                }
                if (parsed) {
                    readed -= parsed;
                    std::memmove(_in_buffer, _in_buffer + parsed, readed);
                } else {
                    _offset = readed;
                    break;
                }
            }
            if (_command_to_execute && _arg_remains > 0) {
                auto len = std::min(int(_arg_remains), readed);
                _argument_for_command.append(_in_buffer, len);
                readed -= len;
                _arg_remains -= len;
                std::memmove(_in_buffer, _in_buffer + len, readed);
            }
            if (_command_to_execute && !_arg_remains) {
                std::string res;
                _command_to_execute->Execute(*_pStorage, _argument_for_command, res);

                bool was_empty = _output_queue.empty();
                _output_queue.push_back(res);

                if (was_empty) {
                    _event.events |= EPOLLOUT;
                }

                _parser.Reset();
                _command_to_execute.reset();
                _argument_for_command.clear();
            }
        }
    }
}

// See Connection.h
void Connection::DoWrite() {
    while (true) {
        int ivlen = 0;
        const size_t len_iov = 64;
        struct iovec iov[len_iov];
        for (auto &buf : _output_queue) {
            iov[ivlen].iov_base = &buf[0];
            iov[ivlen].iov_len = buf.size();

            if (++ivlen == len_iov) {
                break;
            }
        }

        iov[0].iov_base = static_cast<void *>(static_cast<char *>(iov[0].iov_base) + _first_written);
        iov[0].iov_len -= _first_written;

        int written = writev(_socket, iov, ivlen);
        if (written == 0) {
            OnClose();
            return;
        }
        if (written < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                return;
            } else {
                OnError();
                return;
            }
        }

        // written bytes
        written += _first_written;
        auto it = _output_queue.begin();
        for (auto end = _output_queue.end(); it != end; it++) {
            if (it->size() <= written) {
                written -= it->size();
            } else {
                break;
            }
        }

        // it - first buffer not fullly written
        // written - how much written from it buffer
        _output_queue.erase(_output_queue.begin(), it);
        _first_written = written;

        if (_output_queue.empty()) {
            _event.events = EPOLLIN;
        }
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
