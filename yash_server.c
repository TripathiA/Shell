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
#include <pthread.h>
 

#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <time.h>
#include <sys/prctl.h>

#define MY_PORT		3286
#define MAXBUF		1024



int clientfd;
pthread_mutex_t lock;
static FILE* log_file;


static char* args[512];
pid_t pid;
int command_pipe[2];
pid_t currentChild = 0;
pid_t fgcurrentChild = -1;
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

struct thread_args {
    int sock;
    int port;
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

 
void write_to_log(char* command_name, int port)
{
	//printf("INSIDE LOGGER\n");
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	//printf("now: %d-%d-%d %d:%d:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	char log_line[2000];
	sprintf(log_line,"%d %d %d:%d:%d yashd[127.0.0.1:%d]: %s",tm.tm_mon+1,tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,port,command_name);
	log_line[strlen(log_line)-2] = '\0';
	//printf("LOG LINE: %s\n",log_line );
	fprintf(log_file, "%s\n", log_line);
	fflush(log_file);
	memset(log_line,'\0',2000);
	//write(log_file,log_line,sizeof(log_line));
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
	printf("GOING TO PRINT JOBS\n");
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
    printf("JOBS PRINTED\n");
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
	  printf("%s\n",execvp(args[0], args));
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
	  printf("%s\n",execvp(args[0], args));
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

void sigint_handler(int sec=0) 
{
    pid_t pid = fgcurrentChild;
	if(pid>0)
	{
		kill(pid,SIGINT); 
		if (!sec)
			printf("^C\n# ");
		else
			printf("^C\n");
		//siglongjmp(ctrlc_buf, 1);
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


void sigtstp_handler(int sec=0) 
{
    pid_t pid = fgcurrentChild;
	
	if(pid==0)
	return;
	else if(pid > 0)
	{
		kill(pid,SIGTSTP); 
		print_all_jobs();
		if (!sec)
			printf("^Z\n# ");
		else
			printf("^Z\n");
	}

	return;
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

void *control_handler(void *argp)
{
	struct thread_args *arguments = argp;
	int sock = arguments->sock;
	int port = arguments->port;
	//int sock = *(int*)socket_desc;
    int read_size;
    char *message , line[2000];


	while (read_size = recv(sock , line , 2000 , 0) <= 0 ) {
		printf("HERE\n");
	}
		fflush(NULL);
 		read_size = recv(sock , line , 2000 , 0);

		if(read_size == 0)
	    {
	        puts("Client disconnected");
	        fflush(stdout);
	        //break;
	    }
	    else if(read_size == -1)
	    {
	        perror("recv failed");
	        //break;
	    }

		line[read_size] ='\0';

		char* type = strtok(line," ");
		char* cmd = strtok(NULL,"");


		// printf("[%s]\n", type);
		// printf("[%s]\n", cmd);
		if(strstr(type,"CTL") != NULL)
		{
			pthread_mutex_lock(&lock);
			write_to_log(cmd,port);
			pthread_mutex_unlock(&lock);
			if(strstr(cmd,"c"))
			{
				printf("CTL c \n");
				sigint_handler();
			}
			if(strstr(cmd,"z"))
			{
				printf("CTL z\n");
				sigtstp_handler();
			}
			printf("\n# ");
		}
		printf("OUT OF CONTROL\n");
		return;
}


void *connection_handler(void *argp)
{
	struct thread_args *arguments = argp;
	int sock = arguments->sock;
	int port = arguments->port;
	//int sock = *(int*)socket_desc;
    int read_size;
    char *message , line[2000];

	printf("SIMPLE SHELL: Type 'Ctrl-D' to exit.\n");

	// signal(SIGTSTP, sigtstp_handler); 
	// signal(SIGINT, sigint_handler); 
	// signal(SIGQUIT, sigquit_handler);
	signal(SIGCHLD,sigchld_handler);
	initjobs(jobs);


	//while ( sigsetjmp( ctrlc_buf, 1 ) != 0 );

	dup2(sock,STDOUT_FILENO);

	// pthread_t control_commands;
	// if( pthread_create( &control_commands , NULL ,  control_handler , argp) < 0)
	// {
	// 	perror("could not create thread");
	// 	return 1;
	// }

	while (1) {
		fflush(NULL);
 		int background = 0;

 		//signal(SIGINT, sigint_handler); 

 		read_size = recv(sock , line , 2000 , 0);
		// if (!(read_size = recv(sock , line , 2000 , 0))) 
		// 	return 0;

		if(read_size == 0)
	    {
	        puts("Client disconnected");
	        fflush(stdout);
	        break;
	    }
	    else if(read_size == -1)
	    {
	        perror("recv failed");
	        break;
	    }

	    

		//char* cmd = line;
		line[read_size] ='\0';

		// printf("INP: {%s}\n", line);
		char* type = strtok(line," ");
		char* cmd = strtok(NULL,"");

		pthread_mutex_lock(&lock);
		write_to_log(cmd,port);
		pthread_mutex_unlock(&lock);

		// printf("[%s]\n", type);
		printf("Executing:[%s]\n", cmd);
		if(strstr(type,"CTL") != NULL)
		{
			if(strstr(cmd,"c"))
			{
				printf("CTL c \n");
				sigint_handler();
			}
			if(strstr(cmd,"z"))
			{
				printf("CTL z\n");
				sigtstp_handler();
			}
			printf("\n# ");
		}
		else
		{
			if (strstr(cmd, "jobs") != NULL)
			{
				print_all_jobs();
				print_buffer();
				printf("\n# ");
				continue;
			}
			else if (strstr(cmd, "bg") != NULL)
			{
				split(line);
				send_to_background(args);
				print_buffer();
				printf("\n# ");
				continue;
			}
			else if (strstr(cmd, "fg") != NULL)
			{
				split(line);
				send_to_foreground(args);
				print_buffer();
				printf("\n# ");
				continue;
			}
			else if (!strcmp(cmd,"\n"))
			{
				print_buffer();
				printf("\n# ");
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
			// printf("cmd->%s\n", cmd1);

			char* cmd2 = strtok(NULL, "|"); /* Find first '|' */
	 		// printf("Next->%s\n", cmd2);

	 		

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
	 				printf("\n# ");
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
	 			printf("\n# ");

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

		 			// printf("F I O-%s,%s,%s,DONE.%d,%d\n", final_command,inp_file,out_file,in,out);
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
					// printf("%s\n", check_command);
					printf("%s\n",execvp(args[0], args));
					// printf("OUT0-%s,%s\n",args[0],final_command);
					if(check_valid(check_command) == 0)
					{
					   	printf("%s - command not found\n",check_command);
					}
					print_buffer();

					// exit(0);
			    }
			    if (!background)
			    {
			    	// printf("BLAH\n");
			    	int status1;
			    	insert_job(pid1,FG,cmd);
			    	pid_t temp_pid = fork();
			    	if(temp_pid == 0)
			    	{
			    		printf("INSIDE ANOTHER FORK\n");
					 		read_size = recv(sock , line , 2000 , 0);

							if(read_size == 0)
						    {
						        printf("Client disconnected");
						        //break;
						    }
						    else if(read_size == -1)
						    {
						        perror("recv failed");
						        //break;
						    }

							line[read_size] ='\0';

							char* type = strtok(line," ");
							char* cmd = strtok(NULL,"");


							// printf("[%s]\n", type);
							// printf("[%s]\n", cmd);
							if(strstr(type,"CTL") != NULL)
							{
								pthread_mutex_lock(&lock);
								write_to_log(cmd,port);
								pthread_mutex_unlock(&lock);
								if(strstr(cmd,"c"))
								{
									printf("CTL c \n");
									sigint_handler(1);
								}
								if(strstr(cmd,"z"))
								{
									printf("CTL z\n");
									sigtstp_handler(1);
								}
								// printf("\n# ");
							}
							printf("OUT OF CONTROL\n");
			    	}
	   				waitforpid(pid1);
	   				printf("GOING TO KILL TEMP\n");
	   				kill(temp_pid,SIGKILL);
	   				printf("WAIT DONE\n#");
	   				// printf("BLh blah \n");
	   				//waitpid(pid1,&status1,0);
	 			}
	 			else
	 			{
	 				insert_job(pid1,BG,cmd);
	 			}
	 			// printf("OUT\n");
	 			printf("\n# ");

	 		}
	 		
 		}
		
	}
	//Free the socket pointer
	free(sock);
	close(sock);
	pthread_exit(NULL); 
	return;
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
		if (next != NULL) next[0] = '\0';
		++i; 
	}
	args[i] = NULL;
}



static void skeleton_daemon()
{
    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    /* Catch, ignore and handle signals */
    //TODO: Implement a working signal handler */
    //signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    chdir("/");

    /* Close all open file descriptors */
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
    }

    /* Open the log file */
    //openlog ("firstdaemon", LOG_PID, LOG_DAEMON);
}


int main(int Count, char *Strings[])
{   
	//skeleton_daemon();

	log_file = fopen("/home/aastha/Desktop/yash/yashd.log","w");
	fprintf(log_file, "SERVER DAEMON STARTED\n");
	fflush(log_file);
	

	static FILE *log; /* for the log */
    int fd;
	char* u_log_path = "/home/aastha/Desktop/yash/yashd.err";
	log = fopen(u_log_path, "aw"); /* attach stderr to u_log_path */
	fd = fileno(log);  /* obtain file descriptor of the log */
	dup2(fd, STDERR_FILENO);
	close (fd);

	int sockfd;
	struct sockaddr_in self;
	char buffer[MAXBUF];

	//signal(SIGQUIT, sigquit_handler);
	/*---Create streaming socket---*/
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("Socket");
		exit(errno);
	}
	int option = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

	/*---Initialize address/port structure---*/
	memset(&self.sin_zero, 0, sizeof(self.sin_zero));
	self.sin_family = AF_INET;
	self.sin_port = htons(MY_PORT);
	self.sin_addr.s_addr = inet_addr("127.0.0.1");

	/*---Assign a port number to the socket---*/
    if ( bind(sockfd, (struct sockaddr*)&self, sizeof(self)) != 0 )
	{
		perror("socket--bind");
		exit(errno);
	}

	/*---Make it a "listening socket"---*/
	if ( listen(sockfd, 20) != 0 )
	{
		perror("socket--listen");
		exit(errno);
	}

	struct sockaddr_in client_addr;
	int addrlen=sizeof(client_addr);

		/*---accept a connection (creating a data pipe)---*/
	printf("going to accept\n");
		// clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
		// printf("%s:%d connected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		/*---Echo back anything sent---*/
	int *new_sock;
		
	while(clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen))
	{
		printf("%s:%d connected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
		pthread_t sniffer_thread;

		struct thread_args *args = malloc(sizeof *args);

		args->sock = clientfd;
		args->port = ntohs(client_addr.sin_port);
		// new_sock = malloc(sizeof *new_sock);
		// *new_sock = clientfd;
		// close(1);
		// dup2(clientfd,STDOUT_FILENO);
		if( pthread_create( &sniffer_thread , NULL ,  connection_handler , args) < 0)
		{
			perror("could not create thread");
			return 1;
		}
		// close(sockfd);
	}	
	if (clientfd < 0)
    {
        perror("accept failed");
        return 1;
    }
	

	/*---Clean up (should never get here!)---*/
	close(sockfd);
	return 0;
}


// if (execvp( args[0], args) == -1)
// 			_exit(EXIT_FAILURE); // If child fails