// Created on Sunday, November 17, 2024 https://www.iwriteiam.nl/D2411.html#17

// Type following defintions and defines that can be application specific,
// for example, defined by enumaration types.

typedef uint32_t TaskId;
#define NR_TASKS 100
// Task 0 is reserved for Queue 0

typedef uint32_t TimerId;
#define NR_TIMERS 100

typedef uint32_t QueueId;
#define NR_QUEUES 10
// Qeuee 0 is reserved for the main queue

typedef uint32_t CriticalSectionId;
#define NR_CRITICAL_SECTIONS 20

typedef uint32_t TimeTick;
TimeTick timeTick
#define MAX_TIME_TICK 1000
#define INCREMENT_TIME_TICK timeTick = 1 + (timeTick % MAX_TIME_TICK); 
#define TIMER_DONE(X) ((X) == timeTick)
#define TIMER_ON(T) (1 + (timeTick + (T) - 1) % MAX_TIME_TICK)
#define TIMER_OFF 0


typedef struct
{
	void (*function)();
	TaskId next_task;
} Task;

Task tasks[NR_TASKS];


typedef struct
{
	timeTick time;
	TaskId task;
} Timer;

Timer timers[NR_TIMERS];


typedef struct
{
	TaskId first;
	TaskId last;
} Queue;
#define MAIN_RUN_QUEUE 0

Queue queues[NR_QUEUES];

void QueueInit(QueueId queue_id, TaskId task_id)
{
	queues[queue_id].first = task_id;
	queues[queue_id].last = task_id;
	tasks[task_id].next_task = 0;
}

void QueueAdd(QueueId queue_id, TaskId task_id)
{
	tasks[queues[queue_id].last] = task_id;
	queues[queue_id].last = task_id;
	tasks[task_id].next_task = 0; 
}

bool QueueEmpty(QueueId queue_id)
{
	return queues[queue_id].first == queues[queue_id].last;
}

TaskId QueuePop(QueueId queue_id)
{
	TaskId task_id = tasks[queues[queue_id].first].next;
	if (task_id != 0)
	{
		queues[queue_id].first = tasks[task_id].next_id;
		if (queues[queue_id].first == 0)
			queues[queue_id].last = queues[queue_id].first;
	}
	return task_id;
}


typedef struct
{
	Queue queue;
	TaskId claimed_by;
} CriticalSection;

CriticalSection criticalSections[NR_CRITICAL_SECTIONS];

void CriticalSectionInit(CriticalSectionId critical_section_id, QueueId queue_id)
{
	criticalSections[critical_section_id].queue = queue_id;
	criticalSections[critical_section_id].claimed_by = 0;
}

bool CriticalSectionEnter(CriticalSectionId critical_section_id, TaskId task_id)
{
	if (   criticalSections[critical_section_id].claimed_by != 0
		&& criticalSections[critical_section_id].claimed_by != task_id)
	{
		QueueAdd(criticalSections[critical_section_id].queue, task_id);
		return false;
	}
	criticalSections[critical_section_id].claimed_by = task_id;
	return true;
}
// Caller needs to exit the task when this function returns false

void CriticalSectionLeave(CriticalSectionId critical_section_id)
{
	TaskId next_task_id = QueuePop(criticalSections[critical_section_id].queue);
	criticalSections[critical_section_id].claimed_by = next_task_id;
	if (next_task_id != 0)
		QueueAdd(MAIN_RUN_QUEUE, nex_task_id);
}

void runTimerTask(void)
{
	for (int i = 0; i < NR_TIMERS; i++)
		if (TIMER_DONE(timers[i].time))
			QueueAdd(MAIN_RUN_QUEUE, timers[i].task);
	QueueAdd(MAIN_RUN_QUEUE, tasks[TIMER_TASK]);
}

void runMainQueue(void)
{
	for (;;)
	{
		task_id = QueuePop(MAIN_RUN_QUEUE);
		if (task_id == 0)
			break;
		
		tasks[task_id].function();
	}
}




