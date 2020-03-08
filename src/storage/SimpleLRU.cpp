#include "SimpleLRU.h"
#include<iostream>


namespace Afina {
namespace Backend {

void SimpleLRU::add_node(const std::string &key,const std::string &value){

    std::unique_ptr<lru_node> cur(new lru_node(key));
    cur->value=value;

    _cur_size+=key.size()+value.size();

    cur->next=std::move(_lru_tail->prev->next);
    cur->prev=_lru_tail->prev;
    _lru_tail->prev=cur.get();
    cur->prev->next=std::move(cur);
}

void SimpleLRU::del_node(lru_node *cur){
    _cur_size-=cur->key.size()+cur->value.size();

    auto prev=cur->prev;
    prev->next=std::move(cur->next);
    prev->next->prev=prev;
}
// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) { 
    auto pair_size=key.size()+value.size();
    if(pair_size>_max_size)
        return false;

    

    auto f=_lru_index.find(key);
    std::unique_ptr<lru_node> this_node; 

    if(f!=_lru_index.end()){
        auto cur =&f->second.get();
        this_node=std::move(cur->prev->next);
        del_node(cur);
    }

    while(_cur_size+pair_size>_max_size){
        auto cur=_lru_head->next.get();
        _lru_index.erase(cur->key);
        del_node(cur);
    }

    if(f==_lru_index.end()){
        add_node(key,value);
        _lru_index.insert({_lru_tail->prev->key,*_lru_tail->prev});
    }
    else{
        std::unique_ptr<lru_node> cur=std::move(this_node);

        cur->value=value;
        _cur_size+=key.size()+value.size();

        cur->next=std::move(_lru_tail->prev->next);
        cur->prev=_lru_tail->prev;
        _lru_tail->prev=cur.get();
        cur->prev->next=std::move(cur);
    }

    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    auto f=_lru_index.find(key);
    if(f==_lru_index.end()) return Put(key,value);
    else return false;
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
    auto f=_lru_index.find(key);
    if(f==_lru_index.end()) return false;
    if(_cur_size- f->second.get().value.size()+value.size()>_max_size) return false;
    
    std::unique_ptr<lru_node> cur=std::move(f->second.get().prev->next);

    del_node(cur.get());

    cur->value=value;
    _cur_size+=key.size()+value.size();

    cur->next=std::move(_lru_tail->prev->next);
    cur->prev=_lru_tail->prev;
    _lru_tail->prev=cur.get();
    cur->prev->next=std::move(cur);
    return true;
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {
    auto f=_lru_index.find(key);
    if(f==_lru_index.end()) return false;

    auto node=&f->second.get();
    _lru_index.erase(f);
    del_node(node);
    return true;
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) {
    auto f=_lru_index.find(key);
    if(f==_lru_index.end()) return false;

    value=f->second.get().value;
    std::unique_ptr<lru_node> cur=std::move(f->second.get().prev->next);

    del_node(cur.get());

    _cur_size+=key.size()+value.size();

    cur->next=std::move(_lru_tail->prev->next);
    cur->prev=_lru_tail->prev;
    _lru_tail->prev=cur.get();
    cur->prev->next=std::move(cur);
    return true;
}

void SimpleLRU::print_index(){
    std::cout<<"Index = ";
    for (auto &i:_lru_index){
        std::cout<<i.first.get()<<"   ";
    }
    std::cout<<"\n---------------------\n";
}

void SimpleLRU::print_nodes(){
    std::cout<<"Nodes : ";
    auto h=_lru_head->next.get();
    while(h!=_lru_tail){
      std::cout<<"key = "<<h->key<<"   value = "<<h->value<<'\n';
      h=h->next.get();
    }
    std::cout<<"\n=====================\n";
}


} // namespace Backend
} // namespace Afina
