#include<stdio.h>
#include<stdlib.h>
#include<signal.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/times.h>

clock_t originTime;int main(void){
	long clk_tck=sysconf(_SC_CLK_TCK);
	clock_t origin=times(NULL);
	while(1)
	{
		printf("PID:%d\ttime:%ld\n",getpid(),(times(NULL)-origin)/clk_tck);
		sleep(1);
	}
	return 0;
}
