#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main (int argc,char *argv[])
{

//for fcfs, pbs
  int pid; 
  for(int i = 0; i<10; i++){
    pid = fork();
    //int Pid = 0;
    //int x;
    //int y;
    if (pid> 0)
    {    
      //Pid=waitx(&x,&y);
      //printf(1, "Wait Time = %d\n Run Time = %d\n PID: %d \n", x, y, Pid); 
      wait();
    }   
    else
    {   
        //exec(argv[1], argv);
        //printf(1,"%s failed fork\n", argv[1]);
      volatile float kink=0;
      for(volatile int k = 0; k<100000000; k++)
        kink=kink+2.0-2.0+4.0-4.0+1.0-1.0;
        //printf(1,"%d ", k);
        //printf(1,"\n");
      exit();
    }   
  }
  exit();
}

