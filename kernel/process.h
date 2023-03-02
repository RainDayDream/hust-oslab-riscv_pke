#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"

typedef struct trapframe_t {
  // space to store context (all common registers)
  /* offset:0   */ riscv_regs regs;

  // process's "user kernel" stack
  /* offset:248 */ uint64 kernel_sp;
  // pointer to smode_trap_handler
  /* offset:256 */ uint64 kernel_trap;
  // saved user process counter
  /* offset:264 */ uint64 epc;

  // kernel page table. added @lab2_1
  /* offset:272 */ uint64 kernel_satp;
}trapframe;

//added @lab2_challenge2
//block information
typedef struct block{
  uint64 start;//block's physical address
  uint64 size;//block's size
  uint64 va;//block's virtual address
  struct block *next;
}BLOCK;


// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // user page table
  pagetable_t pagetable;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;
  
  //added @lab2_challenge2,save blocks's information for allocate and free
  BLOCK* free_start;
  BLOCK* used_start;
}process;

// switch to run user app
void switch_to(process*);

// current running process
extern process* current;

// address of the first free page in our simple heap. added @lab2_2
extern uint64 g_ufree_page;

//added @lab2_challenge2
//function for allocate and free a block to user.
uint64 do_user_allocate(long n);
void do_user_free(uint64 va);


#endif
