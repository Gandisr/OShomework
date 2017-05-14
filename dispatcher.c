#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <sys/wait.h>

#define FIFO_NAME_LENGTH 13+sizeof(unsigned long)

unsigned long totalCount = 0;

void signal_handler(int signum, siginfo_t* info, void* ptr){
	int fd;
	char fifoName[FIFO_NAME_LENGTH];
	pid_t pid;
	unsigned long count = 0;


	pid = (int) info->si_pid;
	sprintf(fifoName, "/tmp/counter_%d", pid);
	fd = open(fifoName, O_RDONLY, 0755);
	read(fd, &count, sizeof(unsigned long));
	close(fd);
	totalCount += count;
}




int main(int argc, char *argv[]){
	int Q, K, i, status, counts;
	char *args[6];
	struct stat inputStat;
	size_t size = 0;
	pid_t *pids;
	struct sigaction action;

	if(argc < 3){
		printf("missing args\n");
		return -1;
	}


	memset(&action, 0, sizeof(action));
	action.sa_sigaction = signal_handler;
	action.sa_flags = SA_SIGINFO;

	if(sigaction(SIGUSR1, &action, NULL) != 0 ){
		printf("Signal handle registration " "failed. %s\n", strerror(errno));
		return -1;
	}


	if(stat(argv[2], &inputStat) < 0){
		printf("ERROR: cannot open input file: %s\n", strerror(errno));
		return -1;
	}

	if(inputStat.st_size < 2*getpagesize())
		Q = 1;
	else{
		Q = inputStat.st_size / (2*getpagesize());
		if(Q > 16)
			Q = 16;
	}
	K = inputStat.st_size / Q;

	size = 0;
	pids = (int*)malloc(sizeof(int)*Q);
	for(i = 0; i < Q ; i++){
		pids[i] = fork();
		if(pids[i] < 0){
			printf("fork failed: %s\n", strerror(errno));
			return -1; // ERROR!
		}
		if(pids[i] == 0){
			//sons
			args[0] = "/counter.c";
			args[1] = argv[1];
			args[2] = argv[2];
			args[3] = (char*)malloc(sizeof(char)*64);
			sprintf(args[3], "%ld", K*i);
			args[4] = (char*)malloc(sizeof(char)*64);
			if(i == Q-1)
				sprintf(args[4], "%ld", inputStat.st_size - size);
			else{
				sprintf(args[4], "%ld", K);
				size =+ K;
			}
			args[5] = NULL;
			execv(args[0], args);
			//if reached here: failed launch
			printf("execv failed: %s\n", strerror(errno));
			return -1;
		}
	}
	//if reached here than proc is the father
	//wait for all sons to finish
	while(wait(&status) != -1);


	free(args[3]);
	free(args[4]);
	free(pids);

	return 0;
}


