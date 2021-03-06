/* 046267 Computer Architecture - Spring 2020 - HW #4 */

#include "core_api.h"
#include "sim_api.h"

#include <vector>

using std::vector;

// ================================== Enums & defines ==================================== //
#define IS_WAITING(thread) thread.waiting

typedef enum { WORKING, STOPPED } ThreadState;
typedef enum { BLOCKED, FINE_GRAINED } MtConfig; // type of MT

// ================================== Thread class ======================================= //

class Thread {
public:
    vector<int> regs;
    ThreadState state;
    int thread_num;
    int inst_count,cycles_mem; // cycles mem remembers the cycle we can run in. (use for load store & switches).
    bool waiting;
    explicit Thread(int thread_num) : thread_num(thread_num),inst_count(0),cycles_mem(0),waiting(false){
        for(int i = 0 ; i < REGS_COUNT ; i++) {
            this->regs.push_back(0); //8 empty regs
        }
        this->state = STOPPED; //default
    }
    Thread(const Thread&) = default;
    Thread& operator=(const Thread&) = default;
    ~Thread() = default;
};

class SystemVariable {
    vector<Thread> threads;
    vector<int> stopped;
    int insts_count;
    int cycle_count;
    int load_latency,store_latency;
    int switch_price;
    int RR;
    Instruction *curr_inst;
public:
    explicit SystemVariable() {
        this->curr_inst = (Instruction* )malloc(sizeof(*curr_inst));
        if(!this->curr_inst){
            throw std::exception();
        }
        this->cycle_count = this->insts_count = 0;
        this->RR = 0;
    };
    ~SystemVariable() {
        if(curr_inst) {
            free(curr_inst);
            curr_inst = nullptr;
        }
    };
    // round robin
    int next_RR(){
        int next_RR = RR;
        bool found = false;
        for(unsigned int i = RR+1; i < threads.size() ; i++){
            if(threads[i].state == STOPPED) continue;
            if(threads[i].cycles_mem < cycle_count) {
                next_RR = i;
                found = true;
                break;
            }
        }
        if(!found) {
            for (int i = 0; i < RR; i++) {
                if (threads[i].state == STOPPED) continue;
                if (threads[i].cycles_mem < cycle_count) {
                    next_RR = i;
                    break;
                };
            }
        }
        return next_RR;
    }
    // commands
    void nop(){
        this->threads[RR].inst_count += 1;
        this->threads[RR].cycles_mem = this->cycle_count;
    }
    void add(){
        if(!this->curr_inst->isSrc2Imm){
            this->threads[RR].regs[this->curr_inst->dst_index] =
                    this->threads[RR].regs[this->curr_inst->src1_index] + this->threads[RR].regs[this->curr_inst->src2_index_imm];
        } else {
            this->threads[RR].regs[this->curr_inst->dst_index] =
                    this->threads[RR].regs[this->curr_inst->src1_index] + this->curr_inst->src2_index_imm;
        }
        this->threads[RR].cycles_mem = this->cycle_count;
        this->threads[RR].inst_count += 1;
        this->cycle_count += 1;
        this->insts_count += 1;
    }
    void sub(){
        if(!this->curr_inst->isSrc2Imm) {
            this->threads[RR].regs[this->curr_inst->dst_index] =
                    this->threads[RR].regs[this->curr_inst->src1_index] - this->threads[RR].regs[this->curr_inst->src2_index_imm];
        } else {
            this->threads[RR].regs[this->curr_inst->dst_index] =
                    this->threads[RR].regs[this->curr_inst->src1_index] - this->curr_inst->src2_index_imm;
        }
        this->threads[RR].cycles_mem = this->cycle_count;
        this->threads[RR].inst_count += 1;
        this->cycle_count += 1;
        this->insts_count += 1;
    }
    void load() {
        this->threads[RR].waiting = true;
        this->threads[RR].cycles_mem = this->cycle_count + load_latency;
        if(this->curr_inst->isSrc2Imm) {
            SIM_MemDataRead((uint32_t )(this->curr_inst->src2_index_imm + this->threads[RR].regs[this->curr_inst->src1_index])
                    , reinterpret_cast<int32_t *>(&this->threads[RR].regs[this->curr_inst->dst_index]));
        } else {
            SIM_MemDataRead((uint32_t )(this->threads[RR].regs[this->curr_inst->src2_index_imm] + this->threads[RR].regs[this->curr_inst->src1_index])
                    , reinterpret_cast<int32_t *>(&this->threads[RR].regs[this->curr_inst->dst_index]));
        }
        this->threads[RR].inst_count += 1;
        this->cycle_count += 1;
        this->insts_count += 1;
    }
    void store() {
        this->threads[RR].waiting = true;
        this->threads[RR].cycles_mem = this->cycle_count + store_latency;
        if(this->curr_inst->isSrc2Imm) {
            SIM_MemDataWrite((uint32_t )(this->curr_inst->src2_index_imm + this->threads[RR].regs[this->curr_inst->dst_index])
                    , (uint32_t )(this->threads[RR].regs[this->curr_inst->src1_index]));
        } else {
            SIM_MemDataWrite((uint32_t )(this->threads[RR].regs[curr_inst->src2_index_imm] + this->threads[RR].regs[this->curr_inst->dst_index])
                    , (uint32_t )(this->threads[RR].regs[this->curr_inst->src1_index]));
        }
        this->threads[RR].inst_count += 1;
        this->cycle_count += 1;
        this->insts_count += 1;
    }
    void halt() {
        //stop thread.
        this->threads[RR].state = STOPPED;
        this->cycle_count += 1;
        this->insts_count += 1;
        stopped.push_back(RR);
    }
    void core_init() {
        //init
        int thread_num = SIM_GetThreadsNum();
        this->store_latency = SIM_GetStoreLat();
        this->load_latency = SIM_GetLoadLat();
        this->switch_price = SIM_GetSwitchCycles();
        for(int i = 0 ; i < thread_num ; i++) {
            this->threads.emplace_back(i);
            this->threads[i].state = WORKING;
        }
    }
    // command & thread runners
    void run_inst() {
        SIM_MemInstRead((uint32_t)(this->threads[RR].inst_count),this->curr_inst,this->threads[RR].thread_num);
        if(curr_inst->opcode == CMD_NOP){
            this->nop();

        } else if(curr_inst->opcode == CMD_ADD) {
            this->add();

        } else if(curr_inst->opcode == CMD_ADDI) {
            this->add();

        } else if(curr_inst->opcode == CMD_SUB) {
            this->sub();

        } else if(curr_inst->opcode == CMD_SUBI) {
            this->sub();

        } else if(curr_inst->opcode == CMD_LOAD) {
            this->load();

        } else if(curr_inst->opcode == CMD_STORE) {
            this->store();

        } else if(curr_inst->opcode == CMD_HALT) {
            this->halt();
        } else return;
    }
    // blocked implementation
    void blocked_x() {
        bool is_idle_f = false, is_change = false;
        while(true) {
            if(stopped.size() < threads.size()) {
                if(!is_change) {
                    bool all_busy = true;
                    for (unsigned int i = 0; i < threads.size(); i++) {
                        if (threads[i].state == STOPPED) continue;
                        if (threads[i].cycles_mem < cycle_count) {
                            all_busy = false;
                        }
                    }
                    if (all_busy && cycle_count) {
                        //idle state
                        this->cycle_count += 1;
                        if (!is_idle_f) {
                            is_idle_f = true;
                        }
                        continue;
                    }
                    if (is_idle_f) {
                        // out of idle
                        if ((threads[RR].cycles_mem >= this->cycle_count) && (RR != this->next_RR())) {
                            this->cycle_count += switch_price;
                            RR = next_RR();
                        }
                        is_idle_f = false;
                    }
                }
                //run curr thread
//                run_cur_thread(curr_inst)
                if(!IS_WAITING(threads[RR]) && (threads[RR].state != STOPPED)){
                    if(threads[RR].cycles_mem > cycle_count) {
                        threads[RR].waiting = true;
                        this->cycle_count += 1;
                        continue;
                    }
                    run_inst();
                    is_change = true;
                    continue;
                } else {
                    is_change = false;
                }
                threads[RR].waiting = false;
                if (stopped.size()==threads.size()) {
                    break;
                }
                this->cycle_count += (RR == next_RR()) ? 0 : switch_price;
                RR = next_RR();
            } else break;
        }
    }
    // fine implementation
    void fine_grained_x() {
        bool is_idle_f = false;
        while(true) {
            if(stopped.size() < threads.size()) {
                bool all_busy = true;
                for(unsigned int i=0 ; i < threads.size() ; i++){
                    if(threads[i].state == STOPPED) continue;
                    if(threads[i].cycles_mem < cycle_count){
                        all_busy = false;
                    }
                }
                if (all_busy && cycle_count) {
                    this->cycle_count += 1;
                    if(!is_idle_f) {
                        is_idle_f = true;
                    }
                    continue;
                }
                if (is_idle_f) {
                    RR = next_RR();
                    is_idle_f = false;
                }
                if(threads[RR].cycles_mem > cycle_count) {
                    threads[RR].waiting = true;
                    this->cycle_count += 1;
                    continue;
                }
                run_inst();
                if (stopped.size()==threads.size()) {
                    break;
                } else {
                    //always switch
                    RR = next_RR();
                    continue;
                }
            } else break;
        }
    }
    // main core functions
    void core(MtConfig config){
        core_init();
        if(curr_inst == nullptr) {
            return;
        }
        // run
        switch (config) {
            case (BLOCKED) :
                blocked_x();
                break;

            case (FINE_GRAINED) :
                fine_grained_x();
                break;

            default:
                break;
        }
    }
    double cpi() const {
        return (((double )this->cycle_count)/this->insts_count);
    }
    void core_context(tcontext *ctx,int thread_id) const {
        if(thread_id < threads.size()) {
            int idx = 0;
            for (auto &reg : threads[thread_id].regs) {
                ctx[thread_id].reg[idx] = reg;
                idx++;
            }
        }
//        else printf("thread id too big\n");
    }
};

static SystemVariable *blocked;
static SystemVariable *fine_grained;

// =============================== Core Functions ========================================= //

void CORE_BlockedMT() {
    try {
        blocked = new SystemVariable();
        if(blocked == nullptr) {
//        printf("alloc err\n");
            return;
        }
    } catch (std::exception&) {
        return;
    }
    blocked->core(BLOCKED);
}

void CORE_FinegrainedMT() {
    try {
        fine_grained = new SystemVariable();
        if(fine_grained == nullptr){
//        printf("alloc err\n");
            return;
        }
    } catch (std::exception&) {
        return;
    }

    fine_grained->core(FINE_GRAINED);
}

double CORE_BlockedMT_CPI(){
    if(blocked == nullptr) {
//        printf("alloc err\n");
        return 0;
    }
    double cpi = blocked->cpi();
    delete blocked;
    blocked = nullptr;
    return cpi;
}

double CORE_FinegrainedMT_CPI(){
    if(fine_grained == nullptr) {
//        printf("alloc err\n");
        return 0;
    }
    double cpi = fine_grained->cpi();
    delete fine_grained;
    fine_grained = nullptr;
    return cpi;
}

void CORE_BlockedMT_CTX(tcontext* context, int threadid) {
    blocked->core_context(context,threadid);
}

void CORE_FinegrainedMT_CTX(tcontext* context, int threadid) {
    fine_grained->core_context(context,threadid);
}


