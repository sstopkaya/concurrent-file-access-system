/*
* Server
*/

#define _POSIX_C_SOURCE 200809L

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
#include <ctype.h>

#define SIZE 1024
#define MAX_QUEUE_SIZE 100

typedef struct 
    {
    int items[MAX_QUEUE_SIZE]; //struct for queue
    int front;
    int rear;
    } Queue;

void splitCommand(char buf[SIZE], char fName[SIZE], char dir[SIZE]);
void signalHandler(int sig);
void logFile(pid_t pid);
void help(char* param, char fName[SIZE]);
void list(char fName[SIZE]);
void readF(char* file, int line, char fName[SIZE]);
void writeT(char* file, int ln, char *str, char fName[SIZE]);
void upload(char file[SIZE], char dir[SIZE], char fName[SIZE]);
void download(char* file, char dir[SIZE], char fName[SIZE]);
void killServer(int id, char fName[SIZE]);
void initializeQueue(Queue *q);
int isQueueEmpty(Queue *q);
int isQueueFull(Queue *q);
void enqueue(Queue *q, int pid);
int dequeue(Queue *q);
void printQueue(Queue *q);
void killHandler(int sig);
void queueFree(int id, char arg[SIZE]);

//global variables
int MAX_CHILD=0;
char filename[50]="log.txt";
int fdLog;
int shm_fd, shm_freeSpot, shmRead, shmServerPid, shmClient;
void *ptr, *ptr1, *ptrRead, *ptrSid, *ptrClient;
Queue q;
sem_t *semRead, *semCh;
int tempId;
pid_t allPids[SIZE];
int numClient=0;
char allFifos[SIZE][SIZE];
struct sigaction sa; 	/* struct for handling signals */

//main
int main(int argc, char *argv[])
	{
	//usage
	if (argc!=3 && strcmp(argv[0], "biboServer")!=0) 
		{
		fprintf(stderr, "Usage:\n./biboServer <dirname> <max. #ofClients>\n");
		exit(EXIT_FAILURE);
		}
    initializeQueue(&q);

	/*____________________________________________signal mask*/
    //signal operations
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
	// used to lock the fork operation while there is no available slot 
   semCh = sem_open("/semCh", O_CREAT, 0644, 1);
    if (semCh == SEM_FAILED)
    	{
        perror("sem_open");
        exit(EXIT_FAILURE);
    	}

    //create directory
    char dirname[SIZE];
    strcpy(dirname, argv[1]);	     
    struct stat st = {0};
    if (stat(dirname, &st) == -1) 
    	{
        mkdir(dirname, 0700);
    	}
	
	MAX_CHILD=atoi(argv[2]);

	// shared mem for connection
	// to avoid mulitple clients accessing the connection fifo
	shm_fd = shm_open("shm0", O_CREAT | O_RDWR, 0666);
	ftruncate(shm_fd, 4096);
	ptr = mmap(0, 4096, PROT_WRITE, MAP_SHARED, shm_fd, 0);
	sprintf(ptr, "%d", 0); //printf("---%d\n", atoi(ptr));

	// this shm is to specify free available slot
	//used in both server and client sides (tryConnect case)
	shm_freeSpot = shm_open("shm1", O_CREAT | O_RDWR, 0666);
	ftruncate(shm_freeSpot, 4096);
	ptr1 = mmap(0, 4096, PROT_WRITE, MAP_SHARED, shm_freeSpot, 0);
	sprintf(ptr1, "%d", MAX_CHILD); //printf("---started freeSpot:%d\n", atoi(ptr1));

	//to prompt server pid
	shmServerPid = shm_open("shmServerId", O_CREAT | O_RDWR, 0666);
	ftruncate(shmServerPid, 4096);
	ptrSid = mmap(0, 4096, PROT_WRITE, MAP_SHARED, shmServerPid, 0);
	sprintf(ptrSid, "%d", getpid()); //printf("---server id:%d\n", atoi(ptrSid));

	//for queue client
	shmClient = shm_open("shmServerId", O_CREAT | O_RDWR, 0666);
	ftruncate(shmClient, 4096);
	ptrClient = mmap(0, 4096, PROT_WRITE, MAP_SHARED, shmClient, 0);

	fprintf(stderr, "server started with pid : %d...\n", getpid());
	fprintf(stderr, "waiting for clients...\n");

	/*____________________________________________log file part*/
    fdLog=open(filename, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fdLog ==-1) 
    	{
        perror("log open:");
    	}
    if (close(fdLog)==-1)
    	{
    	perror("close log:");
    	}
	/*____________________________________________log file part*/
	mkfifo("/tmp/connectFifo", S_IRUSR | S_IWUSR | S_IWGRP);
	while(1)
		{		
		// connection fifo, gets process id from client
		char buf[200];
		int fd=open("/tmp/connectFifo", O_RDONLY);
		read(fd, buf, sizeof(buf));
		//printf("client connectFifo: %s\n", buf);

		// make connection fifo busy
		sprintf(ptr, "%d", 1); //printf("---after read connectshm:%d\n", atoi(ptr));
		close(fd);

		// try to avoid conflict
	    if (sem_wait(semCh) == -1) 
	    	{
	        perror("sem_wait");
	        exit(EXIT_FAILURE);
	    	}
		/*____________________________________________signal handler*/
		/* Signal Handler is called if SIGINT catched */
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

		// if no available spot
		if (atoi(ptr1)<=0)
			{
			fprintf(stderr, "Connection request PID %s... Que FULL!\n", buf);
		    //client added to queue
		    sprintf(ptrClient, "%d", atoi(buf)); 
		    //sprintf(ptrSid, "%d", getpid()); //printf("---server id:%d\n", atoi(ptrSid));
		    enqueue(&q, atoi(buf));
		   	//fprintf(stdout, "Enqueue: %d\n", atoi(buf));
		    //printQueue(&q);
			sprintf(ptr, "%d", 0); //printf("---after enqueue, in child, shm:%d\n", atoi(ptr));	    
			}
		else
			{
			if (isQueueEmpty(&q)==0)
				{
			    // remove the waiting client to work
				//printQueue(&q);
				dequeue(&q);
				//printQueue(&q);
				}
			++numClient;
			fprintf(stderr, "Client PID %s connected as “client0%d”\n", buf, numClient);
			sprintf(ptr1, "%d", atoi(ptr1)-1);
			//printf("--- connect freeSpot:%d numClient:%d\n", atoi(ptr1), numClient);

			// child process created for each client
			pid_t pid=fork();
			if (sem_post(semCh) == -1) 
		    	{
		        perror("sem_post");
		        exit(EXIT_FAILURE);
		    	}

			if (pid==0)
				{
		    	char buf1[200], fifoName[300];
		    	// connection fifo is free to use again
				sprintf(ptr, "%d", 0); //printf("---after fork, in child, shm:%d\n", atoi(ptr));
				
				// unique fifo name is created to data transfer
		    	sprintf(fifoName, "/tmp/fifo_%s", buf);
		    	//fprintf(stderr, "ch:%d\n", getpid());
		    	//printf("client fifosu:%s\n", fifoName);
				mkfifo(fifoName, S_IRUSR | S_IWUSR | S_IWGRP);
		    	while (1)
		    		{
		    		// getting the command from the client
				    int fd1=open(fifoName, O_RDONLY);
					read(fd1, buf1, sizeof(buf1));
					close(fd1);

					char str[300];
					fdLog=open(filename, O_WRONLY | O_APPEND);
				    sprintf(str, "Process id:%d command:%s\n", getpid(), buf1);
				    write(fdLog, str, strlen(str));
	    			close(fdLog);
					
					if (strcmp(buf1,"quit")==0)
						{
						fprintf(stdout, "---client0%d disconnected..\n", numClient);
						// free spot increase
						sprintf(ptr1, "%d", atoi(ptr1)+1);
						// give information to quit
						int f=open(fifoName, O_WRONLY);
					    write(f, "bye", strlen("bye")+1);
					    close(f);

					    logFile(getpid());
					    queueFree(atoi(ptrClient), argv[1]);
			
						//printf("---forkun ici freeSpot:%d numClient:%d\n", atoi(ptr1), numClient);
						break;
						}
					else if (strcmp(buf1,"killServer")==0)
						{
						fprintf(stdout, "kill signal from client0%d.. terminating...\n", numClient);
						// free spot increase
						sprintf(ptr1, "%d", atoi(ptr1)+1);
						killServer(getppid(), fifoName);
						//printf("---forkun ici freeSpot:%d numClient:%d\n", atoi(ptr1), numClient);
						break;
						}					
					else
						{
						//handle given commands
						splitCommand(buf1, fifoName, argv[1]);
						}
					}
				unlink(fifoName);
				_exit(EXIT_SUCCESS);
				}
			else
				{
				// store all child ids
				int idx=numClient-1;
				allPids[idx]=pid;
				//for (int i = 0; i < numClient; ++i) { fprintf(stderr, "-%d\n", allPids[i]); }
				}
			}
		}

    if (sem_unlink("/semCh") == -1) 
    	{
        perror("sem_unlink");
        exit(EXIT_FAILURE);
	    }
   	unlink("/tmp/connectFifo");
	fprintf(stderr, "bye\n");
	return 0;
	}

void queueFree(int id, char arg[SIZE])
	{
	int status;
	++numClient;
	fprintf(stderr, "Client PID %d connected as “client0%d”\n", id, numClient);
	sprintf(ptr1, "%d", atoi(ptr1)-1);
	//printf("--- connect freeSpot:%d numClient:%d\n", atoi(ptr1), numClient);

	// child process created for each client
	pid_t pid=fork();
	if (pid==0)
		{
    	char buf1[200], fifoName[300];
    	// connection fifo is free to use again
		sprintf(ptr, "%d", 0); //printf("---after fork, in child, shm:%d\n", atoi(ptr));
		
		// unique fifo name is created to data transfer
    	sprintf(fifoName, "/tmp/fifo_%d", id);
    	//fprintf(stderr, "ch:%d\n", getpid());
    	//printf("client fifosu:%s\n", fifoName);
		mkfifo(fifoName, S_IRUSR | S_IWUSR | S_IWGRP);
    	while (1)
    		{
    		// getting the command from the client
		    int fd1=open(fifoName, O_RDONLY);
			read(fd1, buf1, sizeof(buf1));
			close(fd1);

			char str[300];
			fdLog=open(filename, O_WRONLY | O_APPEND);
		    sprintf(str, "Process id:%d command:%s\n", getpid(), buf1);
		    write(fdLog, str, strlen(str));
			close(fdLog);

			if (strcmp(buf1,"quit")==0)
				{
				fprintf(stdout, "---client0%d disconnected..\n", numClient);
				// free spot increase
				sprintf(ptr1, "%d", atoi(ptr1)+1);
				// give information to quit
				int f=open(fifoName, O_WRONLY);
			    write(f, "bye", strlen("bye")+1);
			    close(f);
			    logFile(getpid());
				//printf("---forkun ici freeSpot:%d numClient:%d\n", atoi(ptr1), numClient);
				break;
				}
			else if (strcmp(buf1,"killServer")==0)
				{
				fprintf(stdout, "kill signal from client0%d.. terminating...\n", numClient);
				// free spot increase
				sprintf(ptr1, "%d", atoi(ptr1)+1);
				killServer(getppid(), fifoName);
				//printf("---forkun ici freeSpot:%d numClient:%d\n", atoi(ptr1), numClient);
				break;
				}					
			else
				{
				//handle given commands
				splitCommand(buf1, fifoName, arg);
				}
			}
		unlink(fifoName);
		_exit(EXIT_SUCCESS);
		}
	else
		{
		wait(&status);
		}
	}

void initializeQueue(Queue *q) 
    {
    q->front = -1;
    q->rear = -1;
    }

int isQueueEmpty(Queue *q)
    {
    if (q->rear == -1)
        return 1;
    else
        return 0;
    }

int isQueueFull(Queue *q)
    {
    if (q->rear == MAX_QUEUE_SIZE - 1)
    	{
        return 1;
    	}
    else
    	{
        return 0;
    	}
    }

void enqueue(Queue *q, int pid)
    {
    if (isQueueFull(q)) 
        {
        //fprintf(stdout, "Queue is full\n");
        return;
        }
    else
        {
        if (q->front == -1)
            q->front = 0;
        q->rear++;
        q->items[q->rear] = pid;
        }
    }

int dequeue(Queue *q)
    {
    int pid;
    if (isQueueEmpty(q))
        {
        //fprintf(stdout, "Queue is empty\n");
        return -1;
        }
    else
        {
        pid = q->items[q->front];
        q->front++;
        if (q->front > q->rear)
            {
            q->front = -1;
            q->rear = -1;
            }
        return pid;
        }
    }

void printQueue(Queue *q) 
	{
    int i;
    if (isQueueEmpty(q))
    	{
        printf("Queue is empty\n");
        return;
    	}
    else 
   		{
        printf("Queue members: ");
        for (i = q->front; i <= q->rear; i++)
        	{
            printf("%d ", q->items[i]);
        	}
        printf("\n");
    	}
	}

// command execution function
void splitCommand(char buf[SIZE], char fName[SIZE], char dir[SIZE])
	{
	//fprintf(stderr, "komut:%s\n", buf);
	char str[SIZE][SIZE];
	int counterCmnd=0;
	char bufCopy[SIZE];
	strcpy(bufCopy,buf);
	int flag=0;
	for (int i = 0; i < strlen(buf); ++i)
		{
		if (buf[i]==' ')
			{
			flag=1;
			}
		}
	char* token=strtok(buf," ");
	while (token!=NULL)
		{
		sprintf(str[counterCmnd], "%s", token);
		counterCmnd++;
		token=strtok(NULL," ");
		}
	//fprintf(stderr, "counterCmnd:%d\n", counterCmnd);
	if (strcmp(str[0],"help")==0)
		{
		if (flag==0)
			{
			help("x", fName);
			}
		else
			{
			help(str[1], fName);
			}
		}
	else if (strcmp(str[0],"list")==0)
		{
		list(fName);
		}
	else if (strcmp(str[0],"readF")==0)
		{
		if (counterCmnd==2)
			{
			readF(str[1], 0, fName);				
			}
		else if (counterCmnd==3)
			{
			readF(str[1], atoi(str[2]), fName);				
			}
		else
			{
			int f=open(fName, O_WRONLY);
		    write(f, "readF <file> <line>", strlen("readF <file> <line>")+1);
		    close(f);
			}
		}
	else if (strcmp(str[0],"writeT")==0)
		{
		// split arguments to understand parts of it

		//fprintf(stderr, "bufCopy:%s\n", bufCopy);
		char str1[SIZE][SIZE];
		int counterCmnd1=0;
		char* token=strtok(bufCopy,"\"");
		while (token!=NULL)
			{
			sprintf(str1[counterCmnd1], "%s", token);
			//fprintf(stderr, "%s\n", str1[counterCmnd1]);
			counterCmnd1++;
			token=strtok(NULL,"\"");
			}
		char str2[SIZE][SIZE];
		int counterCmnd2=0;
		char* token1=strtok(str1[0]," ");
		while (token1!=NULL)
			{
			sprintf(str2[counterCmnd2], "%s", token1);
			//fprintf(stderr, "%s\n", str2[counterCmnd2]);
			counterCmnd2++;
			token1=strtok(NULL, " ");
			}
		if (counterCmnd<2)
			{
			int f=open(fName, O_WRONLY);
		    write(f, "writeT <file> <line> <string>", strlen("writeT <file> <line> <string>")+1);
		    close(f);
			}
		else if (counterCmnd2==2)
			{
			writeT(str2[1], 0, str1[1], fName);
			}
		else if (counterCmnd2==3)
			{
			writeT(str2[1], atoi(str2[2]), str1[1], fName);
			}
		else
			{
			int f=open(fName, O_WRONLY);
		    write(f, "writeT <file> <line> <string>", strlen("writeT <file> <line> <string>")+1);
		    close(f);
			}			
		}
	else if (strcmp(str[0],"upload")==0)
		{
		if (counterCmnd==2)
			{
			upload(str[1], dir, fName);
			}
		else
			{
			int f=open(fName, O_WRONLY);
		    write(f, "upload <file>", strlen("upload <file>")+1);
		    close(f);
			}
		}
	else if (strcmp(str[0],"download")==0)
		{
		if (counterCmnd==2)
			{
			download(str[1], dir, fName);
			}
		else
			{
			int f=open(fName, O_WRONLY);
		    write(f, "download <file>", strlen("download <file>")+1);
		    close(f);			
			}
		}
	else
		{
		int f=open(fName, O_WRONLY);
	    write(f, "the command is unsupported..\n", strlen("the command is unsupported..\n")+1);
	    close(f);			
		}
	}

void help(char* param, char fName[SIZE])
	{
	char result[SIZE];	
	if (strcmp(param,"list")==0)
		{
		strcpy(result, "list\n");
		strcpy(result+strlen(result), "display the list of files in Servers directory\n");
		}
	else if (strcmp(param,"readF")==0)
		{
		strcpy(result, "readF <file> <line #>\n");
		strcpy(result+strlen(result), "display the #th line of the <file>,\n");
		strcpy(result+strlen(result), "returns with an error if <file> does not exists\n");
		}
	else if (strcmp(param,"writeT")==0)
		{
		strcpy(result, "writeT <file> <line #> <string> :\n");
		strcpy(result+strlen(result), "request to write the content of “string” to the #th line the <file>,\n");
		strcpy(result+strlen(result), "if the line # is not given\n");
		}
	else if (strcmp(param,"upload")==0)
		{
		strcpy(result, "upload <file>\n");
		strcpy(result+strlen(result), "uploads the file from the current working directory of client to the Servers directory\n");
		}
	else if (strcmp(param,"download")==0)
		{
		strcpy(result, "download <file>\n");
		strcpy(result+strlen(result), "request to receive <file> from Servers directory to client side\n");
		}
	else if (strcmp(param,"quit")==0)
		{
		strcpy(result, "quit\n");
		strcpy(result+strlen(result), "request to Server side log file and quits\n");
		}
	else if (strcmp(param,"killServer")==0)
		{
		strcpy(result, "killServer\n");
		strcpy(result+strlen(result), "Sends a kill request to the Server\n");
		}
	else
		{
		strcpy(result, "Available comments are :\n");
		strcpy(result+strlen(result), "help, list, readF, writeT, upload, download, quit, killServer\n");
		}
	int f=open(fName, O_WRONLY);
    write(f, result, strlen(result)+1);
    close(f);
	}

void list(char fName[SIZE])
	{
    int f = open(fName, O_WRONLY);
    int status;
	pid_t pid=fork();		/* create a new process */
	if (pid<0)
		{
		perror("fork:");
		}
	if (pid==0) 			/* child process handle the given command */
		{
		if (dup2(f, STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }

		if (execl("/bin/sh", "sh", "-c", "ls -l", (char *) 0)==-1) /*command done*/
			{
			perror("execl:");
			}
		_exit(EXIT_SUCCESS);
		}
	else
		{
		if (waitpid(pid, &status, 0)==-1) 	/* parent is waiting */
			{
			perror("close waitpid:");
			}
		close(f);
		}
	}

void readF(char* file, int line, char fName[SIZE])
	{
	//fprintf(stderr, "file:%s - line:%d\n", file, line);
	char result[SIZE], temp[SIZE];
	FILE* fpw = fopen(file, "a");
	fprintf(fpw, "\n");
	fclose(fpw);
	FILE* fp = fopen(file, "r");
    if (fp == NULL) 
    	{
		perror("open file:");
        exit(EXIT_FAILURE);
    	}
    if (line!=0)
    	{
	    for (int i = 1; i <= line; i++)
	    	{
	        if (fgets(temp, sizeof(temp), fp) == NULL)
	        	{
	        	fprintf(stderr, "temp:%s\n", temp);
	            perror("read file:");
	            exit(EXIT_FAILURE);
	        	}
	        if (i==line)
	        	{
	        	// i found the line
	            strcpy(result, temp);
	            //fprintf(stderr, "result:%s\n", result);
	        	}
	    	}
	  	int f=open(fName, O_WRONLY);
	    write(f, result, strlen(result)+1);
	    close(f);
		}
	else
		{
		// try to avoid conflicts
		semRead = sem_open("semReadF", O_CREAT, 0644, 1);
		sem_wait(semRead);
		while (fgets(temp, sizeof(temp), fp))
			{
      		int f=open(fName, O_WRONLY);
		    write(f, temp, strlen(temp)+1);
		    //fprintf(stderr, "%s", temp);
		    close(f);
		    sem_post(semRead);
   			}
   		sem_close(semRead);
		sem_unlink("semReadF");

		// to break the loop of the client side
		int f=open(fName, O_WRONLY);
	    write(f, "end", strlen("end")+1);
	    close(f);
		}
	fclose(fp);
	}

//semaphore should be added
void writeT(char* file, int ln, char *str, char fName[SIZE])
	{
	//fprintf(stderr, "file:%s ln:%d str:%s\n", file, ln, str);
	FILE *fp;
    int counter = 0;
    char line[SIZE];
    int foundLine = 0;

	//check if it already exists
    fp = fopen(file, "r+");
    if (fp == NULL) 
    	{
        fp = fopen(file, "w");
        if (fp == NULL)
        	{
            perror("Creating file:");
            exit(EXIT_FAILURE);
        	}
    	}
	    while (fgets(line, sizeof(line), fp)) 
	    	{
	        counter++;
	        if (counter == ln) 
	        	{
	            foundLine = 1;
	            // move the file pointer go back to begining of the line
	            fseek(fp, -strlen(line), SEEK_CUR); 
	            fprintf(fp, "%s\n", str);
	            break;
	        	}
	    	}
	    // write to the end of file
	    if (!foundLine || ln==0) 
	    	{
	        fprintf(fp, "%s\n", str);
	    	}
    fclose(fp);

    // give information to the client
    char res[SIZE];
   	int f=open(fName, O_WRONLY);
   	sprintf(res, "%s <- that string was written to the given file.", str);
    write(f, res, strlen(res)+1);
    close(f);
	}

void upload(char file[SIZE], char dir[SIZE], char fName[SIZE])
	{
	char buf[SIZE];
	int fd=open(file, O_RDONLY);
	ssize_t bytes_read;
	int size=0;

	// to find size of the file
	while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) 
		{
        size += bytes_read;
    	}
	close(fd);

    char arg[SIZE];
    int status;
	pid_t pid=fork();
	if (pid<0)
		{
		perror("fork:");
		}
	if (pid==0)
		{
		// copy the file from the client dir to the server dir
	    sprintf(arg, "%s/%s", dir, file);
	    if (execl("/bin/cp", "cp", file, arg, (char *) 0) < 0) 
	    	{
	        perror("Failed to upload file.");
	        exit(EXIT_FAILURE);
	    	} 
	    else 
	    	{
	        perror("File uploaded successfully.");
	        exit(EXIT_SUCCESS);
	    	}
		_exit(EXIT_SUCCESS);
		}
	else
		{
		if (waitpid(pid, &status, 0)==-1) 
			{
			perror("close waitpid:");
			}

		char str[SIZE];
		int f = open(fName, O_WRONLY);
		sprintf(str, "file transfer request received. Beginning file transfer:\n%d bytes transferred", size);
		write(f, str, strlen(str)+1);
		close(f);
		}
   	}

void download(char* file, char dir[SIZE], char fName[SIZE])
	{
	char buf[SIZE];
	ssize_t bytes_read;
	int size=0;
	char filepath[SIZE];

	//create path name
	sprintf(filepath, "%s/%s", dir, file);

	//to find size of the file
	int fd=open(filepath, O_RDONLY);
	while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) 
		{
        size += bytes_read;
    	}
	close(fd);

    char arg[SIZE];
    char cwd[SIZE];
    int status;
	pid_t pid=fork();
	if (pid<0)
		{
		perror("fork:");
		}
	if (pid==0) 
		{
		// copy the file from the server dir to the client dir
	    sprintf(arg, "%s/", getcwd(cwd, sizeof(cwd)));
	    if (execl("/bin/cp", "cp", filepath, arg, (char *) 0) < 0) 
	    	{
	        perror("Failed to download file.");
	        exit(EXIT_FAILURE);
	    	} 
	    else 
	    	{
	        perror("File download successfully.");
	        exit(EXIT_SUCCESS);
	    	}
		_exit(EXIT_SUCCESS);
		}
	else
		{
		if (waitpid(pid, &status, 0)==-1) 
			{
			perror("close waitpid:");
			}

		char str[SIZE];
		int f = open(fName, O_WRONLY);
		sprintf(str, "file transfer request received. Beginning file transfer:\n%d bytes transferred", size);
		write(f, str, strlen(str)+1);
		close(f);
		}
	}

//wait for all childs to be terminated and then kill them and kill itself
void killServer(int id, char fName[SIZE])
	{
	int f=open(fName, O_WRONLY);
	write(f, "bye", strlen("bye")+1);
	close(f);

	int status;
//	fprintf(stderr, "numClient:%d\n", numClient);
//	for (int i = 0; i < numClient; ++i) { fprintf(stderr, ">%d\n", allPids[i]); }
	for (int i = 0; i < numClient; ++i)
		{
		waitpid(allPids[i], &status, 0);
		}
	for (int i = 0; i < numClient; ++i)
		{
		kill(allPids[i], SIGTERM);
		}
	kill(id, SIGTERM);
	}

/*kill all process to avoid zombies*/
void killHandler(int sig)
	{
	int status;
	for (int i = 0; i < numClient; ++i)
		{
		if(waitpid(allPids[i], &status, 0)==-1)	/*wait for all process termination*/
			{
			perror("waitpid:");
			}
		}	
	for (int i = 0; i < numClient; ++i)
		{
		kill(allPids[i], sig);	/*all process are killed*/
		}
	if (close(fdLog)==-1)			/*log file is closed*/
		{
		perror("close:");
		}
	exit(EXIT_SUCCESS);
	}

/* signal handler function */
void signalHandler(int sig)
	{	
	if(sig == SIGINT)
		{
		fprintf(stderr, "\n\nSIGINT signal catched!\n");
		killHandler(0);
		exit(EXIT_SUCCESS);
		}
	else if(sig == SIGTERM)
		{
		fprintf(stderr, "\nSIGTERM signal catched!\n");
		killHandler(0);
		exit(EXIT_SUCCESS);
		}
	else if(sig == SIGTSTP)
		{
		fprintf(stderr, "\nSIGTSTP signal catched!\n");
		killHandler(0);
		exit(EXIT_SUCCESS);
		}
	else if(sig == SIGKILL)
		{
		fprintf(stderr, "\nSIGKILL signal catched!\n");
		killHandler(0);
		exit(EXIT_SUCCESS);
		}
	}

/*log file keeps the information about processes*/
void logFile(pid_t pid)
	{
	char str[SIZE];
	fdLog=open(filename, O_WRONLY | O_APPEND);
    if (fdLog==-1)
    	{
    	perror("open:");
    	}
    sprintf(str, "Process id:%d terminated - client%d is disconnected\n", pid, numClient);
    if (write(fdLog, str, strlen(str))==-1)
    	{
    	perror("log write:");
    	}
    if (close(fdLog)==-1)
    	{	
    	perror("close:");
    	}
	}

