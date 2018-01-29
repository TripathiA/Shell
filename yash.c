#include <setjmp.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
 
#include <sys/types.h>
#include <sys/wait.h>
 
static char* args[512];
pid_t pid;
int command_pipe[2];
pid_t currentChild = 0;
pid_t fgcurrentChild = 10000;
char done_buffer[1000];

#define READ  0
#define WRITE 1
#define MAXLINE    200   
#define max_jobs     50 

#define UNDEF 0 
#define FG 1    
#define BG 2    
#define ST 3    

struct job_t {              
    pid_t pid;            
    int jid;              
    int state;              
    char cmdline[MAXLINE];  
};


static void split(char* cmd);
static char* skipwhite(char* s);
static char line[1024];


struct job_t;
static struct job_t jobs[max_jobs];
int nextjid = 1; 

void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < max_jobs; i++)
		clearjob(&jobs[i]);
}

int maxjid() 
{
    int i, max=0;

    for (i = 0; i < max_jobs; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

void insert_job(pid_t gpid,int gstate,char* gcmd)
{
	for (int  i = 0 ;i< max_jobs ; i++)
	{
		if(jobs[i].pid == 0)
		{
			jobs[i].pid = gpid;
			jobs[i].jid = nextjid++;
			jobs[i].state = gstate;
			strcpy(jobs[i].cmdline, gcmd);
			return;
		}
	}
}

int delete_job(pid_t gpid)
{
	for (int  i = 0 ;i< max_jobs ; i++)
	{
		if(jobs[i].pid == gpid)
		{
			jobs[i].pid = 0;
		    jobs[i].jid = 0;
		    jobs[i].state = UNDEF;
		    jobs[i].cmdline[0] = '\0';
		    nextjid = maxjid()+1;
		    return 1;
		}
	}
	return 0;
}

void changestate(pid_t pid, int state)
{
	for (int  i = 0 ;i< max_jobs ; i++)
	{
		if(jobs[i].pid == 0)
		{
			jobs[i].state = state;
		}
	}
}

 

int check_valid(char* command)
{
  struct stat sb;
  char* delimiter = ":";
  char* path = getenv("PATH");
  size_t start_pos = 0, end_pos = 0;
  int i = 0;
  int len = 0;
  char *p = strtok(path, delimiter);
  char *array[100];

  while (p != NULL)
  {
    array[i++] = p;
    p = strtok (NULL, delimiter);
    len++;
  }
  i = 0;
  while(i < len)
  {
    char *current_path = malloc(strlen(array[i])+strlen("/")+strlen(command)+1);
    strcpy(current_path, array[i]);
    strcat(current_path,"/");
    strcat(current_path, command);
    if ((stat(current_path, &sb) == 0) && (sb.st_mode & S_IXOTH))
    {
      return 1;
    }
    i++;
  }
  return 0;
}


void print_all_jobs() 
{
    int i;
    char* val[4];
    val[1] = "+ Running \t";
    val[2] = "- Running \t";
    val[3] = "+ Stopped \t";
    for (i = 0; i < max_jobs; i++) {
	
		if (jobs[i].pid != 0) {
		    printf("[%d] %s %s\n", jobs[i].jid,val[jobs[i].state],skipwhite(jobs[i].cmdline));
		}
    }
}

int run_pipe(char* command1,char* command2,int bg)
{
   pid_t pid1, pid2;
   int pipefd[2];
   pipe(pipefd);

   pid1 = fork();
   if (pid1 == 0) {
      char* cmd = command1;
      if (strchr(cmd,'>'))
	  {	
		printf("SYNTAX ERROR\n");
		exit(0);
		return 1;
	  }
	  char* cmd1 = strtok(cmd,"<");
	  char* cmd2 = strtok(NULL,"<");
	  if (cmd2)
	  {
			cmd2 = skipwhite(cmd2);
			int fd0 ;
			if ((fd0 = open(cmd2 , O_RDONLY, 0)) < 0) {
			    perror("Couldn't open the input file");
				exit(0);
			}           
			dup2(fd0, STDIN_FILENO); 
			close(fd0);
	  }
	  dup2(pipefd[1], STDOUT_FILENO);
      close(pipefd[0]);
	  split(cmd1);
	  execvp(args[0], args);
	  char* check_command = args[0];
	  if(check_valid(check_command) == 0)
	  {
		printf("%s - command not found\n",check_command);
	  }
      //execvp(args[0], args);
   }
   // Create our second process.
   pid2 = fork();
   currentChild = pid2;
   if (!bg)
   {
   	fgcurrentChild = pid2;
   }
   if (pid2 == 0) {
      char* cmd = command2;
      if (strchr(cmd,'<'))
	  {	
		printf("SYNTAX ERROR\n");
		exit(0);
		return 1;
	  }
	  char* cmd1 = strtok(cmd,">");
	  char* cmd2 = strtok(NULL,">");
	  if (cmd2)
	  {
			cmd2 = skipwhite(cmd2);
			int fd1 ;
			if ((fd1 = creat(cmd2 , 0644)) < 0) {
			    perror("Couldn't open the output file");
				exit(0);
			}           
			dup2(fd1, STDOUT_FILENO);
			close(fd1);
	  }
	  dup2(pipefd[0], STDIN_FILENO);
      close(pipefd[1]);
	  split(cmd1);
	  execvp(args[0], args);
	  char* check_command = args[0];
	  if(check_valid(check_command) == 0)
	  {
		printf("%s - command not found\n",check_command);
	  }
   }
   close(pipefd[0]);
   close(pipefd[1]);
   if (!bg)
   {
   	int status1,status2;
   	waitpid(pid1,&status1,WUNTRACED);
   	waitpid(pid2,&status2,WUNTRACED);
   }
   exit(0);
   return 0;
}

sigjmp_buf ctrlc_buf;

void sigint_handler(int sig) 
{
    pid_t pid = fgcurrentChild;
	
	if(pid>0)
	{
		printf("^C\n");
		kill(pid,SIGINT); 
		siglongjmp(ctrlc_buf, 1);
	}
	return;
}

void sigquit_handler(int p){
	// We send a SIGTERM signal to the child process
	// if (kill(fgcurrentChild,SIGTERM) == 0){
	// 	printf("\nProcess %d received a SIGQUIT signal\n",fgcurrentChild);
	// 	//no_reprint_prmpt = 1;			
	// }else{
	// 	printf("\n");
	// }
	exit(1);
	exit(1);
}


void sigtstp_handler(int sig) 
{
    pid_t pid = fgcurrentChild;
	
	if(pid==0)
	return;
	else
	{
		kill(pid,SIGTSTP); 
	}
}

struct job_t *getjobpid(pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < max_jobs; i++)
    {
    	if (jobs[i].pid == pid)
	    	return &jobs[i];
    }
	
    return NULL;
}

struct job_t *getjobpidbyjid(int jid) {
    int i;

    for (i = 0; i < max_jobs; i++)
    {
    	if (jobs[i].jid == jid)
	    	return &jobs[i];
    }
	
    return NULL;
}


void sigchld_handler(int sig) 
{
    struct job_t *s4;
	int status = -1;
	pid_t pid;
			
	
		while((pid=waitpid(-1,&status,WNOHANG|WUNTRACED))>0)  
		{
			s4 = getjobpid(pid);
			//printf("%d,%d\n",s4->pid,s4->jid );
			if(WIFEXITED(status))
			{   
				if (s4->state == BG)    
					sprintf(done_buffer,"[%d]+ Done\t\t%s\n",s4->jid,skipwhite(s4->cmdline));
				//printf("[%d]+ Done    %s\n",s4->jid,s4->cmdline );
				delete_job(pid);
				
			}
			if(WIFSIGNALED(status))
			{
				//printf("[%d] (%d) terminated by signal 2\n",s4->jid,s4->pid);
				delete_job(pid);
			}
			if(WIFSTOPPED(status))
			{
				printf("[%d]+ Stopped \t %s\n",s4->jid,skipwhite(s4->cmdline));
				s4->state = 3; 
			}
		
		}
	
	return;
}

void waitforpid(pid_t pid)
{
   
	struct job_t *s5;
	s5 = getjobpid(pid);
	while(s5!=NULL&&(s5->state==FG))
    	{
      	sleep(1);
      		
    	}
    	return;
	
}


void send_to_background(char** argv)
{
	struct job_t* fg_job;
	pid_t cmd_pid;
	if (strstr(argv[1],"%") != NULL)
	{
		char* p = strstr(argv[1],"%");
		p++;
		int job_num = atoi(p);
		fg_job = getjobpidbyjid(job_num);
		if (fg_job == NULL)
		{
			printf("NOT VALID PID or JOBID\n");
			return ;
		}
	}
	else if (isdigit(argv[1][0]))
	{
		pid_t job_pid = atoi(argv[1]);
		fg_job = getjobpid(job_pid);
		if (fg_job == NULL)
		{
			printf("NOT VALID PID or JOBID\n");
			return ;
		}
	}
	kill(fg_job->pid,SIGCONT);
	fg_job->state = BG;
	printf("[%d]+ %s &\n",fg_job->jid,skipwhite(fg_job->cmdline));
}

void send_to_foreground(char** argv)
{
	struct job_t* fg_job;
	pid_t cmd_pid;
	if (strstr(argv[1],"%") != NULL)
	{
		char* p = strstr(argv[1],"%");
		p++;
		int job_num = atoi(p);
		fg_job = getjobpidbyjid(job_num);
		if (fg_job == NULL)
		{
			printf("NOT VALID PID or JOBID\n");
			return ;
		}
	}
	else if (isdigit(argv[1][0]))
	{
		pid_t job_pid = atoi(argv[1]);
		fg_job = getjobpid(job_pid);
		if (fg_job == NULL)
		{
			printf("NOT VALID PID or JOBID\n");
			return ;
		}
	}
	kill(fg_job->pid,SIGCONT);
	fg_job->state = FG;
	printf("%s\n", skipwhite(fg_job->cmdline));
	waitforpid(fg_job->pid);
}

void print_buffer()
{
	if (done_buffer[0] != '\0')
		printf("%s\n",done_buffer );
	done_buffer[0] = '\0';

}

int main()
{
	printf("SIMPLE SHELL: Type 'Ctrl-D' to exit.\n");

	signal(SIGTSTP, sigtstp_handler); 
	signal(SIGINT, sigint_handler); 
	signal(SIGQUIT, sigquit_handler);
	signal(SIGCHLD,sigchld_handler);
	initjobs(jobs);


	while ( sigsetjmp( ctrlc_buf, 1 ) != 0 );

	while (1) {
		printf("\n# ");
		fflush(NULL);
 		int background = 0;

 		signal(SIGINT, sigint_handler); 

		if (!fgets(line, 1024, stdin)) 
			return 0;

		char* cmd = line;

		if (strstr(line, "jobs") != NULL)
		{
			print_all_jobs();
			print_buffer();
			continue;
		}
		else if (strstr(line, "bg") != NULL)
		{
			split(line);
			send_to_background(args);
			print_buffer();
			continue;
		}
		else if (strstr(line, "fg") != NULL)
		{
			split(line);
			send_to_foreground(args);
			print_buffer();
			continue;
		}
		else if (!strcmp(line,"\n"))
		{
			print_buffer();
			continue;
		}

		char* initial_command = malloc(strlen(line)+1);
		strcpy(initial_command,line);

		if (strchr(cmd,'&'))
 		{
 			cmd = strtok(line,"&");
 			background = 1;
 		}

		char* cmd1 = strtok(cmd,"|");
		//printf("cmd->%s\n", cmd1);

		char* cmd2 = strtok(NULL, "|"); /* Find first '|' */
 		//printf("Next->%s\n", cmd2);

 		

 		if (cmd2)
 		{
 			pid_t pid1 = fork();
 			currentChild = pid1;
 			if (!background)
			{
			   	fgcurrentChild = pid1;
			}
			if (pid1 < 0)
			{
				perror("Fork Failed\n");
				exit(0);
			}
			if (pid1 == 0)
 			{
 				run_pipe(cmd1,cmd2,background);
 			}
		    if (!background)
		    {
		    	int status1;
		    	insert_job(pid1,FG,skipwhite(initial_command));
   				waitforpid(pid1);
   				//waitpid(pid1,&status1,0);
 			}
 			else
 			{
 				insert_job(pid1,BG,skipwhite(initial_command));
 			}
 		}
 		else
 		{
 			pid_t pid1 = fork();
 			currentChild = pid1;
 			char* final_command;
 			if (!background)
			   {
			   	fgcurrentChild = pid1;
			   }
 			if (pid1 == 0)
 			{
 				int in = 0;
 				int out = 0;
 				char* inp_file = "";
 				char* out_file = "";

	 			if (strchr(cmd,'>'))
	 			{
	 				char* cmd1 = strtok(cmd,">");
				 	char* cmd2 = strtok(NULL,">");
				 	out = 1;
				 	if (strchr(cmd1,'<'))
				 	{
				 		char* cmd11 = strtok(cmd1,"<");
					    char* cmd21 = strtok(NULL,"<");
						if (cmd21)
						{
							cmd21 = skipwhite(cmd21);
							inp_file = cmd21;
							in = 1;
						}
						final_command = cmd11;
						out_file = skipwhite(cmd2);
				 	}	
				 	else if (strchr(cmd2,'<'))
				 	{
				 		char* cmd11 = strtok(cmd2,"<");
					    char* cmd21 = strtok(NULL,"<");
						if (cmd21)
						{
							cmd21 = skipwhite(cmd21);
							inp_file = cmd21;
							in = 1;
						}
						out_file = skipwhite(cmd11);
						final_command = cmd1;
				 	}
				 	else
				 	{
				 		final_command = cmd1;
				 		out_file = skipwhite(cmd2);
				 	}
	 			}
	 			else
	 			{
	 				char* cmd1 = strtok(cmd,"<");
				    char* cmd2 = strtok(NULL,"<");
					if (cmd2)
					{
						cmd2 = skipwhite(cmd2);
						inp_file = cmd2;
						in = 1;
					}
					final_command = cmd1;
	 			}

	 			// printf("F I O-%s,%s,%sDONE%d,%d\n", final_command,inp_file,out_file,in,out);
	 			if (in == 1)
				{
					int fd0;
				    if ((fd0 = open(inp_file, O_RDONLY, 0)) < 0) {
				        perror("Couldn't open input file");
				        exit(0);
				    } 
				    dup2(fd0, STDIN_FILENO); // STDIN_FILENO here can be replaced by 0 
				    close(fd0); // necessary
			    }
			    //if '>' char was found in string inputted by user
			    if (out == 1)
			    {
				    int fd1 ;
				    if ((fd1 = creat(out_file , 0644)) < 0) {
				        perror("Couldn't open the output file");
				        exit(0);
				    }           
				    dup2(fd1, STDOUT_FILENO);
				    close(fd1);
			   	}
				split(final_command);
				char* check_command = args[0];
				execvp(args[0], args);
				if(check_valid(check_command) == 0)
				{
				   	printf("%s - command not found\n",check_command);
				}
				print_buffer();
				exit(0);
		    }
		    if (!background)
		    {
		    	int status1;
		    	insert_job(pid1,FG,line);
   				waitforpid(pid1);
   				//waitpid(pid1,&status1,0);
 			}
 			else
 			{
 				insert_job(pid1,BG,line);
 			}
 		}
		
	}
	return 0;
}

static char* skipwhite(char* s)
{
	while (isspace(*s)) ++s;
	char* back = s + strlen(s);
    while(isspace(*--back));
    *(back+1) = '\0';
	if (s[strlen(s)-1] == '\n')
		s[strlen(s)-1] = 0;
	return s;
}
 

static char* skipwhite1(char* s)
{
	while (isspace(*s)) ++s;
	return s;
}

static void split(char* cmd)
{
	cmd = skipwhite1(cmd);
	char* next = strchr(cmd, ' ');
	int i = 0;
 
	while(next != NULL) {
		next[0] = '\0';
		args[i] = cmd;
		++i;
		cmd = skipwhite1(next + 1);
		next = strchr(cmd, ' ');
	}
 
	if (cmd[0] != '\0') {
		args[i] = cmd;
		next = strchr(cmd, '\n');
		next[0] = '\0';
		++i; 
	}
 
	args[i] = NULL;
}


// if (execvp( args[0], args) == -1)
// 			_exit(EXIT_FAILURE); // If child fails