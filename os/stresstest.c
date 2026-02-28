#include "kernel/types.h"
#include "user/user.h"

#define LARGE_TIME 1222122550
#define SMALL_TIME 122212325

uint64 useless_calc(uint64 z, uint64 time)
{
  volatile uint64 t = z;  // volatile to not be optimized out
  volatile uint64 sum = 0;
  for (;;) {
    t += 2;
    sum += t;
    t -= 1;
    if (t == time)
      break;
  }
  return sum;
}

int
useless(uint64 time)
{
  uint64 sum = useless_calc(0, time);
  char* spent = (time == LARGE_TIME) ? "large" : "small";
  // Need to print uselesss sum to suppress warning.
  printf("Child pid %d, %s, finished. Useless sum: %d\n", getpid(), spent,  sum);

  exit(0);
}

 int main(int argc, char *argv[])
{
  int id = 0;

  // Create 4 long running children
  for (int i=0; i < 4; i++)
  {
    id = fork();
    if(id < 0)
      printf("%d failed in fork!\n", getpid());
    else if(id == 0) {
      // Child
      useless(LARGE_TIME);
    }
  }

  // Create 40 small processes with varying priorities
  int n = 40;
  for (int i=0; i<n; i++) {
    id = fork();
    if(id < 0)
      printf("%d failed in fork!\n", getpid());
    else if(id == 0) {
      // Child
      useless(SMALL_TIME);
    }
  }

  // Parent waits for all the children (n+4)
  if (id > 0) {
    for (int i = 0; i < n+4; i++) {
      wait((int*)0);
    }
  }
  exit(0);
}
