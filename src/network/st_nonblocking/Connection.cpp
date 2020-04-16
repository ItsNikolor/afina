#include "Connection.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#include <iostream>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() { std::cout << "Start" << std::endl; }

// See Connection.h
void Connection::OnError() { std::cout << "OnError" << std::endl; _is_alive=false; }

// See Connection.h
void Connection::OnClose() { std::cout << "OnClose" << std::endl; _is_alive=false; }

// See Connection.h
void Connection::DoRead(std::shared_ptr<Afina::Storage> pStorage) { 

    int readed=read(_socket,_in_buffer+_offset,_max_size-_offset);
    if(readed==0){
        OnClose();
        //is_alive=false;
        return;
    }
    if(readed==-1){
        OnError();
        //is_alive=false;
        return;
    }
    readed+=_offset;
    _offset=0;
    while(readed>0){
        if(!_command_to_execute){
            size_t parsed;
            if(_parser.Parse(_in_buffer,readed,parsed)){
                _command_to_execute=_parser.Build(_arg_remains);

                if (_arg_remains > 0) {
                    _arg_remains += 2;
                }
            }
            if(parsed){
                readed-=parsed;
                std::memmove(_in_buffer,_in_buffer+parsed,readed);
            }
            else{
                _offset=readed;
                break;
            }
        }
        if(_command_to_execute && _arg_remains>0){
            auto len=std::min(int(_arg_remains),readed);
            _argument_for_command.append(_in_buffer,len);
            readed-=len;
            _arg_remains-=len;
            std::memmove(_in_buffer,_in_buffer+len,readed);
        }
        if(_command_to_execute && !_arg_remains){
            std::string res;
            _command_to_execute->Execute(*pStorage, _argument_for_command, res);

            if(_writed==0)
                _event.events |= EPOLLOUT;
            
            std::memcpy(_out_buffer+_writed,res.data(),res.size());
            _writed+=res.size();
            if(_writed>_max_size-100)
                _event.events=EPOLLOUT;

            _parser.Reset();
            _command_to_execute.reset();
            _argument_for_command.clear();

        }
    }
        
}

// See Connection.h
void Connection::DoWrite() {
    int sended=send(_socket,_out_buffer,_writed,0);
    
    if(sended==0){
        OnClose();
        return;
    }
    if(sended==-1){
        OnError();
        return;
    }
    _writed-=sended;
    std::memmove(_in_buffer,_in_buffer+sended,_writed);
    if(_writed<_max_size-100)
                _event.events=EPOLLOUT+EPOLLIN;
    if(_writed==0)
        _event.events=EPOLLIN;
    

}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
