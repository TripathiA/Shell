#include <setjmp.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
 
#include <sys/types.h>
#include <sys/wait.h>

#include <stdio.h> //printf
#include <string.h>    //strlen
#include <sys/socket.h>    //socket
#include <arpa/inet.h> //inet_addr
#include <netdb.h> /* struct hostent */


#define PORT 3286
sigjmp_buf ctrlc_buf;
sigjmp_buf ctrlz_buf;

int sock;
struct sockaddr_in server;
struct hostent *hp;
char message[1000] , server_reply[5000];

int sigint_flag = 0;
int sigstp_flag = 0;

void sigint_handler(int sig) 
{
    printf("SIG INT\n");
    char* command = "CTL c\n";
    if( send(sock , command , strlen(command) , 0) < 0)
    {
        puts("Send failed");
    }

    printf("Sending:{%s}\n",command );
    memset(server_reply,'\0',sizeof(server_reply));
    //Receive a reply from the server
    while(strstr(server_reply,"#") == NULL)
        {
            printf("INSIDE SIGINT WHILE\n");
            memset(server_reply,'\0',sizeof(server_reply));
            if( recv(sock , server_reply , 20000 , 0) < 0)
            {
                puts("recv failed");
                break;
            }

            // printf("Server reply : ");
            printf("%s",server_reply);
            
        }
    //iglongjmp(ctrlc_buf, 1);
    return;
}

void sigquit_handler(int p){
    printf("HERE_END\n");
    exit(1);
    exit(1);
    //siglongjmp(ctrlc_buf, 1);
    
    return;
}


void sigtstp_handler(int sig) 
{
    char* command = "CTL z\n";
    if( send(sock , command , strlen(command) , 0) < 0)
    {
        perror("Send failed");
    }

    //Receive a reply from the server
    printf("Sending:{%s}\n",command );
    memset(server_reply,'\0',sizeof(server_reply));
    while(strstr(server_reply,"#") == NULL)
        {
            printf("INSIDE SIGSTP WHILE\n");
            memset(server_reply,'\0',sizeof(server_reply));
            if( recv(sock , server_reply , 20000 , 0) < 0)
            {
                puts("recv failed");
                break;
            }

            // printf("Server reply : ");
            printf("%s",server_reply);
            
        }
    return;
}


int main(int argc , char *argv[])
{
    // int sock;
    // struct sockaddr_in server;
    // struct hostent *hp;
    // char message[1000] , server_reply[5000];



    //Create socket
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1)
    {
        printf("Could not create socket");
    }
    puts("Socket created");

    hp = gethostbyname(argv[1]);
    bcopy ( hp->h_addr, &(server.sin_addr.s_addr), hp->h_length);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    //Connect to remote server
    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
    {
        perror("connect failed. Error");
        return 1;
    }

    printf("Connected\n");

    signal(SIGTSTP, sigtstp_handler); 
    signal(SIGINT, sigint_handler); 
    if(signal(SIGQUIT, sigquit_handler) == SIG_ERR)
        printf("Parent: Unable to create handler for SIGQUIT\n");

    // while ( sigsetjmp( ctrlc_buf, 1 ) != 0 );
    // while ( sigsetjmp( ctrlz_buf, 1 ) != 0 );

    //keep communicating with server
    while(1)
    {
        // printf("NOW ENTER\n");
        if (!fgets(message, 2000, stdin)) 
            return 0;
        char *command = malloc(strlen("CMD ")+strlen(message)+strlen("\n")+1);
        strcpy(command,"CMD ");
        strcat(command,message);
        strcat(command,"\n");

        printf("Sending:{%s}\n",command );

        //Send some data
        if( send(sock , command , strlen(command) , 0) < 0)
        {
            puts("Send failed");
            return 1;
        }

        //Receive a reply from the server
        memset(server_reply,'\0',sizeof(server_reply));
        while(strstr(server_reply,"#") == NULL)
        {
            printf("INSIDE WHILE\n");
            memset(server_reply,'\0',sizeof(server_reply));
            if( recv(sock , server_reply , 20000 , 0) < 0)
            {
                puts("recv failed");
                break;
            }

            // printf("Server reply : ");
            printf("%s",server_reply);
            
        }
        
    }

    close(sock);
    return 0;
}