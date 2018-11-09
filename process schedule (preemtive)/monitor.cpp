#include<stdio.h>
#include<stdlib.h>
#include<signal.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/times.h>
#include<vector>

using namespace std;

pid_t childPID=0;
tms startTime;
tms stopTime;
long clk_tck=sysconf(_SC_CLK_TCK);
clock_t startClock;
clock_t stopClock;


void signal_relay(int sigNum)
{
	if(childPID)
	{
		//printf("relay signal:%d PID:%d\n",sigNum,childPID);
		if(kill(childPID,sigNum)==-1)
		{
			printf("send signal error!\n");
		}

	}
}

int main(int argc,char *argv[])
{
	if(argc!=2)
	{
		return 0;
	}

	//tokenize command string
	char *str;
	int i=1;
	str=strtok(argv[1]," ");
	vector<char *> tokens;
	while(str)
	{
		tokens.push_back(str);
		str=strtok(NULL," ");
	}
	tokens.push_back(NULL);
	
	signal(SIGTERM,signal_relay);
	signal(SIGCONT,signal_relay);
	signal(SIGTSTP,signal_relay);
	childPID=fork();
	startClock=times(&startTime);
	if(childPID==0)//child process
	{
		if(execvp(tokens[0],&tokens[0]))
		{
			printf("exec error!errno:%d\n",errno);
		}
		return 1;
	}
	int childStatus=0;
	int exitPID;
	do{
		exitPID=waitpid(childPID,&childStatus,WNOHANG|WCONTINUED|WUNTRACED);		
	}while(!exitPID||(!WIFEXITED(childStatus)&&!WIFSIGNALED(childStatus)/*&&!WIFSTOPPED(childStatus)*/));
	stopClock=times(&stopTime);
	printf("Process %d :\ttime elapsed: %.3f\n",childPID,(stopClock-startClock)/float(clk_tck));
	printf("\t\tuser time   : %.3f\n",(stopTime.tms_cutime-startTime.tms_cutime)/float(clk_tck));
	printf("\t\tsystem time : %.3f\n",(stopTime.tms_cstime-startTime.tms_cstime)/float(clk_tck));
	return 0;
}
