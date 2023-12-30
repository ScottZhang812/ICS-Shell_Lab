/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and ID here>
 * Name: Zhang Xuteng
 * ID: 521260910019
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
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */    //in this lab it's special, actually just the sole process struct
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
// int getRelBgJid(pid_t pid);
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
void Addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
void Deletejob(struct job_t *jobs, pid_t pid);

void Dup2(int FD1, int FD2)
{
    if (dup2(FD1, FD2) < 0) unix_error("dup2_error");
}
void Kill(pid_t PID, int SIG)
{
    if (kill(PID, SIG) < 0) unix_error("kill_error");
}
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
    Dup2(1, 2);

    /* Parse the command line */
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
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}
    //printf("MAIN noticed: %s\n", cmdline);
	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}

pid_t Fork()
{
    pid_t pid = fork();
    if (pid < 0) unix_error("Fork error");
    return pid;
}
void Sigfillset(sigset_t* p) {int res = sigfillset(p); if (res < 0) unix_error("Sigfillset error");}
void Sigemptyset(sigset_t* p) {int res = sigemptyset(p); if (res < 0) unix_error("Sigemptyset error");}
void Sigaddset(sigset_t* p, int nowSig) {int res = sigaddset(p, nowSig); if (res < 0) unix_error("Sigaddset error");}
void Sigprocmask(int how, sigset_t* nowSet, sigset_t* oldSet) 
{
    int res = sigprocmask(how, nowSet, oldSet);
    if (res < 0 ) unix_error("Sigprocmask Error");
}
void Execve(char* PATH, char** argv, char** env)
{
    if (execve(PATH, argv, env) < 0) {
        printf("%s: Command not found\n", PATH);
        exit(0);
    }
}
void Setpgid(pid_t p1, pid_t p2)
{
    if (setpgid(p1, p2) < 0) unix_error("setpgid_error");
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
*/
void eval(char *cmdline) 
{
    char* argv[MAXARGS];
    int isBG = parseline(cmdline, argv);
    int isBuiltinCmd = !strcmp(argv[0], "quit") + !strcmp(argv[0], "jobs") * 2 + !strcmp(argv[0], "bg") * 3 
                        + !strcmp(argv[0], "fg") * 4;
    if (isBuiltinCmd == 1) {    //faced with builtin command "quit"
        exit(0);
    }
    if (isBuiltinCmd == 2) {    //faced with builtin command "jobs"
        listjobs(jobs);
    }
    else if (isBuiltinCmd == 3 || isBuiltinCmd == 4) {  //faced with builtin command "bg"/"fg"
        do_bgfg(argv);
    }
    else {
        //other case, need to fork
        pid_t pid;
        sigset_t mask_all, mask_one, prev_one;
        Sigfillset(&mask_all);
        Sigemptyset(&mask_one);
        Sigaddset(&mask_one, SIGCHLD);
        Sigprocmask(SIG_BLOCK, &mask_one, &prev_one);
        if ((pid = Fork()) == 0) {
            //son
            Sigprocmask(SIG_SETMASK, &prev_one, NULL);
            Setpgid(0, 0);
            //printf("%s\n", *(argv+1));
            Execve(argv[0], argv, environ);
        }
        else {
            //parent
                Sigprocmask(SIG_BLOCK, &mask_all, NULL);
                Addjob(jobs, pid, isBG?BG:FG, cmdline);
                Sigprocmask(SIG_SETMASK, &prev_one, NULL);
            if (!isBG) {
                //int status;
                //if (waitpid(pid, &status, 0) < 0) unix_error("waitpid error.");
                waitfg(pid);
            }
            else {
                printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);         //my own creation
            }
        }
    }
    //printf("normal return\n");
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
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

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    int isBG = !strcmp(argv[0], "bg");
    int isJID, ID;
    char  tmpStr[] = "abcdefghijklmnopqrst";
    if (argv[1] == NULL) {
        printf("%s command requires PID or %%jobid argument\n",(isBG)? "bg" : "fg");
        return;
    }
    if (argv[1][0] == '%') {    //the input is JID: store it in "ID"
        isJID = 1;
        if (strlen(argv[1]) <= 1) {
            printf("%s command requires PID or %%jobid argument\n",(isBG)? "bg" : "fg");
            return;
        }
        strcpy(tmpStr, (char *)(argv[1]) + 1);
        for (int i = 0; i < strlen(tmpStr); i++) {
            if (!(tmpStr[i] >= '0' && tmpStr[i] <= '9')) {
                printf("%s: argument must be a PID or %%jobid\n",(isBG)? "bg" : "fg");
                return;
            }
        }
        ID = atoi(tmpStr);
    }
    else {    //the input is PID: store it in "ID"
        isJID = 0;
        strcpy(tmpStr, argv[1]);
        for (int i = 0; i < strlen(tmpStr); i++) {
            if (!(tmpStr[i] >= '0' && tmpStr[i] <= '9')) {
                printf("%s: argument must be a PID or %%jobid\n",(isBG)? "bg" : "fg");
                return;
            }
        }
        ID = atoi(tmpStr);
    }
    //printf("%d %d %d\n", isBG, isJID, ID);
    if (isJID) {
        struct job_t* tmpJob = getjobjid(jobs, ID);
        if (tmpJob == NULL) {
            printf("%%%d: No such job\n", ID);
            return;
        }
        Kill(-(tmpJob->pid), SIGCONT);      //send SIGCONT
        tmpJob->state = (isBG)? BG : FG;
        if (isBG) printf("[%d] (%d) %s", tmpJob->jid, tmpJob->pid, tmpJob->cmdline);
        else waitfg(tmpJob->pid);
    }
    else {
        struct job_t* tmpJob = getjobpid(jobs, ID);
        if (tmpJob == NULL) {
            printf("(%d): No such process\n", ID);
            return;
        }
        Kill(-ID, SIGCONT);      //send SIGCONT
        tmpJob->state = (isBG)? BG : FG;
        if (isBG) printf("[%d] (%d) %s", tmpJob->jid, tmpJob->pid, tmpJob->cmdline);
        else waitfg(tmpJob->pid);
    }
    return;
}

void Sleep(int sec)
{
    if (sleep(sec)) unix_error("sleep_error");
}
/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    while (pid == fgpid(jobs)) {
        Sleep(1);
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    int olderrno = errno;

    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        //printf("reapedpid: %d\n",pid);
        if (WIFEXITED(status)) {
            Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            Deletejob(jobs, pid);
            Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        }
        else if (WIFSIGNALED(status)) {
            printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
            Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            Deletejob(jobs, pid);
            Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        }
        else if (WIFSTOPPED(status)) {
            printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
            if (getjobpid(jobs, pid) != NULL) getjobpid(jobs, pid)->state = ST;
            else unix_error("getjobpid_error");
        }
    }
    //printf("pid: %d, errno: %d\n",pid, errno);
    //if (errno != ECHILD) unix_error("waitpid_error");
    errno = olderrno;
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    int olderrno = errno;

    pid_t pid = fgpid(jobs);
    if (!pid) {errno = olderrno; return;}
    Kill(-pid, SIGINT);
    errno = olderrno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    int olderrno = errno;

    pid_t pid = fgpid(jobs);
    if (!pid) {errno = olderrno; return;}
    Kill(-pid, SIGTSTP);
    errno = olderrno;
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
// returns: 1 if succeed, 0 if fail
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
        //printf("NOW NEXTJID: %d\n",nextjid);
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
void Addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    if (!addjob(jobs, pid, state, cmdline)) unix_error("addjob_error");
}
/* deletejob - Delete a job whose PID=pid from the job list */
// returns: 1 if succeed, 0 if fail
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
void Deletejob(struct job_t *jobs, pid_t pid)
{
    if (!deletejob(jobs, pid)) unix_error("deletejob_error");
}
/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
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

/* pid2jid - Map process ID to job ID */
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

// int getRelBgJid(pid_t pid)
// {
//     int cnt = 0;
//     if (pid < 1) return 0;
//     for (int i = 0; i < MAXJOBS; i++) {
//         if (jobs[i].state == BG) cnt++;
//         if (jobs[i].pid == pid) return cnt;
//     }
//     return 0;
// }

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
        if (jobs[i].state == FG) continue;
	    printf("[%d] (%d) ", pid2jid(jobs[i].pid), jobs[i].pid);
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
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
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
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    Sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



