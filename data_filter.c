
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv){
	int acc, k, fd_input, fd_output, buff_size, n_printable, n_printable_total, i, leftToRead;
	char *buffer, *filtered;
	ssize_t n_read_bytes, len, written;
	if (argc < 4){
		printf("ERROR: Missing Arguments\n");
		return -1;
	}
	//get the number
	acc = 0;
	while(isdigit(*argv[1])){
		acc *= 10;
		acc = acc + (*argv[1] - '0');
		argv[1]++;
	}
	//get the exponent
	if (*argv[1] == 'B' || *argv[1] == 'b')
		k = 1;
	else if (*argv[1] == 'K' || *argv[1] == 'k')
		k = 1024;
	else if (*argv[1] == 'M' || *argv[1] == 'm')
		k = 1024*1024;
	else if (*argv[1] == 'G' || *argv[1] == 'g')
		k = 1024*1024*1024;
	else{
		printf("ERROR: Illegal size letter\n");
		return -1;
	}

	//read from input file to the buffer
	fd_input = open(argv[2], O_RDWR);
	if (fd_input < 0){
		printf("ERROR while opening file: %s\n", strerror(errno));
		return -1;
	}
	fd_output = open(argv[3], O_RDWR | O_CREAT);
	if (fd_output < 0){
		printf("ERROR while opening file: %s\n", strerror(errno));
		close(fd_input);
		return -1;
	}

	//if request is less than 100K, than allocate 1k buffer
	//else allocate 4k buffer
	if(acc*k < 100*1024)
		buff_size = 1024;
	else
		buff_size = 4*1024;
	//if requested amount is less than default value, set to the requested



	buffer = (char*)malloc(sizeof(char)*buff_size);
	if (buffer == NULL){
		printf("ERROR: allocation failed\n");
		close(fd_input);
		close(fd_output);
		return -1;
	}
	filtered = (char*)malloc(sizeof(char)*buff_size);
	if (filtered == NULL){
		printf("ERROR: allocation failed\n");
		free(buffer);
		close(fd_input);
		close(fd_output);
		return -1;
	}

	n_read_bytes = 0;
	n_printable_total = 0;
	//read input file
	leftToRead =  acc*k;
	//while((len = read(fd_input, buffer, buff_size)) > 0 && (n_read_bytes + len) <= acc*k){
	while(leftToRead > 0){
		if(leftToRead > buff_size)
			len = read(fd_input, buffer, buff_size);
		else
			len = read(fd_input, buffer, leftToRead);
		//check if we at the end of file
		if (len == 0){
			lseek(fd_input, 0, SEEK_SET);
			if(leftToRead > buff_size)
				len = read(fd_input, buffer, buff_size);
			else
				len = read(fd_input, buffer, leftToRead);

		}
		if(len < 0 ){
			printf("Error reading from file: %s\n", strerror(errno));
			free(buffer);
			free(filtered);
			close(fd_input);
			close(fd_output);
			return -1;
		}
		//read
		if (len == 0){
			lseek(fd_input, 0, SEEK_SET);
			if(leftToRead > buff_size)
				len = read(fd_input, buffer, buff_size);
			else
				len = read(fd_input, buffer, leftToRead);

		}
		leftToRead = leftToRead - len;
		n_read_bytes += len;
		n_printable = 0;
		//filter the data
		for(i = 0; i < len; i++){
			if(buffer[i] >=20 || buffer[i] <= 126){
				filtered[n_printable] = buffer[i];
				n_printable++;
				n_printable_total++;
			}
		}
		//write data to output file
		written = write(fd_output, filtered, n_printable);
		if(written < 0){
			printf("Error writing to file: %s\n", strerror(errno));
			free(buffer);
			free(filtered);
			close(fd_input);
			close(fd_output);
			return -1;
		}
	}


	//print statistics
	printf("%d characters requested, %d characters read, %d are printable\n", (acc*k), n_read_bytes, n_printable_total);

	//free resources
	close(fd_input);
	close(fd_output);
	free(buffer);
	free(filtered);



	return 0;
}
