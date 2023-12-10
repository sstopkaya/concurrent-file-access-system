/*
*	Client
*/

/*added for std=c11 if makefile will be changed*/
#define _POSIX_C_SOURCE 200809L

/* libraries */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h> 
#include <errno.h>   
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/mman.h>

#define SIZE 1024

void signalHandler(int sig);

sem_t *semRead;
struct sigaction sa; 	/* struct for handling signals */

//main
int main(int argc, char *argv[])
	{
	//usage
	if (argc!=3 && strcmp(argv[0], "biboClient")!=0)
		{
		fprintf(stderr, "Usage:\n./biboClient <Connect/tryConnect> ServerPID\n");
		exit(EXIT_FAILURE);
		}
	//valid server id check
	int shmServerPid = shm_open("shmServerId", O_RDONLY, 0666);
	void *ptrSid = mmap(0, 4096, PROT_READ, MAP_SHARED, shmServerPid, 0);
	//fprintf(stderr, "ServerPID:%d\n", atoi(ptrSid));
	//fprintf(stderr, "shm_freeSpot:%d\n", atoi(ptrSid));
	if (atoi(argv[2])!=atoi(ptrSid))
		{
		fprintf(stderr, "Usage:\n./biboClient <Connect/tryConnect> ServerPID\n");
		exit(EXIT_FAILURE);
		}

	//tryconnect case handled
	if (strcmp(argv[1], "tryConnect")==0 || strcmp(argv[1], "tryconnect")==0)
		{
		int shm_freeSpot = shm_open("shm1", O_RDONLY, 0666);
		void *ptr1 = mmap(0, 4096, PROT_READ, MAP_SHARED, shm_freeSpot, 0);
		//fprintf(stderr, "shm_freeSpot:%d\n", atoi(ptr1));
		if (atoi(ptr1)<=0)
			{
			fprintf(stderr, "There is not available slot.\n");
			exit(EXIT_SUCCESS);
			}
		}

	/*____________________________________________signal mask*/
	sa.sa_handler = signalHandler; 
   	sa.sa_flags = SA_RESTART;
    
	if (sigemptyset(&sa.sa_mask) == -1)
		{
		perror("\tFailed to initialize the signal mask");
		exit(EXIT_FAILURE);
		}
	/* SIGINT signals are added */
	if(sigaddset(&sa.sa_mask, SIGINT) == -1)
		{
		perror("\tFailed to initialize the signal mask");
		exit(EXIT_FAILURE);
		}
	/* SIGTERM signals are added */
	if(sigaddset(&sa.sa_mask, SIGTERM) == -1)
		{
		perror("\tFailed to initialize the signal mask");
		exit(EXIT_FAILURE);
		}
	/* SIGSTOP signals are added */
	if(sigaddset(&sa.sa_mask, SIGTSTP) == -1)
		{
		perror("\tFailed to initialize the signal mask");
		exit(EXIT_FAILURE);
		}
	/*____________________________________________signal mask*/

	int shm_fd = shm_open("shm0", O_RDONLY, 0666);
	while(1)
		{
		/*____________________________________________signal handler*/
		if(sigaction(SIGINT, &sa, NULL) == -1)
			{
			perror("\tCan't SIGINT");
			}
		/* Signal Handler is called if SIGTERM catched */
		if(sigaction(SIGTERM, &sa, NULL) == -1)
			{
			perror("\tCan't SIGTERM");
			}
		/* Signal Handler is called if SIGTERM catched */
		if(sigaction(SIGTSTP, &sa, NULL) == -1)
			{
			perror("\tCan't SIGTSTP");
			}
		/*____________________________________________signal handler*/
		int flg=0;
		void *ptr = mmap(0, 4096, PROT_READ, MAP_SHARED, shm_fd, 0);
		//printf("---shm:%d clientpid:%d\n", atoi(ptr), getpid());
		if (flg==0)
			{
	    	fprintf(stdout, "Waiting for Que.. ");
	    	flg=1;
	    	}
	    //if connection fifo is free to use
		if (atoi(ptr)==0)
			{
			char msg[SIZE];
			mkfifo("/tmp/connectFifo", S_IRUSR | S_IWUSR | S_IWGRP);
			int fd = open("/tmp/connectFifo", O_WRONLY);
		    if (fd == -1) 
		    	{
		        perror("Failed to open fifo");
		        exit(EXIT_FAILURE);
		    	}

		    //sending process id to server side
		    sprintf(msg, "%d", getpid());
		    if (write(fd, msg, strlen(msg)+1) == -1)
		    	{
		        perror("Error writing to FIFO");
		        exit(EXIT_FAILURE);
		    	}
			close(fd);
			
	    	fprintf(stdout, "Connection established:\n");

	    	//client fifo created to data transfer
	    	char fifoName[SIZE];
	    	char result[SIZE];
	    	sprintf(fifoName, "/tmp/fifo_%d", getpid());
	    	//fprintf(stdout, "%d\n", getpid());
	    	//printf("client fifo : %s\n", fifoName);
	    	mkfifo(fifoName, S_IRUSR | S_IWUSR | S_IWGRP);
			while (1) 
				{
				//getting command by terminal
				char buf1[SIZE];
	    		fprintf(stdout, ">>");
				fgets(buf1, SIZE, stdin);
				if (buf1[strlen(buf1)-1]=='\n') { buf1[strlen(buf1)-1]='\0'; }

				int fd1=open(fifoName, O_WRONLY);
		   	    write(fd1, buf1, strlen(buf1)+1);
		   	    close(fd1);

		   		if (strcmp(buf1,"quit")==0)
					{
					fprintf(stdout, "Sending write request to server log file\n");
					fprintf(stdout, "waiting for logfile ...\n");
					fprintf(stderr, "logfile write request granted\n");
					int f=open(fifoName, O_RDONLY);
					read(f, result, sizeof(result));
					close(f);
					fprintf(stderr, "%s\n", result);
					exit(EXIT_SUCCESS);
					}
		   		if (strcmp(buf1,"killServer")==0)
					{
					fprintf(stdout, "Sending kill request to server\n");
					int f=open(fifoName, O_RDONLY);
					read(f, result, sizeof(result));
					close(f);
					fprintf(stderr, "%s\n", result);
					if(strcmp(result,"bye")==0)
						{
						exit(EXIT_SUCCESS);
						}
					}
				//try to understand the given command
				int counterCmnd=0;
				char str[SIZE][SIZE];
				char* token=strtok(buf1," ");
				while (token!=NULL)
					{
					sprintf(str[counterCmnd], "%s", token);
					counterCmnd++;
					token=strtok(NULL," ");
					}

				if (strcmp(str[0], "readF")==0 && counterCmnd==2)
					{
					// if try to read all the file content
					while (1)
						{
						// lock the fifo
						sem_wait(semRead);
						int f=open(fifoName, O_RDONLY);
						read(f, result, sizeof(result));
						close(f);
						// here is like base case
						if (strcmp(result, "end")==0)
							{
							break;
							}
						fprintf(stdout, "%s", result);
						sem_post(semRead);
						}
					}
				else
					{
					//if try to read only one line
					int f=open(fifoName, O_RDONLY);
					read(f, result, sizeof(result));
					fprintf(stdout, "%s\n", result);
					close(f);
					}
				}
			}			
		}
	return 0;
	}

/* signal handler function */
void signalHandler(int sig)
	{
	if(sig == SIGINT)
		{
		fprintf(stderr, "\n\nSIGINT signal catched!\n");
		exit(EXIT_SUCCESS);
		}
	else if(sig == SIGTERM)
		{
		fprintf(stderr, "\nSIGTERM signal catched!\n");
		exit(EXIT_SUCCESS);
		}
	else if(sig == SIGTSTP)
		{
		fprintf(stderr, "\nSIGTSTP signal catched!\n");
		exit(EXIT_SUCCESS);
		}
	else if(sig == SIGKILL)
		{
		fprintf(stderr, "\nSIGKILL signal catched!\n");
		exit(EXIT_SUCCESS);
		}
	}
