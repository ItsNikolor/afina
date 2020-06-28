#include"fcb.h"
#include<map>
#include<algorithm>

enum OpCode {Put,Set,Delete};
class StorageSlot {
    friend class StorageFcImpl;
    friend struct key_op_comparator;
    //friend struct FlatCombiner<StorageSlot>;

    int opcode; // put, set, delete, e.t.c
    const std::string *key;
    const std::string *value;

    bool completed;
    std::exception *ex;

    bool complete() {
    return (completed || ex != nullptr);
    }
};

struct key_op_comparator{
    bool operator()(const StorageSlot *slot1,const StorageSlot *slot2) const{
        return slot1->key<slot2->key;
    }
};

//class StorageFcImpl : public Storage {
class StorageFcImpl {
public:
    StorageFcImpl() : _combiner(flat_combine) {}

    bool Put(const std::string &key, const std::string &value) {
        StorageSlot *slot = _combiner.get_slot();
        slot->opcode = OpCode::Put;
        slot->key = &key;
        slot->value = &value;

        _combiner.apply_slot(*slot);
        if (slot->ex != nullptr) { throw *slot->ex; }

        return (*slot->value == value);
    }

protected:
    //void flat_combine(StorageSlot *begin, StorageSlot *end) override {
    static void flat_combine(StorageSlot **begin, StorageSlot **end) {
        std::sort(begin, end, key_op_comparator());

        for (auto p = begin; p != end; p+=1) {
            // eliminate as much ops as possible
            // use map methods with a hint to use the fact keys are ordered
        }
    }

private: 
   FlatCombiner<StorageSlot> _combiner;
   std::map<std::string, std::string> _backend;
};

int main(){

}