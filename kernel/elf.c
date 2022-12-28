/*

 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
   */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "spike_interface/spike_utils.h"

typedef struct elf_info_t {
  spike_file_t *f;
  process *p;
} elf_info;

//elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
elf_ctx elfloader;

//
// the implementation of allocater. allocates memory space for later segment loading
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  // directly returns the virtual address as we are in the Bare mode in lab1_x
  return (void *)elf_va;
}

//
// actual file reading, using the spike file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  // call spike file utility to load the content of elf file into memory.
  // spike_file_pread will read the elf file (msg->f) from offset to memory (indicated by
  // *dest) for nb bytes.
  return spike_file_pread(msg->f, dest, nb, offset);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions as we are in Bare mode in lab1
//
elf_status elf_load(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;
    
    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);
    
    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;

  }

  return EL_OK;
}

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  //elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the host spike file
  spike_file_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

//elf_ctx elfloader;

long backtrace(int depth){

  int i,off;
  //string table head address
  
  //claim string table header
  elf_sect_header strtab;
  
  //string table head address
  //get string table header's info
  for(i=0,off=elfloader.ehdr.shoff;i<elfloader.ehdr.shnum;i++,off+=sizeof(strtab))
  {
    if(elf_fpread(&elfloader,(void*)&strtab, sizeof(elf_sect_header), off) != sizeof(elf_sect_header)) panic("string table header get failed!\n");
    if(strtab.type == SHT_STRTAB) break;
  }

  //save string table
  char strtab_info[STRTAB_MAX];
  if (elf_fpread(&elfloader,(void*)strtab_info, sizeof(strtab_info), strtab.offset) != sizeof(strtab_info)) panic("string table get failed!\n");
  
  //symbol table header
  elf_sect_header symtab;
  //look up for symbol table header's info
  
  for(i=0,off=elfloader.ehdr.shoff;i<elfloader.ehdr.shnum;i++,off+=sizeof(symtab))
  {
    if(elf_fpread(&elfloader,(void*)&symtab, sizeof(elf_sect_header), off) != sizeof(elf_sect_header)) panic("symbol table header get failed!\n");
    if(symtab.type == SHT_SYMTAB) break;
  }

  //save symbol table's info
  int sym_num=0;
  elf_sym symbols[MAX_DEPTH];
  elf_sym temp;

  off = symtab.offset;
  for(i=0;i<symtab.size/symtab.entsize;i++)
  {
    if(elf_fpread(&elfloader,(void*)&temp, sizeof(temp), off) != sizeof(temp)) panic("symbol table get failed!\n");
    if ((ELF64_ST_TYPE(temp.st_info)) == STT_FUNC){
      symbols[sym_num] = temp;
      sym_num++;
      //sprint("symbol:%s\n",temp.st_name+strtab_info);
    }
    off += sizeof(temp);
  }


  //f8 address
  uint64 *cur_s0 = ((uint64*)current->trapframe->regs.s0+1);
  //sprint("cur_s0:%x\n",*cur_s0);
  
  //sprint("depth:%d\n",depth);
  

  for(i=0;i<depth;i++){
    //sprint("cur_depth:%d\n",i);
    //sprint("cur_s0:%x\n",*cur_s0);
    //int flag=0;
    for(int j=0;j<sym_num;j++){
      if(symbols[j].st_value <= *cur_s0 && symbols[j].st_value+symbols[j].st_size>*cur_s0){
        off = symbols[j].st_name;
        sprint("%s\n",off+strtab_info,off);
        if(strcmp(strtab_info+off,"main")==0) return i+1;
        //sprint("st_size:%d\n",symbols[j].st_size);
        cur_s0 += 2 ;
        //flag=1;
        break;
      }
    }
    //sprint("cur_depth:%d,flag:%d\n",i,flag);
  }
  return i;
}