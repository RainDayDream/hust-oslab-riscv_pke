# OS课设实验思路raindaydream

## lab1_challenge1打印堆栈

已完成，从上次报告中直接粘贴

## lab1_challenge2打印异常代码行

**实验目标：**

​	修改内核（包括machine文件夹下）的代码，使得用户程序在发生异常时，内核能够输出触发异常的用户程序的源文件名和对应代码行。

**实验指导：**

* 修改读取elf文件的代码，找到包含调试信息的段，将其内容保存起来（可以保存在用户程序的地址空间中）
* 在适当的位置调用debug_line段解析函数，对调试信息进行解析，构造指令地址-源代码行号-源代码文件名的对应表，注意，连续行号对应的不一定是连续的地址，因为一条源代码可以对应多条指令。
* 在异常中断处理函数中，通过相应寄存器找到触发异常的指令地址，然后在上述表中查找地址对应的源代码行号和文件名输出

**实验思路：**

1. 读出debug段的信息：

   ①读字符串表：采用文件头的信息和elf_fpread读取；注意坑1

   ②遍历段头部，根据name和字符串表找到“.debug_line”段

   ③读debug段内容，注意坑4、2

2. 使用make_addr_line解析debug段信息得到指令地址-代码行-文件这个表和相关信息

   make_addr_line放在加载debug段的函数中，因此**需要在合适位置调用加载debug段的函数，**分析需要在进程调度之前将这个文件加载进去，跟踪进程调度的函数发现，在进程调度之前会加载文件相关信息，即load_bincode_from_host_elf，因此可以在该函数中的适当位置调用load_debugline，在elf_load被调用之后调用load_debugline即可

   至此处理异常所需要的信息已经写好

3. 打印异常代码行：

   ①遍历进程的line，找到指令地址与异常代码的指令地址相同的指令信息(cur_line)；

   ②根据cur_line中的file地址和进程中的file信息起始地址找到该代码行所在文件的信息（cur_file）；

   ③根据该文件信息中的dir和进程中的文件路径保存位置dir找到该文件的文件路径；

   ④将文件路径和文件名拼接得到文件的整体路径，同时异常代码的所在位置即为①中得到的信息中的line；至此得到代码出错的文件+路径+位置；

   ⑤根据④中得到的路径，打开文件，调用函数将其中的内容读出来；

   ⑥根绝cur_line中的line知道出错代码行的位置line，对⑤中得到的内容进行遍历：以换行符为一行的结束进行判断找到line的起始位置；

   ⑦将该文件从⑥中找到的起始位置开始打印出来，直到遇到换行符。



**实验栽的坑：**

1. 不可以对字节头进行遍历判断type找字符串表：因为一个文件有多个字符串表

2. 保存debug段信息的char数组的数组大小应该开的大一些，8000能过educoder,否则就会出现各种各样的问题，包括但不限于：make_addr_line死循环、加载失败、AMO等等

3. 找当前指令地址的时候，不可以直接使用epc，需要调用宏read_csr(mepc)，二者的值不相同

4. 构造表被破坏：指令地址-代码行-文件这个表被破坏，

   原因：保存debug段文件的字符串用的是char，用static char就正确了

5. 代码行位置从1开始

## 实验代码

```
//对process结构的修改
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;

  // added @lab1_challenge2
  char *debugline; char **dir; code_file *file; addr_line *line; int line_ind;
}process;
```

```
//读debug段，在elf文件
//added @lab1_challenge2，elf.h
#define SHT_STRTAB 3
#define STRTAB_MAX 300
#define DEBUGLINE_MAX 8000
elf_status load_debugline(elf_ctx *ctx);

//added @lab1_challenge2 | load debugline ,elf.c
elf_status load_debugline(elf_ctx *ctx)
{
  int i,off;

  //claim string table header
  elf_sect_header strtab;
  //get string table header's info
  off = ctx->ehdr.shoff;
  off += sizeof(strtab) * (ctx->ehdr.shstrndx);
  if(elf_fpread(ctx,(void*)&strtab, sizeof(strtab), off) != sizeof(strtab)) panic("string table header get failed!\n");
  //save string table
  static char strtab_info[STRTAB_MAX];
  if (elf_fpread(ctx,(void*)strtab_info,strtab.size, strtab.offset) != strtab.size) panic("string table get failed!\n");
  
  //get .debug_line segment
  elf_sect_header debugseg;
  for(i=0,off=ctx->ehdr.shoff;i<ctx->ehdr.shnum;i++,off+=sizeof(debugseg))
  {
    if(elf_fpread(ctx,(void*)&debugseg, sizeof(elf_sect_header), off) != sizeof(elf_sect_header)) panic("debug header get failed!\n");
    //sprint("i=%d,%s\n",i,strtab_info+debugseg.name);
    if(strcmp((char*)(strtab_info+debugseg.name),".debug_line")==0) break;
  }

  if(i==ctx->ehdr.shnum){
    panic("can't find debugline!\n");
    return EL_ERR;
  } 

  //get debugline's information
  static char debugline[DEBUGLINE_MAX];
  if(elf_fpread(ctx,(void*)debugline,debugseg.size,debugseg.offset)!=debugseg.size) panic("debugline get failed!\n");
  make_addr_line(ctx,debugline,debugseg.size);
  return EL_OK;
}
```

```
//异常处理，mtrap.c
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
```



## lab2_challenge2 堆空间管理 

**实验目标：**

​	挑战任务是**修改内核(包括machine文件夹下)的代码，使得应用程序的malloc能够在一个物理页中分配，并对各申请块进行合理的管理**。

​	通过修改PKE内核（包括machine文件下的代码），实现优化后的malloc函数，使得应用程序两次申请块在同一页面，并且能够正常输出存入第二块中的字符串"hello world"。

**实验指导：**

- 增加内存控制块数据结构对分配的内存块进行管理。

- 修改process的数据结构以扩展对虚拟地址空间的管理，后续对于heap的扩展，需要**对新增的虚拟地址添加对应的物理地址映射。**

- 设计函数对进程的虚拟地址空间进行管理，借助以上内容具体实现heap扩展。

- 设计malloc函数和free函数对内存块进行管理。

**实验思路：**

1. 增加block数据结构
2. 对process增加空闲block队列和使用block队列
3. 对malloc和free进行系统调用完善
4. malloc：
   1. 判断size是否合法
   2. 查找是否有满足的空闲block
   3. 有的话分配一个
   4. 没有的话新分配一个页面
   5. 更新空闲block队列和used的block队列
5. free：
   1. 从used的block队列中找该位置对应的块
   2. 没有的话报错
   3. 有的话free掉，将其从used队列中删除
   4. 插入该块到free的block队列中，合并相邻的block

**实验栽坑：**

1. 内核态访问的都是物理地址，构造的块指针一定要指向物理地址
2. 不要在进程中对块初始化，因为在执行过程中会发生进程调度导致原本保存的块被损坏
3. 起始地址保存好，块的结构信息也要保存在页中
4. 申请的大小要对齐，与8对齐，否则会触发异常

## 实验代码

```
//added @lab2_challenge2,syscall.c
//allocate a block to user,size is n.  return virtual address
uint64 sys_user_allocate(long n)
{
  uint64 va=do_user_allocate(n);
  return va;
}
//free a block , virtual address start from va.
uint64 sys_user_free(uint64 va)
{
  do_user_free(va);
  return 0;
}
```

```
//added @lab2_challenge2,process.h
//function for allocate and free a block to user.
uint64 do_user_allocate(long n);
void do_user_free(uint64 va);
//added @lab2_challenge2
//block information
typedef struct block{
  uint64 start;//block's physical address
  uint64 size;//block's size
  uint64 va;//block's virtual address
  struct block *next;
}BLOCK;

//added @lab2_challenge2,process.c
//allocate a block to user
uint64 do_user_allocate(long n)
{
//check whether the size is legal
  n=ROUNDUP(n,8);
  if(n>PGSIZE) panic("better_allocate failed,because your size is too large!\n");
//check whether the process has satisfied blocks
  BLOCK *cur=current->free_start,*pre=current->free_start;
  while(cur){
    if(cur->size>=n) break;
    pre=cur;
    cur=cur->next;
  }
  //there is not a satisfied block,allocate a new page to get a new block
  if(cur==NULL){
    //allocate a new page
    void* pa = alloc_page();
    uint64 va = g_ufree_page;
    g_ufree_page += PGSIZE;
    user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,prot_to_type(PROT_WRITE | PROT_READ, 1));
    //turn this new page to a block
    BLOCK* temp=(BLOCK*)pa;
    temp->start=(uint64)pa+sizeof(BLOCK);
    temp->size = PGSIZE-sizeof(BLOCK);
    temp->va=va+sizeof(BLOCK);
    temp->next=NULL;
    //insert this block to line
    if(pre==NULL){
      current->free_start=temp;
      cur=temp;
    }
    else{
      if(pre->va+pre->size>=((pre->va)/PGSIZE+PGSIZE)){
        pre->size+=PGSIZE;
        cur=pre;
      }
      else{
        pre->next=temp;
        cur=temp;
      } 
    }
    
  }
  //util this step,cur point to a satisfied block
  
//allocate a block and update free line
  if(n+sizeof(BLOCK)<cur->size){
    BLOCK *to_use=(BLOCK*)(cur->start+n);
    to_use->start=cur->start+n+sizeof(BLOCK);
    to_use->size=cur->size-n-sizeof(BLOCK);
    to_use->va=cur->va+n+sizeof(BLOCK);
    to_use->next=cur->next;
    if(cur==current->free_start)
      current->free_start=to_use;
    else
      pre->next=to_use;
  }
  else{
    if(cur==current->free_start)
      current->free_start=cur->next;
    else
      pre->next=cur->next;
  }

//update used line
  cur->size=n;
  cur->next=current->used_start;
  current->used_start=cur;
  return cur->va;
}

//free a block
void do_user_free(uint64 va)
{
//find va's block
  BLOCK *cur=current->used_start,*pre=current->free_start;
  while(cur){
    if(cur->va<=va&&cur->va+cur->size>va) break;
    pre=cur;
    cur=cur->next;
  }
  if(cur==NULL) panic("free failed,because the address you want to free is illegal!\n");
//free,update used line
  if(cur==current->used_start)
    current->used_start=cur->next;
  else
    pre->next=cur->next;
//update free line
  //find the corrent position to insert
  BLOCK *free_cur=current->free_start,*free_pre=current->used_start;
  while(free_cur){
    if(free_cur->va<=cur->va);
    else break;
    free_pre=free_cur;
    free_cur=free_cur->next;
  }
  //insert this block into the last
  if(free_cur==NULL){
    if(free_pre==NULL)
    {
      cur->next=current->free_start;
      current->free_start=cur;
    }
    else{
      //merge too blocks
      if(free_pre->va+free_pre->size>=cur->va){
        free_pre->size+=cur->size;
      }
      else free_pre->next=cur,cur->next=NULL;//don't merge
    }
  }
  else{
    //insert the block into the first
    if(free_cur==current->free_start){
      if(cur->va+cur->size>=free_cur->va)//merge this and the next
      {
        cur->size+=free_cur->size;
        cur->next=free_cur->next;
        current->free_start=cur;
      }
      else{
        cur->next=current->free_start;
        current->free_start=cur;
      }
    }
    else{//insert the block into the middle
      if(free_pre->va+free_pre->size>=cur->va&&cur->va+cur->size>=free_cur->va)//merge three blocks
      {
        free_pre->size+=cur->size+free_cur->size;
        free_pre->next=free_cur->next;
      }
      else if(cur->va+cur->size>=free_cur->va)//merge this and the next
      {
        cur->size+=free_cur->size;
        cur->next=free_cur->next;
        free_pre->next=cur;
      }
      else if(free_pre->va+free_pre->size>=cur->va)//merge this and pre
      {
        free_pre->size+=cur->size;
      }
      else//don't merge
      {
        free_pre->next=cur;
        cur->next=free_cur;
      }
    }
  }
  //cur->next=current->free_start;
  //current->free_start=cur;
  
  
}
```



## lab3_challenge2实现信号量

**实验目标：**

​	通过控制信号量的增减，控制主进程和两个子进程的输出按主进程，第一个子进程，第二个子进程，主进程，第一个子进程，第二个子进程……这样的顺序轮流输出，使得输出达到预期。

**实验指导：**

* **添加系统调用，使得用户对信号量的操作可以在内核态处理**
* 在内核中**实现信号量的分配、释放和PV操作，当P操作处于等待状态时能够触发进程调度**

**实验思路：**

1. 实现三个函数：sem_num,sem_P,sem_V，三个函数思路如下：

   - sem_num:参数为信号灯的初始值，返回信号灯的信号值，该信号值由操作系统为其分配并初始化。
   - sem_P:参数为信号灯的信号值； 对该信号进行P操作，即对该信号的值-1，如果减完之后小于0，将该进程置于该信号的等待队列中，并调度一个新的进程；否则继续执行。
   - sem_V:参数为信号灯的信号值； 对该信号进行V操作，即对该信号的值+1，如果加了之后大于0，不管；否则，就调度该信号的等待队列中的一个进程将其加入就绪队列。

   在实现这些函数的过程中，由于需要内核态来完成一些功能，因此在此处实现的时候直接采用系统调用来实现。

2. 系统调用实现上述三个函数调用的部分，即完善系统调用

   首先需要对每一个功能定义一个系统调用号，从而可以找到服务例程。

   在服务例程中，需要用到进程的相关信息，因此可以将详细实现放在process中，这里服务例程的实现直接调用process将封装好的服务程序。

3. 具体实现功能：

   - 定义信号灯的数据结构：信号值和等待进程队列
   - 具体实现三个函数，达到1中所想要的功能，此处介绍一下几个函数的算法思路。

4. 从app程序->系统功能实现一步步的进行逻辑推理，从系统功能实现->app程序进行一步步程序实现（实现->封装->调用）。

## 实验代码

```
//user_lib.h
int sem_new(int resource);
void sem_P(int mutex);
void sem_V(int mutex);
//added @lab3_challenge2
int sem_new(int resource){
  //printu("sem_new syscall\n");
  return do_user_call(SYS_user_sem_new,resource,0,0,0,0,0,0);
}

//user_lib.c
void sem_P(int mutex){
  //printu("sem_p syscall\n");
  do_user_call(SYS_user_sem_P,mutex,0,0,0,0,0,0);
}

void sem_V(int mutex){
  //printu("sem_v syscall\n");
  do_user_call(SYS_user_sem_V,mutex,0,0,0,0,0,0);
}
```

```
//process.h
//added @lab3_challenge2
#define SEM_MAX 32
//struct definition
typedef struct semphore{
  int signal;
  process *waiting_queue;
}semphore;
//PV operation function
long do_sem_new(int resource);
void do_sem_P(int mutex);
void do_sem_V(int mutex);
void insert_to_waiting_queue(int mutex);



//process.c

int cur=0;
semphore signal[SEM_MAX];
//PV operation function
long do_sem_new(int resource)
{
  if(cur<SEM_MAX)
  {
    signal[cur].signal = resource;
    return cur++;
  }
  panic("Too many signals!You should notice your process.\n");
  return -1;
}

//P operation
void do_sem_P(int mutex){
  if(mutex<0||mutex>=SEM_MAX)
  {
    panic("Your signal is error!\n");
    return;
  }
  signal[mutex].signal--;
  if(signal[mutex].signal<0){
    //insert current process to this signal's waiting list
    insert_to_waiting_queue(mutex);
    //schedule a ready process
    schedule();
  }
  
}

//V operation
void do_sem_V(int mutex){
  if(mutex<0||mutex>=SEM_MAX)
  {
    panic("Your signal is error!\n");
    return;
  }
  signal[mutex].signal++;
  if(signal[mutex].signal<=0)
  {
    if(signal[mutex].waiting_queue!=NULL){
      process* cur=signal[mutex].waiting_queue;
      signal[mutex].waiting_queue=signal[mutex].waiting_queue->queue_next;
      cur->status = READY;
      insert_to_ready_queue(cur);
    }
  }
  
}

//P operation's insert
void insert_to_waiting_queue(int mutex){
  if(signal[mutex].waiting_queue==NULL)
  {
    signal[mutex].waiting_queue = current;
  }
  else{
    process *cur=signal[mutex].waiting_queue;
    for(;cur->queue_next!=NULL;cur=cur->queue_next)
    {
      if(cur==current) return;
    }
    if(cur==current) return;
    cur->queue_next=current;
  }
  
  current->queue_next=NULL;
  current->status=BLOCKED;
}

```

