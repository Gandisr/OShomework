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


int main(int argc, char *argv[]){
	int fd, fifofd, i;
	char *arr, *ptr;
	pid_t pid, dis_pid;
	char fifoFilename[FIFO_NAME_LENGTH];
	long offset, size;
	unsigned long count = 0;


	offset = strtol(argv[3], &ptr, 10);
	size = strtol(argv[4], &ptr, 10);


	arr = (char*)malloc(sizeof(char)*size);
	fd = open(argv[2], O_RDONLY, 0755);
	if(fd < 0){
		printf("ERROR: cannot open file: %s\n", strerror(errno));
		return -1;
	}
	lseek(fd, offset, SEEK_SET);
	read(fd, arr, size);
	close(fd);
	//iterate over the chunk
	for(i = 0; i < size; i++){
		if(arr[i] == *(argv[1]))
			count++;
	}
	pid = getpid();
	sprintf(fifoFilename, "/tmp/counter_%d", pid);
	if (mkfifo(fifoFilename, 0755) < 0){
		printf("ERROR: cannot open pipe file: %s\n", strerror(errno));
		return -1;
	}
	fifofd = open(fifoFilename,  O_WRONLY);
	write(fifofd, &count, sizeof(unsigned long));
	dis_pid = getppid();
	kill(dis_pid, SIGUSR1);

	sleep(1);
	close(fifofd);
	unlink(fifoFilename);

	return 0;
}
