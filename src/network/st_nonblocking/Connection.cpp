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
void Connection::OnError() { std::cout << "OnError" << std::endl; is_alive=false; }

// See Connection.h
void Connection::OnClose() { std::cout << "OnClose" << std::endl; is_alive=false; }

// See Connection.h
void Connection::DoRead(std::shared_ptr<Afina::Storage> pStorage) { 

    int readed=read(_socket,in_buffer+offset,max_size-offset);
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
    readed+=offset;
    offset=0;
    while(readed>0){
        if(!command_to_execute){
            size_t parsed;
            if(parser.Parse(in_buffer,readed,parsed)){
                command_to_execute=parser.Build(arg_remains);

                //std::cout<<arg_remains<<std::endl;


                if (arg_remains > 0) {
                    arg_remains += 2;
                }
            }
            if(parsed){
                readed-=parsed;
                std::memmove(in_buffer,in_buffer+parsed,readed);
            }
            else{
                offset=readed;
                break;
                //if(!command_to_execute) break;
            }
        }
        if(command_to_execute && arg_remains>0){
            auto len=std::min(int(arg_remains),readed);
            argument_for_command.append(in_buffer,len);
            readed-=len;
            arg_remains-=len;
            std::memmove(in_buffer,in_buffer+len,readed);
        }
        if(command_to_execute && !arg_remains){
            std::string res;
            command_to_execute->Execute(*pStorage, argument_for_command, res);

            if(writed==0)
                _event.events=EPOLLIN|EPOLLOUT;
            
            std::memcpy(out_buffer+writed,res.data(),res.size());
            writed+=res.size();
            if(writed>max_size-100)
                _event.events=EPOLLOUT;

            parser.Reset();
            command_to_execute.reset();
            argument_for_command.clear();

        }
    }
        
}

// See Connection.h
void Connection::DoWrite() {
    int sended=send(_socket,out_buffer,writed,0);
    
    if(sended==0){
        OnClose();
        return;
    }
    if(sended==-1){
        OnError();
        return;
    }
    writed-=sended;
    std::memmove(in_buffer,in_buffer+sended,writed);
    if(writed<max_size-100)
                _event.events=EPOLLOUT+EPOLLIN;
    if(writed==0)
        _event.events=EPOLLIN;
    

}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
