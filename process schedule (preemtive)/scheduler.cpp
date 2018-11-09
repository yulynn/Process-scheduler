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
#include<map>
#include<queue>
#include<iostream>
#include<cassert>
#include<algorithm>

#define INT_MAX 0x7fffffff

using namespace std;

long clk_tck=sysconf(_SC_CLK_TCK);
clock_t originTime;
int maxTimeSeconds;
vector<int> mixedJob;

struct ComingTask
{
	int jobID;
	int arrivalTime;
	char commandStr[256];
	int duration;
};

struct RunningTask
{
	int jobID;
	clock_t scheduleTime;
	clock_t sliceEndTime;
	clock_t endTime;
	char 	cmdStr[256];
	pid_t 	PID;
};

const static RunningTask idleTask{0,0,0,0};

enum JobEventType
{
	NONE,
	ARRIVAL,
	PAUSE,
	RUN,
};

struct JobEvent
{
	int time;//in second
	JobEventType event;
};

map<int,queue<JobEvent>> JobEvents;
queue<ComingTask> comingTasks;

void pushEvent(int jobID,JobEventType event)
{
	//const char *enumStr[]={"NONE","ARRIVAL","PAUSE","RUN"};
	//printf("Event{JobID:%d event:%s}\n",jobID,enumStr[event]);
	JobEvents[jobID].push({int((times(NULL)-originTime)/clk_tck+0.5),event});
}

class Scheduler
{
protected:
	RunningTask activeTask;
	pid_t createTask(RunningTask &task){
		assert(task.PID==0);
		task.PID=fork();
		if(task.PID==0)
		{
			if(execlp("./monitor","monitor",task.cmdStr,(char *)0)==-1)
			{
				printf("exec error!\n");
				exit(-1);
			}
		}
		pushEvent(task.jobID,RUN);
		task.scheduleTime=times(NULL);
		return task.PID;
	}
	void pauseTask(RunningTask &task){
		assert(task.PID!=0);
		kill(task.PID,SIGTSTP);
		pushEvent(task.jobID,PAUSE);
		task.endTime-=times(NULL)-task.scheduleTime;
	}
	void resumeTask(RunningTask &task){
		assert(activeTask.PID!=0);
		kill(task.PID,SIGCONT);
		pushEvent(task.jobID,RUN);
		task.scheduleTime=times(NULL);
	}
	void terminateTask(RunningTask &task){
		assert(task.PID!=0);
		kill(task.PID,SIGTERM);
		int status=0;
		if(task.PID==waitpid(task.PID,&status,0)&&(WIFEXITED(status)||WIFSTOPPED(status)))	//complete
		{
			pushEvent(task.jobID,NONE);
		}
	}
	bool taskExit(RunningTask task)
	{
		int status=0;
		return task.PID==waitpid(task.PID,&status,WNOHANG)&&WIFEXITED(status);
	}
	bool taskTimeUp(RunningTask task)
	{
		return times(NULL)>activeTask.sliceEndTime;
	}
	virtual void taskArrival(ComingTask cTask)=0;
	virtual RunningTask getNextScheduleTask()=0;
public:
	Scheduler(){
		activeTask=idleTask;
	}
	virtual ~Scheduler(){};
	virtual void run()=0;
};

class Scheduler_FIFO:public Scheduler
{
private:
	queue<RunningTask> tasks;
	virtual void taskArrival(ComingTask aTask){
		RunningTask task=idleTask;
		task.jobID=aTask.jobID;
		strcpy(task.cmdStr,aTask.commandStr);
		task.PID=0;
		task.endTime=aTask.duration<0?INT_MAX:aTask.duration*clk_tck;
		tasks.push(task);
		pushEvent(task.jobID,ARRIVAL);
	}
	virtual RunningTask getNextScheduleTask(){
		auto t=tasks.front();
		tasks.pop();
		return t;
	}	
public:
	Scheduler_FIFO(){
	}
	~Scheduler_FIFO(){}
	virtual void run(){
		while(activeTask.PID||tasks.size()||comingTasks.size())
		{
			//add new task to queue
			while(!comingTasks.empty()&&comingTasks.front().arrivalTime*clk_tck<times(NULL)-originTime)
			{
				taskArrival(comingTasks.front());
				comingTasks.pop();
			}
			if(activeTask.PID)
			{
				//test if current task exit
				if(taskExit(activeTask))
				{
					pushEvent(activeTask.jobID,NONE);
					activeTask=idleTask;
				}
				else if(taskTimeUp(activeTask))	//if time up
				{
					terminateTask(activeTask);
					activeTask=idleTask;
				}
				else
				{
					continue;
				}
			}
			assert(!activeTask.PID&&!activeTask.jobID);
			if(tasks.size())
			{
				activeTask=getNextScheduleTask();
				assert(!activeTask.PID);
				//update sliceEndTime and endTime
				activeTask.sliceEndTime=activeTask.endTime==INT_MAX?INT_MAX:times(NULL)+activeTask.endTime;
				activeTask.PID=createTask(activeTask);
				printf("%d %d %d\n",activeTask.endTime,activeTask.sliceEndTime,activeTask.scheduleTime);
			}
		}
	}
};

class Scheduler_RR:public Scheduler
{
private:
	queue<RunningTask> tasks;
	virtual void taskArrival(ComingTask aTask){
		RunningTask task=idleTask;
		task.jobID=aTask.jobID;
		strcpy(task.cmdStr,aTask.commandStr);
		task.PID=0;
		task.endTime=aTask.duration<0?INT_MAX:aTask.duration*clk_tck;
		tasks.push(task);
		pushEvent(task.jobID,ARRIVAL);
	}
	virtual RunningTask getNextScheduleTask(){
		auto t=tasks.front();
		tasks.pop();
		return t;
	}	
public:
	Scheduler_RR(){
	}
	~Scheduler_RR(){}
	virtual void run(){
		while(activeTask.PID||tasks.size()||comingTasks.size())
		{
			//add new task to queue
			while(!comingTasks.empty()&&comingTasks.front().arrivalTime*clk_tck<times(NULL)-originTime)
			{
				taskArrival(comingTasks.front());
				comingTasks.pop();
			}
			if(activeTask.PID)
			{
				//test if current task exit
				if(taskExit(activeTask))
				{
					pushEvent(activeTask.jobID,NONE);
					activeTask=idleTask;
				}
				else if(taskTimeUp(activeTask))	//if time up
				{
					if(activeTask.endTime<=0){
						terminateTask(activeTask);
						activeTask=idleTask;
					}
					else
					{
						pauseTask(activeTask);
						tasks.push(activeTask);
						activeTask=idleTask;
					}
				}
				else
				{
					continue;
				}
			}
			assert(!activeTask.PID&&!activeTask.jobID);
			if(tasks.size())
			{
				activeTask=getNextScheduleTask();
				if(!activeTask.PID)
				{
					activeTask.sliceEndTime=activeTask.endTime==INT_MAX?times(NULL)+2*clk_tck:times(NULL)+min(activeTask.endTime,2*clk_tck);
					activeTask.PID=createTask(activeTask);
				}
				else
				{
					activeTask.sliceEndTime=activeTask.endTime==INT_MAX?times(NULL)+2*clk_tck:times(NULL)+min(activeTask.endTime,2*clk_tck);
					resumeTask(activeTask);
				}
			}
		}
	}
};

class Scheduler_SJF:public Scheduler
{
private:
	vector<RunningTask> tasks;
	virtual void taskArrival(ComingTask aTask){
		RunningTask task=idleTask;
		task.jobID=aTask.jobID;
		strcpy(task.cmdStr,aTask.commandStr);
		task.PID=0;
		task.endTime=aTask.duration<0?INT_MAX:aTask.duration*clk_tck;
		tasks.push_back(task);
		pushEvent(task.jobID,ARRIVAL);
	}
	virtual RunningTask getNextScheduleTask(){
		sort(tasks.begin(),tasks.end(),[](const RunningTask &lhs,const RunningTask &rhs){
			return lhs.endTime<rhs.endTime;
		});
		auto t=tasks[0];
		tasks.erase(tasks.begin());
		return t;
	}	
public:
	Scheduler_SJF(){
	}
	~Scheduler_SJF(){}
	virtual void run(){
		while(activeTask.PID||tasks.size()||comingTasks.size())
		{
			//add new task to queue
			bool needReschedule=false;
			while(!comingTasks.empty()&&comingTasks.front().arrivalTime*clk_tck<times(NULL)-originTime)
			{
				taskArrival(comingTasks.front());
				comingTasks.pop();
				needReschedule=true;
			}
			if(activeTask.PID)
			{
				//test if current task exit
				if(taskExit(activeTask))
				{
					pushEvent(activeTask.jobID,NONE);
					activeTask=idleTask;
				}
				else if(taskTimeUp(activeTask))	//if time up
				{
					terminateTask(activeTask);
					activeTask=idleTask;
				}
				else if(needReschedule)
				{
					pauseTask(activeTask);
					tasks.push_back(activeTask);
					activeTask=idleTask;
				}
				else
				{
					continue;
				}
			}
			assert(!activeTask.PID&&!activeTask.jobID);
			if(tasks.size())
			{
				activeTask=getNextScheduleTask();
				if(!activeTask.PID)
				{
					activeTask.sliceEndTime=activeTask.endTime==INT_MAX?INT_MAX:times(NULL)+activeTask.endTime;
					activeTask.PID=createTask(activeTask);
				}
				else
				{
					activeTask.sliceEndTime=activeTask.endTime==INT_MAX?INT_MAX:times(NULL)+activeTask.endTime;
					resumeTask(activeTask);
				}
			}
		}
	}
};

void printJobReport(int jobID,queue<JobEvent> events)
{
	int timeStamp=0;
	int state=NONE;
	printf("---------+-");
	for(int i=0;i<maxTimeSeconds;++i)
	{
		cout<<"--";
	}
	cout<<endl;
	printf("   Job %d | ",jobID);
	for(int i=0;i<maxTimeSeconds;++i)
	{
		while(!events.empty()&&events.front().time==i)
		{
			state=events.front().event;
			events.pop();
		}
		const char *symbol[]={" ",".",".","#"};
		cout<<symbol[state]<<" ";
		if(state==RUN)
		{
			mixedJob[i]=jobID;
		}
	}
	cout<<endl;
}

void printReport()
{
	maxTimeSeconds=int((times(NULL)-originTime)/clk_tck+0.5);
	mixedJob=vector<int>(maxTimeSeconds,0);
	printf("Gantt Chart\n");
	printf("===========\n");
	printf("   Time  | ");
	for(int i=0;i<=maxTimeSeconds/10;++i)
	{
		cout<<i<<"                   ";
	}
	cout<<endl;
	printf("         | ");
	for(int i=0;i<maxTimeSeconds;++i)
	{
		cout<<i%10<<" ";
	}
	cout<<endl;
	for(auto &k:JobEvents)
	{
		printJobReport(k.first,k.second);
	}

	int timeStamp=0;
	int activeJOB=0;
	printf("---------+-");
	for(int i=0;i<maxTimeSeconds;++i)
	{
		cout<<"--";
	}
	cout<<endl;
	printf("  Mixed  | ");
	for(auto id:mixedJob)
	{
		if(id)
			cout<<id<<" ";
		else
			cout<<"  ";
	}
	cout<<endl;
	
}

int main(int argc,char *argv[])
{
	if(argc!=3)
	{
		return 0;
	}

	Scheduler *scheduler=NULL;

	if(!strcmp("FIFO",argv[2])) scheduler=new Scheduler_FIFO();
	else if(!strcmp("RR",argv[2])) scheduler=new Scheduler_RR();
	else if(!strcmp("SJF",argv[2])) scheduler=new Scheduler_SJF();
	else exit(-1);

	FILE *fp=fopen(argv[1],"r");
	if(!fp)
	{
		printf("file %s open error!\n",argv[1]);
		exit(-1);
	}
	char line[1024];
	int jobId=1;
	while(fgets(line,1024,fp))
	{
		ComingTask task;
		task.jobID=jobId++;
		task.arrivalTime=atoi(strtok(line,"\t"));
		strcpy(task.commandStr,strtok(NULL,"\t"));
		task.duration=atoi(strtok(NULL,"\t"));
		comingTasks.push(task);
	}

	originTime=times(NULL);
	scheduler->run();
	delete scheduler;

	printReport();
}
