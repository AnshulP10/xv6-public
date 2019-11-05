#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main (int argc,char *argv[])
{

    int pid; 
    pid = fork();
    int status = 0;
    int x;
    int y;
    if (pid!= 0)
    {   
        status=waitx(&x,&y);
        printf(1, "Wait Time = %d\n Run Time = %d\n Status: %d \n", x, y, status); 

    }   
    else
    {   
      //exec(argv[1], argv);
      //printf(1,"%s failed fork\n", argv[1]);
      for(volatile int k = 0; k<100000000; k++)
        k = k + 1 - 2 + 2; 
    }   
    exit();
}
