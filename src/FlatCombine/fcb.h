#include<functional>
#include<atomic>
#include<thread>

/**
 * Create new thread local variable
 */
template <typename T> class ThreadLocal {
public:
    ThreadLocal(T *initial = nullptr, std::function<void(T *)> destructor = nullptr) {
        pthread_key_create(&_th_key, destructor);
        set(initial);
    }

    inline T *get() {
        return static_cast<T*>(pthread_getspecific(_th_key));
    }

    inline void set(T *val) {
        pthread_setspecific(_th_key, static_cast<void *>(val));
    }

    T &operator*() { return *get(); }

private:
    pthread_key_t _th_key;
};

/**
 * Create new flat combine synchronizaion primitive
 *
 * @template_param OpNode
 * Class for a single pending operation descriptor. Must provides following API:
 * - complete() returns true is operation gets completed and false otherwise
 * - error(const std::exception &ex) set operation as failed. After this call return,
 *   subsequent calls to complete() method must return true
 *
 * @template_param QMS
 * Maximum array size that could be passed to a single Combine function call
 */
template <typename OpNode, std::size_t QMS = 64> class FlatCombiner {
public:
    // User defined type for the pending operations description, must be plain object without
    // virtual functions
    using pending_operation = OpNode;

    // Function that combine multiple operations and apply it onto data structure
    using combiner = std::function<void(OpNode **, OpNode **)>;

    // Maximum number of pernding operations could be passed to a single Combine call
    static const std::size_t max_call_size = QMS;
    
    /**
     * @param Combine function that aplly pending operations onto some data structure. It accepts array
     * of pending ops and allowed to modify it in any way except delete pointers
     */
    FlatCombiner(std::function<void(OpNode **, OpNode **)> combine) : _slot(nullptr, orphan_slot), _combine(combine) {
        auto head=new Slot();
        _tail=new Slot();

        head->next_and_alive.store(reinterpret_cast<uint64_t>(_tail)|LCK_BIT_MASK,std::memory_order_relaxed);
        _tail->next_and_alive.store(LCK_BIT_MASK,std::memory_order_relaxed);

        _queue=head;
        std::atomic_thread_fence(std::memory_order_release);
    }
    ~FlatCombiner() {
        auto _next=_queue->next(std::memory_order_acquire);
        while(_next!=_tail){
            dequeue_slot(_queue, _next);
            _next=_queue->next(std::memory_order_relaxed);
        }
        delete _tail;
        delete _queue;
      }

    /**
     * Return pending operation slot to the calling thread, object stays valid as long
     * as current thread is alive or got called detach method
     */
    pending_operation *get_slot() {
        Slot *result = _slot.get();
        if (result == nullptr) {
            result = new Slot();
            result->next_and_alive.store(LCK_BIT_MASK,std::memory_order_relaxed);
            _slot.set(result);
        }

        return &result->user_op;
    }

    /**
     * Put pending operation in the queue and try to execute it. Method gets blocked until
     * slot gets complete, in other words until slot.complete() returns false
     */
    void apply_slot(pending_operation &slot) {
        Slot *_slot = reinterpret_cast<Slot *>(((void *)&slot) - containerof(Slot, user_op));
        //Slot *_slot = container_of(&slot, struct Slot, user_op);

        Slot *_slot;
        _slot->generation=_lock.load(std::memory_order_acquire)&GEN_VAL_MASK;
        _slot->complete.store(false,std::memory_order_release);


        while(_slot->next(std::memory_order_acquire)==nullptr){
            auto slot_next=_queue->next_and_alive.load(std::memory_order_relaxed);
            _slot->next_and_alive.store(slot_next,
                                        std::memory_order_relaxed);

            _queue->next_and_alive.compare_exchange_weak(slot_next,
                                                        reinterpret_cast<uint64_t>(_slot)|LCK_BIT_MASK,
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed);
        }

        while(!_slot->complete.load(std::memory_order_acquire)){
            uint64_t gen;
            if((gen = try_lock(std::memory_order_release,
                            std::memory_order_relaxed))!=0){
                auto prev = _queue;
                int i=0;
                for(auto h=prev->next(std::memory_order_relaxed);
                    h!=_tail;h=h->next(std::memory_order_relaxed)){
                    if(h->complete.load(std::memory_order_acquire) && gen-h->generation&GEN_VAL_MASK>100){
                        dequeue_slot(prev, h);
                    }
                    
                    _combine_shot[i++]=&h->user_op;
                    if(i==QMS){
                        _combine(&_combine_shot[0],&_combine_shot[0]+QMS);
                        for(auto &elem:_combine_shot){
                            Slot *current = reinterpret_cast<Slot *>(((void *)elem) - containerof(Slot, user_op));
                            current->complete.store(true,std::memory_order_relaxed);
                        }
                        i=0;

                    }
                }
                _combine(&_combine_shot[0],&_combine_shot[0]+i);
                while(i){
                    Slot *current = reinterpret_cast<Slot *>(((void *)_combine_shot[--i]) - containerof(Slot, user_op));
                    current->complete.store(true,std::memory_order_relaxed);
                }
                unlock(std::memory_order_release,std::memory_order_relaxed);
            }
            else{
                std::this_thread::yield();
            }
        }


        // TODO: assert slot params
        // TODO: enqueue slot if needs
        // TODO: try to become executor (cquire lock)
        // TODO: scan qeue, dequeue stale nodes, prepare array to be passed
        // to Combine call
        // TODO: call Combine function
        // TODO: unlock
        // TODO: if lock fails, do thread_yeild and goto 3 TODO
    }

    /**
     * Detach calling thread from this flat combiner, in other word
     * destroy thread slot in the queue
     */
    void detach() {
        pending_operation *result = _slot.get();
        if (result != nullptr) {
            _slot.set(nullptr);
        }
        orphan_slot(result);
    }

protected:
    // Extend user provided pending operation type with fields required for the
    // flat combine algorithm to work
    using Slot = struct Slot {
        // User pending operation to be complete
        OpNode user_op;

        // When last time this slot was detected as been in use
        uint64_t generation;

        // Pointer to the next slot. One bit of pointer is stolen to
        // mark if owner thread is still alive, based on this information
        // combiner/thread_local destructor able to take decission about
        // deleting node.
        //
        // So if stolen bit is set then the only reference left to this slot
        // if the queue. If pointer is zero and bit is set then the only ref
        // left is thread_local storage. If next is zero there are no
        // link left and slot could be deleted
        std::atomic<uint64_t> next_and_alive;

        /**
         * Remove alive bit from the next_and_alive pointer and return
         * only correct pointer to the next slot
         */
        Slot *next(std::memory_order mo) {
            return reinterpret_cast<Slot *>(next_and_alive.load(mo)&GEN_VAL_MASK);
        }

        std::atomic<bool> complete;
    };

    /**
     * Try to acquire "lock", in case of success returns current generation. If
     * fails the return 0
     *
     * @param suc memory barier to set in case of success lock
     * @param fail memory barrier to set in case of failure
     */
    uint64_t try_lock(std::memory_order suc, std::memory_order fail) {
        auto l=_lock.load(std::memory_order_acquire);
        if(l&LCK_BIT_MASK==1){
            return 0;
        }
        l=l&GEN_VAL_MASK;
        if(_lock.compare_exchange_weak(l,l|LCK_BIT_MASK,suc,fail)){
            return l;
        }
        return 0;
    }

    /**
     * Try to release "lock". Increase generation number in case of sucess
     *
     * @param suc memory barier to set in case of success lock
     * @param fail memory barrier to set in case of failure
     */
    void unlock(std::memory_order suc, std::memory_order fail) {
        _lock.store(_lock.load(std::memory_order_relaxed)&GEN_VAL_MASK+1,suc);
    }

    /**
     * Remove slot from the queue. Note that method must be called only
     * under "lock" to eliminate concurrent queue modifications
     *
     */
    void dequeue_slot(Slot *parent, Slot *slot2remove) {
        if(parent==_queue){
            auto head_next=reinterpret_cast<uint64_t>(slot2remove)|LCK_BIT_MASK;
            if(!parent->next_and_alive.compare_exchange_strong(head_next,
                                    reinterpret_cast<uint64_t>(slot2remove->next(std::memory_order_relaxed))|LCK_BIT_MASK,
                                    std::memory_order_release,
                                    std::memory_order_relaxed)){
                while(parent->next(std::memory_order_relaxed)!=slot2remove){
                    parent=parent->next(std::memory_order_relaxed);
                }
            }
            else{
                auto next=slot2remove->next_and_alive.load(std::memory_order_relaxed);
                if(slot2remove->next_and_alive.compare_exchange_strong(next,next&LCK_BIT_MASK),
                                                                        std::memory_order_release,
                                                                        std::memory_order_relaxed){
                    if(next&LCK_BIT_MASK==0){
                        delete slot2remove;
                    }
                }
                else{
                    delete slot2remove;
                }
                return; 
            }
        }

        auto next=parent->next_and_alive.load(std::memory_order_acquire);
        auto new_next=reinterpret_cast<uint64_t>(slot2remove->next(std::memory_order_relaxed))|(next&LCK_BIT_MASK);
        if(!parent->next_and_alive.compare_exchange_strong(next,new_next,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed)){
            parent->next_and_alive.store(new_next&GEN_VAL_MASK,std::memory_order_release);
        }

        next=slot2remove->next_and_alive.load(std::memory_order_relaxed);
        if(slot2remove->next_and_alive.compare_exchange_strong(next,next&LCK_BIT_MASK),
                                                                std::memory_order_release,
                                                                std::memory_order_relaxed){
            if(next&LCK_BIT_MASK==0){
                delete slot2remove;
            }
        }
        else{
            delete slot2remove;
        }
    }

    /**
     * Function called once thread owning this slot is going to die or to
     * destory slot in some other way
     *
     * @param Slot pointer to the slot is being to orphan
     */
    static void orphan_slot(Slot *s) {
        auto next=s->next_and_alive.load(std::memory_order_acquire);
        while(true){
            if(s->next_and_alive.compare_exchange_weak(next,next&GEN_VAL_MASK,
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed)){
                if(next&GEN_VAL_MASK==0){
                    delete s;
                }
                break;
            }
            next=s->next_and_alive.load(std::memory_order_relaxed);
        }
    }

private:
    static constexpr uint64_t LCK_BIT_MASK = uint64_t(1) << 63L;
    static constexpr uint64_t GEN_VAL_MASK = ~LCK_BIT_MASK;

    // First bit is used to see if lock is acquired already or no. Rest of bits is
    // a counter showing how many "generation" has been passed. One generation is a
    // single call of flat_combine function.
    //
    // Based on that counter stale slots found and gets removed from the pending
    // operations queue
    std::atomic<uint64_t> _lock;

    // Pending operations queue. Each operation to be applied to the protected
    // data structure is ends up in this queue and then executed as a batch by
    // flat_combine method call
    //std::atomic<Slot *> _queue;
    Slot * _queue;

    Slot *_tail;

    
    // Function to call in order to execute operations
    combiner _combine;

    // Usual strategy for the combine flat would be sort operations by some creteria
    // and optimize it somehow. That array is using by executor thread to prepare
    // number of ops to pass to combine
    std::array<OpNode *, QMS> _combine_shot;

    // Slot of the current thread. If nullptr then cur thread gets access in the
    // first time or after a long period when slot has been deleted already
    ThreadLocal<Slot> _slot;
};