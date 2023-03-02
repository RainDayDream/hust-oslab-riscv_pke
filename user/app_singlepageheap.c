 /*
 * Below is the given application for lab2_challenge2_singlepageheap.
 * This app performs malloc memory.
 */

#include "user_lib.h"
#include "util/types.h"
#include "util/string.h"
int main(void) {
  
  char str[20] = "cross page";
  char *m = (char *)better_malloc(100);
  char *p = (char *)better_malloc(4096);  // cross page
  if((uint64)p - (uint64)m > 512 ){
    printu("you need to manage the vm space precisely!");
    exit(-1);
  }
  better_free((void *)m);

  strcpy(p,str);
  printu("%s\n",p);
  exit(0);
  return 0;
}