/* 
 * tsh - A tiny shell program with job control
 * 
 * 郭子阳 1170300520
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size 命令行的最大字符数 */
#define MAXARGS     128   /* max args on a command line 参数的最大值 */
#define MAXJOBS      16   /* max jobs at any point in time 同一时刻的最大作业数 */
#define MAXJID    1<<16   /* max job ID 作业id的最大值 */

/* Job states */
#define UNDEF 0 /* undefined 未定义 */
#define FG 1    /* running in foreground 前台运行（唯一） */
#define BG 2    /* running in background 后台运行 */
#define ST 3    /* stopped 调度停止 */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z 前台的程序停止
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state. 每个时刻前台最多运行一个作业
 */

/* Global variables */
extern char **environ;      /* defined in libc 系统环境变量 */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) 控制台前缀 */
int verbose = 0;            /* if true, print additional output 输出调试信息 */
int nextjid = 1;            /* next job ID to allocate 下一个作业ID */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct 作业的结构 */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line 作业的命令 */
};
struct job_t jobs[MAXJOBS]; /* The job list 存储作业的数组 */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2); //将所有输出链接到stdout

    /* Parse the command line 解析命令行字符串（开启shell的命令行） */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        	case 'h':             /* print help message */
            	usage();
		    	break;
        	case 'v':             /* emit additional diagnostic info */
            	verbose = 1;
	    		break;
        	case 'p':             /* don't print a prompt */
            	emit_prompt = 0;  /* handy for automatic testing */
	    		break;
			default:
            	usage();
		}
    }

    /* Install the signal handlers */
	//注册信号处理函数
    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

		/* Read command line */
		if (emit_prompt) {
	    	printf("%s", prompt);
	    	fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))	//读取并判断错误
	    	app_error("fgets error");
		if (feof(stdin)) { /* End of file (ctrl-d) */
	    	fflush(stdout);
		    exit(0);
		}

		/* Evaluate the command line */
		eval(cmdline);	//解析并运行命令行
		fflush(stdout);
		fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 * 解析并执行用户输入的命令行字符串
 * 如果命令为内置命令（quit，jobs，bg，fg），直接运行
 * 否则，fork一个子进程，并且在子进程中执行命令
 * 如果运行在前台，shell等待子进程停止返回
 * 每个子进程必须有一个唯一的PGID
*/
void eval(char *cmdline) 
{
    /* $begin handout */
    char *argv[MAXARGS]; /* argv for execve() */	//执行时候的参数
    int bg;              /* should the job run in bg or fg? */	//前后台运行
    pid_t pid;           /* process id */	//进程id ，pid
    sigset_t mask;       /* signal mask */	//信号掩码

    /* Parse command line */
    bg = parseline(cmdline, argv); 	//解析串，并分割存储在argv，返回前后台运行的参数
    if (argv[0] == NULL)  //空串
		return;   /* ignore empty lines */

    if (!builtin_cmd(argv)) { 
	   /* 
		* This is a little tricky. Block SIGCHLD, SIGINT, and SIGTSTP
		* signals until we can add the job to the job list. This
		* eliminates some nasty races between adding a job to the job
		* list and the arrival of SIGCHLD, SIGINT, and SIGTSTP signals.
		* 阻塞SIGCHLD、SIGINT、SIGTSTP信号
		*/

		if (sigemptyset(&mask) < 0)
			unix_error("sigemptyset error");
		if (sigaddset(&mask, SIGCHLD)) 
			unix_error("sigaddset error");
		if (sigaddset(&mask, SIGINT)) 
			unix_error("sigaddset error");
		if (sigaddset(&mask, SIGTSTP)) 
			unix_error("sigaddset error");
		if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
			unix_error("sigprocmask error");

		/* Create a child process */	//fork当前进程
		if ((pid = fork()) < 0)
			unix_error("fork error");
		
	   /* 
	    * Child  process 
		* 子进程运行以下代码
		*/

		if (pid == 0) {
			/* Child unblocks signals 子进程解除对信号的阻塞 */
			sigprocmask(SIG_UNBLOCK, &mask, NULL);

			/* Each new job must get a new process group ID 
			so that the kernel doesn't send ctrl-c and ctrl-z
			signals to all of the shell's jobs */
			//每个作业以自己的pid为pgid
			if (setpgid(0, 0) < 0) 
				unix_error("setpgid error"); 

			/* Now load and run the program in the new job */
			//在子进程中运行程序
			if (execve(argv[0], argv, environ) < 0) {
				printf("%s: Command not found\n", argv[0]);
				exit(0);
			}
		}

		/* 
		* Parent process
		*/

		/* Parent adds the job, and then unblocks signals so that
		the signals handlers can run again */
		//子进程开始运行的同时，父进程执行以下：在作业数组中添加作业，停止阻塞信号
		addjob(jobs, pid, (bg == 1 ? BG : FG), cmdline);
		sigprocmask(SIG_UNBLOCK, &mask, NULL);

		if (!bg) 
			waitfg(pid);
		else
			//后台执行的就输出一下命令后即可，不等待
			printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
    }
    /* $end handout */
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 * 解析命令行参数，分割并存在数组里
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line 命令行字符串副本 */
    char *buf = array;          /* ptr that traverses command line 移动的字符指针 */
    char *delim;                /* points to first space delimiter 指向第一个空格 */
    int argc;                   /* number of args 参数个数 */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space 用空格替代最后一个回车符号 */
    while (*buf && (*buf == ' ')) /* ignore leading spaces 忽略前缀空格 */
		buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
    }
    else {
		delim = strchr(buf, ' ');
    }

    while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

		if (*buf == '\'') {
	    	buf++;
	    	delim = strchr(buf, '\'');
		}
		else {
	    	delim = strchr(buf, ' ');
		}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
		return 1;

    /* should the job run in the background? 最后加&号则在后台运行 */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
		argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.
 * 直接执行内置命令：quit、jobs、bg、fg
 */
int builtin_cmd(char **argv)
{
	if(!strcmp(argv[0], "quit")) {
		//结束作业列表中的所有job
		int i;
		pid_t pid;
		for(i = 0; i < MAXJOBS; i ++) {
			if(jobs[i].pid != 0) {
				if((pid = kill(jobs[i].pid, SIGKILL)) < 0) {
					printf("fail to kill job %d", pid);
				}
			}
		}
		exit(0);
		return 1;	//never reach
	} else if(!strcmp(argv[0], "jobs")) {
		listjobs(jobs);
		return 1;
	} else if(!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
		do_bgfg(argv);
		return 1;
	}
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    /* $begin handout */
    struct job_t *jobp=NULL;
    
    /* Ignore command if no argument 忽略无参命令 */
    if (argv[1] == NULL) {
		printf("%s command requires PID or %%jobid argument\n", argv[0]);
		return;
    }
    
    /* Parse the required PID or %JID arg 解析得到pid和jid */
    if (isdigit(argv[1][0])) {
		pid_t pid = atoi(argv[1]);
		if (!(jobp = getjobpid(jobs, pid))) {
	    	printf("(%d): No such process\n", pid);
	    	return;
		}
    }
    else if (argv[1][0] == '%') {
		int jid = atoi(&argv[1][1]);
		if (!(jobp = getjobjid(jobs, jid))) {
	    	printf("%s: No such job\n", argv[1]);
	    	return;
		}
    }	    
    else {
		printf("%s: argument must be a PID or %%jobid\n", argv[0]);
		return;
    }

    /* bg command */
    if (!strcmp(argv[0], "bg")) { 
		if (kill(-(jobp->pid), SIGCONT) < 0)
	    	unix_error("kill (bg) error");
		jobp->state = BG;
		printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
    }

    /* fg command */
    else if (!strcmp(argv[0], "fg")) { 
		if (kill(-(jobp->pid), SIGCONT) < 0)
	    	unix_error("kill (fg) error");
		jobp->state = FG;
		waitfg(jobp->pid);
    }
    else {
		printf("do_bgfg: Internal error\n");
		exit(0);
    }
    /* $end handout */
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 * 阻塞主进程shell直到无前台进程在运行
 * 此时，主进程不应当屏蔽任何信号
 */
void waitfg(pid_t pid)
{
	sigset_t mask;
	sigemptyset(&mask);
	//todo 此处不安全，使用fgpid之前应当阻塞所有信号，然而运行时不应当阻塞任何信号
	while(pid == fgpid(jobs)) {
		sigsuspend(&mask);
	}
    return;
}

/*****************
 * Signal handlers
 * 信号处理函数
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 * 处理SIGCHLD的函数，子进程终止后向父进程发送此信号
 * 要求shell进程去主动回收所有僵尸进程，不停止等待其他进程
 */
void sigchld_handler(int sig) 
{
	int olderrno = errno;
	pid_t temp_pid;
	int status;
	sigset_t all_mask, pre_mask;
	struct job_t *temp_job;

	sigfillset(&all_mask);

	while((temp_pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
		sigprocmask(SIG_BLOCK, &all_mask, &pre_mask);
		temp_job = getjobpid(jobs, temp_pid);
		if(WIFSTOPPED(status)) {
			temp_job->state = ST;
			printf("Job [%d] (%d) stopped by signal %d\n", temp_job->jid, temp_job->pid, WSTOPSIG(status));
		} else {
			if(WIFSIGNALED(status)) {
				printf("Job [%d] (%d) terminated by signal %d\n", temp_job->jid, temp_job->pid, WTERMSIG(status));
			}
			deletejob(jobs, temp_pid);
		}
	}
	//if(errno == ECHILD)
	// 	app_error("waitpid error");

	errno = olderrno;
	fflush(stdout);
	sigprocmask(SIG_SETMASK, &pre_mask, NULL);
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 * 处理SIGINT的函数，输入ctrl+c时发出此信号，主进程捕获此信号并且向前台作业发送此信号。
 * ctrl+c由bash发送给tsh，控制tsh转发即可
 * 前台作业执行默认行为即可
 */
void sigint_handler(int sig) 
{
	int olderrno = errno;
	sigset_t all_mask, pre_mask;
	pid_t fg_pid;

	sigfillset(&all_mask);
	sigprocmask(SIG_BLOCK, &all_mask, &pre_mask);
	fg_pid = fgpid(jobs);
	sigprocmask(SIG_SETMASK, &pre_mask, NULL);
	if(fg_pid != 0) {
		if(kill(0 - fg_pid, SIGINT) < 0) {	//注意，应当关闭前台进程组！
			printf("failed to kill the process group %d", 0 - fg_pid);
		}
	}

	errno = olderrno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 * 处理SIGTSTP的函数，输入ctrl+z时发出此信号，主进程捕获此信号并向前台作业发送此信号
 * 默认行为挂起前台进程
 */
void sigtstp_handler(int sig) 
{
	int olderrno = errno;
	sigset_t all_mask, pre_mask;
	pid_t fg_pid;

	sigfillset(&all_mask);
	sigprocmask(SIG_BLOCK, &all_mask, &pre_mask);
	fg_pid = fgpid(jobs);
	sigprocmask(SIG_SETMASK, &pre_mask, NULL);
	if(fg_pid != 0) {
		if(kill(0 - fg_pid, SIGTSTP) < 0) {	//注意，应当停止前台进程组！
			printf("failed to stop the precess group %d", 0 - fg_pid);
		}
	}

	errno = olderrno;
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 * 作业列表的处理函数（查询）
 **********************************************/

/* clearjob - Clear the entries in a job struct 清空作业结构的所有信息 */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list 初始化作业列表（数组） */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
		clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID 返回最大的作业id */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid > max)
	    	max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list 向作业列表添加一个作业 */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
		return 0;

    for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
	    	jobs[i].pid = pid;
		    jobs[i].state = state;
		    jobs[i].jid = nextjid++;
	    	if (nextjid > MAXJOBS)
				nextjid = 1;
	    	strcpy(jobs[i].cmdline, cmdline);
  	    	if(verbose){
	        	printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
		}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list 删除作业列表中的一个特定的作业（僵尸作业） */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
		return 0;

    for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
	    	clearjob(&jobs[i]);
		nextjid = maxjid(jobs)+1;
	    return 1;
		}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job 返回当前前台程序的pid */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == FG)
	    	return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list 按照pid从作业列表中获得一个作业 */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
		return NULL;
    for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
	    	return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list 按照jid从作业列表中获得一个作业 */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
		return NULL;
    for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
	    	return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID 按照pid找到对应的作业的jid */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
		return 0;
    for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list 输出作业列表中的所有作业信息 */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid != 0) {
	    	printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
		    switch (jobs[i].state) {
				case BG: 
		    		printf("Running ");
		    		break;
				case FG: 
				    printf("Foreground ");
				    break;
				case ST: 
		    		printf("Stopped ");
		    		break;
	    		default:
		    		printf("listjobs: Internal error: job[%d].state=%d ", i, jobs[i].state);
	    	}
	    printf("%s", jobs[i].cmdline);
		}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 * 输出帮助信息
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 * 输出unix错误信息
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 * 输出程序错误信息
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 * 给对应的信号绑定处理函数
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 * 没使用过，SIGQUIT信号处理函数
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
