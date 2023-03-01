#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "string.h"

#define FILENAME_MAX 100
#define FILE_MAX 1000

static void print_error_line()
{
  uint64 epc=read_csr(mepc);
  addr_line *cur_line=current->line;
  int i=0;
  
//find errorline's instruction address
  for(i=0,cur_line=current->line;i<current->line_ind&&cur_line->addr!=epc;++i,++cur_line)
  {
    //sprint("i=%d,addr=%x\n",i,cur_line->addr);
    if(cur_line->addr==epc) break;
  }
  if(i==current->line_ind) panic("can't find errorline!\n");
  //sprint("find errorline,i=%d\n",i);


//find file's path and name
  char filename[FILENAME_MAX];
  //find filename
  code_file *cur_file=current->file+cur_line->file;
  char *single_name=cur_file->file;
  //find file's path
  char *file_path=(current->dir)[cur_file->dir];
  //combine path and name
  int start=strlen(file_path);
  strcpy(filename,file_path);
  filename[start]='/';
  start++;
  strcpy(filename+start,single_name);
  sprint("Runtime error at %s:%d\n", filename, cur_line->line);
  
//find error line
  //error instruction's line
  int error_line=cur_line->line;
  //open file
  spike_file_t *file=spike_file_open(filename,O_RDONLY,0);
  if (IS_ERR_VALUE(file)) panic("open file failed!\n");
  //get file's content
  char file_detail[FILE_MAX];
  spike_file_pread(file,(void*)file_detail,sizeof(file_detail),0);

  //fine error line's start 
  int line_start=0;
  for(i=1;i<error_line;i++)
  {
    //sprint("line=%d,%s\n",i,file_detail+line_start);
    while(file_detail[line_start]!='\n') line_start++;
    line_start++;
  }
  char *errorline=file_detail+line_start;
  while(*errorline!='\n') errorline++;
  *errorline='\0';
//print error line
  sprint("%s\n",file_detail+line_start);
  spike_file_close(file);
}

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }


//added @lab1_challenge2 
static void handle_illegal_instruction() 
{ 
  print_error_line();
  panic("Illegal instruction!"); 
}

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      
/*      //debug for @lab1_challenge2 
  addr_line *cur_line=current->line;
  int i;
  for(i=0;i<current->line_ind;i++,cur_line++)
  {
    sprint("i=%d,addr=%x\n",i,cur_line->addr);
  }
*/
      handle_illegal_instruction();

      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
