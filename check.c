#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

struct procstat{
  int pid;
  int runtime;
  int num_run;
  int current_queue;
  int ticks[5];
};

int main (int argc,char *argv[])
{

    int pid; 
    pid = fork();
//    int status = 0;
//    int x;
//    int y;
    if (pid!= 0)
    {   
        struct procstat curproc;//=(struct proc_stat*)(malloc(sizeof(struct proc_stat*)*1));
        getpinfo(&curproc);
        // val = val;
        printf(1,"PID:%d\n",curproc.pid);
        printf(1,"Run Time%d\n",curproc.runtime);
        printf(1,"Num Run%d\n",curproc.num_run);
        printf(1,"Current Queue%d\n",curproc.current_queue);
        for(int i=0;i<5;++i)    printf(1,"Ticks %d in queue %d\n",curproc.ticks[i],i);
//        status=waitx(&x,&y);
//        printf(1, "Wait Time = %d\n Run Time = %d\n Status: %d \n", x, y, status); 

    }   
    else
    {   
      for(volatile int k = 0; k<100000000; k++)
        k = k + 3 - 2 + 2 + 3; 
    }   
    exit();
}