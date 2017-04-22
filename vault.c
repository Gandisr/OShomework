#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdbool.h>
#include <unistd.h>


#define _FILE_OFFSET_BITS 64
#define NAME_LENGH 257
#define METADATA_LENGH (sizeof(ssize_t)+sizeof(ssize_t)+sizeof(time_t)+sizeof(time_t)+sizeof(short))
#define FAT_LINE_LENGH ((sizeof(char)*NAME_LENGH)+sizeof(ssize_t)+sizeof(mode_t)+sizeof(time_t)+3*(sizeof(off_t)+sizeof(ssize_t)))
#define START_OF_DATA METADATA_LENGH + 100*FAT_LINE_LENGH
#define BUFFERSIZE 4096

typedef struct vaultMetadata vaultMetadata;
typedef struct fatLine fatLine;


char* toLowerCase(char* str);

void fetchFile(int fd,  fatLine* fl);

void getFatLine(fatLine *fl, int fd, int line);

void getMetadata(vaultMetadata* md, int fd);

void writeMetadata(vaultMetadata* md, int fd);

void sizeToString(ssize_t s, char* str);

short isInVault(int fd, char* filename);

ssize_t countEmptySpace(int fd, ssize_t cap);

off_t findNextSpace(int fd, off_t cap);

ssize_t fillBuffer(int fd, ssize_t* buffer, off_t* offset, ssize_t bufferCap, off_t offCap);

short findLineOfOffset(int fd, vaultMetadata* md, fatLine *fl, off_t off);

void placeFile(int fd, char* filename, struct stat inputStat, off_t offBlock1, ssize_t sizeBlock1,
		off_t offBlock2, ssize_t sizeBlock2, off_t offBlock3, ssize_t sizeBlock3);

void moveBlockBack(int fd, fatLine *fl, ssize_t size, short blockNum);

void updateFATLine(int fd, fatLine *fl, vaultMetadata* md, ssize_t size, short blockNum);

int findLineIndex(int fd, fatLine *fl, vaultMetadata* md);

void measureTime(struct timeval start);

ssize_t getTotalFilesSize(int fd, vaultMetadata* md);

double calcFragmentRatio(int fd, vaultMetadata* md);

off_t findMinOffset(int fd, vaultMetadata* md);

off_t findMaxOffset(int fd, vaultMetadata* md);

struct vaultMetadata{
	ssize_t overallSize;
	time_t createTime;
	time_t modTime;
	short numfiles;
};

struct fatLine{
	char filename[NAME_LENGH];
	ssize_t filesize;
	mode_t protMode;
	time_t insertTime;
	off_t offBlock1;
	ssize_t sizeBlock1;
	off_t offBlock2;
	ssize_t sizeBlock2;
	off_t offBlock3;
	ssize_t sizeBlock3;
};

void measureTime(struct timeval start){
	struct timeval end;
	long seconds, useconds;
	double mtime;

	gettimeofday(&end, NULL);
	seconds  = end.tv_sec  - start.tv_sec;
	useconds = end.tv_usec - start.tv_usec;

	mtime = ((seconds) * 1000 + useconds/1000.0);
	printf("Elapsed time: %.3f milliseconds\n", mtime);
}

char* toLowerCase(char* str){
	int i = 0;
	char* newStr = (char*)malloc(sizeof(char)*strlen(str)+1);
	while(str[i] > 0){
		newStr[i] = tolower(str[i]);
		i++;
	}
	newStr[strlen(str)] = '\0';

	return newStr;
}

void placeFile(int fd, char* filename, struct stat inputStat, off_t offBlock1, ssize_t sizeBlock1,
		off_t offBlock2, ssize_t sizeBlock2, off_t offBlock3, ssize_t sizeBlock3){

	int inputFd;
	int index;
	char charBuff;
	struct timeval current;
	char inputBuffer[BUFFERSIZE];
	ssize_t leftToRead, len;
	vaultMetadata* meta;

	meta = (vaultMetadata*)malloc(sizeof(vaultMetadata));
	getMetadata(meta, fd);

	//update metedata
	gettimeofday(&current, NULL);
	if(offBlock2 == 0)
		meta->overallSize += inputStat.st_size + 16;
	else if (offBlock3 == 0)
		meta->overallSize+= inputStat.st_size + 32;
	else
		meta->overallSize+= inputStat.st_size + 48;
	meta->modTime = current.tv_sec;
	meta->numfiles++;

	writeMetadata(meta, fd);

	index = 1;
	//look for an empty space at FAT
	lseek(fd, METADATA_LENGH, SEEK_SET);
	read(fd, &charBuff, 1);
	while(charBuff != 0){
		lseek(fd, METADATA_LENGH + index*FAT_LINE_LENGH, SEEK_SET);
		read(fd, &charBuff, 1);
		index++;
	}
	//write data to FAT

	lseek(fd, METADATA_LENGH + (index-1)*FAT_LINE_LENGH, SEEK_SET);
	write(fd, filename, sizeof(char)*257); //write name
	write(fd, &inputStat.st_size, sizeof(ssize_t));//write size
	write(fd, &inputStat.st_mode, sizeof(mode_t)); //write mode
	write(fd, &current, sizeof(time_t)); //write time
	write(fd, &offBlock1, sizeof(off_t));
	write(fd, &sizeBlock1, sizeof(ssize_t));
	write(fd, &offBlock2, sizeof(off_t));
	write(fd, &sizeBlock2, sizeof(ssize_t));
	write(fd, &offBlock2, sizeof(off_t));
	write(fd, &sizeBlock2, sizeof(ssize_t));

	//write data to blocks:

	inputFd = open(filename, O_RDWR);
	if(inputFd < 0){
		printf("ERROR: cannot open file: %s\n", strerror(errno));
		return;
	}

	//write block1

	lseek(fd, START_OF_DATA+offBlock1, SEEK_SET);
	write(fd, "<<<<<<<<", 8);
	leftToRead = sizeBlock1-16;
	while(leftToRead > 0){
		if (leftToRead < BUFFERSIZE)
			len = read(inputFd, inputBuffer, leftToRead);
		else
			len = read(inputFd, inputBuffer, BUFFERSIZE);
		if(len < 0 ){
			printf("Error reading from file: %s\n", strerror(errno));
			return;
		}
		leftToRead -= len;
		write(fd, inputBuffer, len);
	}
	write(fd, ">>>>>>>>", 8);


	//write block 2
	if(offBlock2 != 0){
		lseek(fd, START_OF_DATA+offBlock2, SEEK_SET);
		write(fd, "<<<<<<<<", 8);
		leftToRead = sizeBlock2-16;
		while(leftToRead > 0){
			if (leftToRead < BUFFERSIZE)
				len =read(inputFd, inputBuffer, leftToRead);
			else
				len =read(inputFd, inputBuffer, BUFFERSIZE);
			if(len < 0 ){
					printf("Error reading from file: %s\n", strerror(errno));
					return;
			}
			leftToRead -= len;
			write(fd, inputBuffer, len);
		}
		write(fd, ">>>>>>>>", 8);
	}
	//write block 3
		if(offBlock3 != 0){
		lseek(fd, START_OF_DATA+offBlock3, SEEK_SET);
		write(fd, "<<<<<<<<", 8);
		leftToRead = sizeBlock3 -16;
		while(leftToRead > 0){
			if (leftToRead < BUFFERSIZE)
				len =read(inputFd, inputBuffer, leftToRead);
			else
				len =read(inputFd, inputBuffer, BUFFERSIZE);
			if(len < 0 ){
					printf("Error reading from file: %s\n", strerror(errno));
					return;
			}
			leftToRead -= len;
			write(fd, inputBuffer, len);
		}
		write(fd, ">>>>>>>>", 8);
	}
	free(meta);
	close(inputFd);
}

ssize_t fillBuffer(int fd, ssize_t* buffer, off_t* offset, ssize_t bufferCap, off_t offCap){
	char charBuff;
	off_t off = 0, len = 0;
	ssize_t size = 0;

	read(fd, &charBuff, 1);
	//start with blank
	if(charBuff == 0){
		size = countEmptySpace(fd, bufferCap);
		if (size > 16){
			*(buffer) = size;
			*(offset) = 0;
			return size;
		}
	}

	while(size <=16){
		//look for the next space
		len = findNextSpace(fd, offCap);
		if(len == 0)
			return 0;

		off += len;
		//measure the next space
		size = countEmptySpace(fd, bufferCap);
		//check if there is room
		if(size + off > offCap)
			return 0;
	}

	//enter the found offset and size of empty space
	*(buffer) = size;
	*(offset) = off;

	return (size+off);
}

long getEnterdSize(char* str){
	int acc = 0, k;
	while(isdigit(*str)){
		acc *= 10;
		acc = acc + (*str - '0');
		str++;
	}
	//get the exponent
	if (*str == 'B' || *str == 'b')
		k = 1;
	else if (*str == 'K' || *str == 'k')
		k = 1024;
	else if (*str == 'M' || *str == 'm')
		k = 1024*1024;
	else if (*str == 'G' || *str == 'g')
		k = 1024*1024*1024;
	else{
		return -1;
	}

	return acc*k;
}

ssize_t countEmptySpace(int fd, ssize_t cap){
	char buffChar = 0;
	ssize_t space = 0;

	if (cap == 0)
		return 0;
	read(fd, &buffChar, 1);

	//already has data
	if(buffChar != 0)
		return 0;
	space++;

	while(buffChar == 0){
		read(fd, &buffChar, 1);
		space++;
		if(space == cap)
			return space;
	}
	return (space + 1);
}

void getFatLine(fatLine *fl, int fd, int line){

	lseek(fd,METADATA_LENGH + (line-1)*FAT_LINE_LENGH, SEEK_SET);
	read(fd, fl->filename, NAME_LENGH); // name
	read(fd, &(fl->filesize), sizeof(ssize_t)); //size
	read(fd, &(fl->protMode), sizeof(mode_t)); //mode
	read(fd, &(fl->insertTime), sizeof(time_t)); //time
	read(fd, &(fl->offBlock1), sizeof(off_t));
	read(fd, &(fl->sizeBlock1), sizeof(ssize_t));
	read(fd, &(fl->offBlock2), sizeof(off_t));
	read(fd, &(fl->sizeBlock2), sizeof(ssize_t));
	read(fd, &(fl->offBlock3), sizeof(off_t));
	read(fd, &(fl->sizeBlock3), sizeof(ssize_t));
}

void getMetadata(vaultMetadata* md, int fd){
	lseek(fd, sizeof(ssize_t), SEEK_SET);
	read(fd, &(md->overallSize), sizeof(ssize_t));
	read(fd, &(md->createTime), sizeof(time_t));
	read(fd, &(md->modTime), sizeof(time_t));
	read(fd, &(md->numfiles), sizeof(short));
}

void writeMetadata(vaultMetadata* md, int fd){
	lseek(fd, sizeof(ssize_t), SEEK_SET);
	write(fd, &(md->overallSize), sizeof(ssize_t));
	write(fd, &(md->createTime), sizeof(time_t));
	write(fd, &(md->modTime), sizeof(time_t));
	write(fd, &(md->numfiles), sizeof(short));
}

void printFatLine(fatLine *fl){
	char* str = (char*)malloc(sizeof(char)*5);
	sizeToString(fl->filesize, str);
	printf("%s\t%s\t%d\t%s\n", fl->filename, str,fl->protMode ,ctime(&fl->insertTime));
	free(str);
}

void sizeToString(ssize_t s, char* str){
	char k;
	if(s < 1024){
		k = 'B';
		sprintf(str, "%d%c", s, k);
	}
	else if (s < 1024*1024){
		k = 'K';
		sprintf(str, "%d%c", s / 1024, k);
	}
	else if (s < 1024 * 1024 * 1024){
		k = 'M';
		sprintf(str, "%d%c", s / (1024*1024), k);
	}
	else{
		k = 'G';
		sprintf(str, "%d%c", s / (1024*1024*1024), k);
	}
}

short isInVault(int fd, char* filename){
	int i, index;
	short totalFiles;
	fatLine* fl = (fatLine*)malloc(sizeof(fatLine));
	lseek(fd, METADATA_LENGH-sizeof(short), SEEK_SET);
	read(fd, &totalFiles, sizeof(short));

	index = 0;
	for(i = 1; i <= totalFiles; i++){
		getFatLine(fl, fd, i + index);
		while(fl->filesize == 0){
			index++;
			getFatLine(fl, fd, i + index);
		}
		if(strcmp(fl->filename, filename) == 0)
			return (i+index);
	}
	free(fl);
	return 0;
}

void fetchFile(int fd,  fatLine* fl){

	char charBuffer[BUFFERSIZE];
	int newFile;
	ssize_t leftToRead, len;

	newFile = open(fl->filename, O_RDWR|O_CREAT, fl->protMode);
	if(newFile < 0){
		printf("Error: cannot create new file: %s", strerror(errno));
		return;
	}
	//write block1
	lseek(fd, START_OF_DATA+fl->offBlock1+8, SEEK_SET);
	leftToRead = fl->sizeBlock1-16;
	while(leftToRead > 0){
		if (leftToRead < BUFFERSIZE)
			len =read(fd, charBuffer, leftToRead);
		else
			len =read(fd, charBuffer, BUFFERSIZE);
		if(len < 0 ){
			printf("Error reading from vault: %s\n", strerror(errno));
			return;
		}
		leftToRead -= len;

		write(newFile, charBuffer, len);
	}

	//write block2
	if(fl->offBlock2 != 0){
		lseek(fd, START_OF_DATA+fl->offBlock2+8, SEEK_SET);
		leftToRead = fl->sizeBlock2-16;
		while(leftToRead > 0){
			if (leftToRead < BUFFERSIZE)
				len =read(fd, charBuffer, leftToRead);
			else
				len =read(fd, charBuffer, BUFFERSIZE);
			if(len < 0 ){
				printf("Error reading from vault: %s\n", strerror(errno));
				return;
			}
			leftToRead -= len;

			write(newFile, charBuffer, len);
		}
	}

	//write block3
	if(fl->offBlock3 != 0){
		lseek(fd, START_OF_DATA+fl->offBlock3+8, SEEK_SET);
		leftToRead = fl->sizeBlock3-16;
		while(leftToRead > 0){
			if (leftToRead < BUFFERSIZE)
				len = read(fd, charBuffer, leftToRead);
			else
				len = read(fd, charBuffer, BUFFERSIZE);
			if(len < 0 ){
				printf("Error reading from vault: %s\n", strerror(errno));
				return;
			}
			leftToRead -= len;

			write(newFile, charBuffer, len);
		}
	}


}

off_t findNextSpace(int fd, off_t cap){
	char buffChar;
	int delimiterCount;
	off_t off = 0;
	delimiterCount = 0;
	buffChar = 1;
	while(delimiterCount < 8 || buffChar != 0){
		read(fd, &buffChar, 1);
		off++;
		if(buffChar == '>')
			delimiterCount++;
		else if(buffChar != 0)
			delimiterCount = 0;

	}
	read(fd, &buffChar, 1);


	return off;

}

short findLineOfOffset(int fd, vaultMetadata* md, fatLine *fl, off_t off){
	int i, index;
	index = 0;
	for(i = 1; i <= md->numfiles; i++){
		getFatLine(fl, fd, i + index);
		while(fl->filesize == 0){
			index++;
			getFatLine(fl, fd, i + index);
		}
		if(fl->offBlock1 == off)
			return 1;
		if(fl->offBlock2 == off)
			return 2;
		if(fl->offBlock3 == off)
			return 3;
	}

	return -1;
}

void moveBlockBack(int fd, fatLine *fl, ssize_t size, short blockNum){
	char charBuffer[BUFFERSIZE];
	ssize_t leftToRead, len;
	off_t off;
	int i;

	//get the off and size
	if(blockNum == 1){
		off = fl->offBlock1;
		leftToRead = fl->sizeBlock1+16;
	}
	else if(blockNum == 2){
		off = fl->offBlock2;
		leftToRead = fl->sizeBlock2+16;
	}
	else{
		off = fl->offBlock3;
		leftToRead = fl->sizeBlock3+16;
	}

	//move the block <size> bytes back, using buffer (buffersize at a time)
	lseek(fd, START_OF_DATA+off, SEEK_SET);
	while(leftToRead > 0){
		if (leftToRead < BUFFERSIZE)
			len = read(fd, charBuffer, leftToRead);
		else
			len = read(fd, charBuffer, BUFFERSIZE);
		if(len < 0 ){
			printf("Error reading from vault: %s\n", strerror(errno));
			return;
		}

		leftToRead -= len;

		lseek(fd, START_OF_DATA+off-size, SEEK_SET);
		write(fd, charBuffer, len);
		lseek(fd, START_OF_DATA+off+len, SEEK_SET);
		off += len;
	}
	lseek(fd, START_OF_DATA+off-size, SEEK_SET);

	//clear
	for(i = 0; i < size; i++)
		write(fd, "\0", 1);
}

int findLineIndex(int fd, fatLine *fl, vaultMetadata* md){
	int i, index;
	fatLine *line;

	line = (fatLine*)malloc(sizeof(fatLine));

	index = 0;
	for(i = 1; i <= md->numfiles; i++){
		getFatLine(line, fd, i + index);
		while(line->filesize == 0){
			index++;
			getFatLine(line, fd, i + index);
		}
		if(strcmp(line->filename, fl->filename) == 0){
			free(line);
			return(i+index);
		}
	}
	free(line);
	return -1;
}

void updateFATLine(int fd, fatLine *fl, vaultMetadata* md, ssize_t size, short blockNum){
	int index;


	// update the FAT line offset value
	index =findLineIndex(fd, fl, md);
	if(blockNum ==1){
		lseek(fd,METADATA_LENGH + (index-1)*FAT_LINE_LENGH+(NAME_LENGH+sizeof(ssize_t)+sizeof(mode_t)+sizeof(time_t)), SEEK_SET);
		fl->offBlock1 -= size;
		write(fd, &(fl->offBlock1), sizeof(off_t));
	}
	else if(blockNum == 2){
		lseek(fd,METADATA_LENGH + (index-1)*FAT_LINE_LENGH+(NAME_LENGH+sizeof(ssize_t)+sizeof(mode_t)+sizeof(time_t)+
				sizeof(off_t)+sizeof(ssize_t)), SEEK_SET);
		fl->offBlock2 -= size;
		write(fd, &(fl->offBlock2), sizeof(off_t));
	}
	else{
		lseek(fd,METADATA_LENGH + (index-1)*FAT_LINE_LENGH+(NAME_LENGH+sizeof(ssize_t)+sizeof(mode_t)+sizeof(time_t)+
						sizeof(off_t)+sizeof(ssize_t)+sizeof(off_t)+sizeof(ssize_t)), SEEK_SET);
		fl->offBlock3 -= size;
		write(fd, &(fl->offBlock3), sizeof(off_t));
	}
}

ssize_t getTotalFilesSize(int fd, vaultMetadata* md){
	ssize_t totalSize;
	fatLine *fl;
	int i, index;
	fl = (fatLine*)malloc(sizeof(fatLine));

	index = 0;
	totalSize = 0;
	for (i = 1; i <= md->numfiles; i++){
		getFatLine(fl, fd, i + index);
		while(fl->filesize == 0){
			index++;
			getFatLine(fl, fd, i+index);
		}
		totalSize += fl->filesize;
	}
	free(fl);
	return totalSize;
}

off_t findMinOffset(int fd, vaultMetadata* md){
	fatLine *fl;
	int i, index;
	off_t minOff;
	fl = (fatLine*)malloc(sizeof(fatLine));

	minOff = md->overallSize;
	index = 0;
	for (i = 1; i <= md->numfiles; i++){
		getFatLine(fl, fd, i + index);
		while(fl->filesize == 0){
			index++;
			getFatLine(fl, fd, i+index);
		}
		if (fl->offBlock1 < minOff)
			minOff = fl->offBlock1;
	}
	free(fl);

	return minOff;
}

off_t findMaxOffset(int fd, vaultMetadata* md){
	fatLine *fl;
	int i, index;
	off_t farthest;
	off_t maxOff;
	fl = (fatLine*)malloc(sizeof(fatLine));

	farthest = 0;
	index = 0;
	for (i = 1; i <= md->numfiles; i++){
		getFatLine(fl, fd, i + index);
		while(fl->filesize == 0){
			index++;
			getFatLine(fl, fd, i+index);
		}
		if (fl->offBlock1 > farthest){
			farthest = fl->offBlock1;
			maxOff = fl->sizeBlock1 + fl->offBlock1;
		}
		if (fl->offBlock2 > farthest){
			farthest = fl->offBlock2;
			maxOff = fl->sizeBlock2 + fl->offBlock2;
		}
		if (fl->offBlock2 > farthest){
			farthest = fl->offBlock2;
			maxOff = fl->sizeBlock2 + fl->offBlock3;
		}
	}
	free(fl);

	return maxOff;
}

double calcFragmentRatio(int fd, vaultMetadata* md){
	off_t maxOff, minOff, count, len, consumedLength, totalGap;

	maxOff = findMaxOffset(fd, md);
	minOff = findMinOffset(fd, md);
	consumedLength = maxOff - minOff;

	//measure the gaps
	count = 0;
	totalGap = 0;
	lseek(fd, START_OF_DATA, SEEK_SET);
	count += countEmptySpace(fd, maxOff - count);

	while(count < maxOff){
		count += findNextSpace(fd, maxOff - count);
		len = countEmptySpace(fd, maxOff - count);
		count += len;
		totalGap += len;

	}

	return (((double)(totalGap))/((double)(consumedLength)));
}

int main(int argc, char **argv){
	struct timeval current, start;
	struct stat inputStat;
	int fd , tmp, i, index;
	fatLine* fl;
	long vaultfileSize;
	short totalFiles, blockNum;
	off_t offset1, offset2, offset3, count, off, maxOff;
	ssize_t buff1, buff2, buff3, size;
	ssize_t totalSize, size2, remSize, filled;
	vaultMetadata *md;
	char str[NAME_LENGH];

	gettimeofday(&start, NULL);
	if(argc < 3){
		printf("missing arguments\n");
		measureTime(start);
		return -1;
	}

	fd = open(argv[1], O_RDWR|O_CREAT, 0755);
	if(fd == -1){
		printf("ERROR with vault file: %s\n", strerror(errno));
		measureTime(start);
		return -1;
	}


	fl = (fatLine*)malloc(sizeof(fatLine));

	argv[2] = toLowerCase(argv[2]);

	//operation 1: init
	if(strcmp(argv[2], "init")==0){
		vaultfileSize = getEnterdSize(argv[3]);
		if(vaultfileSize == -1){
			printf("ERROR: Illegal size letter\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		if(vaultfileSize < METADATA_LENGH+100*(FAT_LINE_LENGH)+8*3){
			printf("ERROR: insufficient file size, minimum required: %dB\n",  METADATA_LENGH+100*(FAT_LINE_LENGH)+8*3);
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}

		gettimeofday(&current, NULL);
		totalFiles = 0;
		tmp = METADATA_LENGH;

		write(fd, &vaultfileSize, sizeof(ssize_t));// maximum size
		write(fd, &tmp, sizeof(ssize_t));// current size -> at least the meta data
		write(fd, &current.tv_sec, sizeof(time_t)); //create time
		write(fd, &current.tv_sec, sizeof(time_t)); //mode time
		write(fd, &totalFiles, sizeof(short));//total files
		//clear the data
		lseek(fd, METADATA_LENGH, SEEK_SET);
		for(i = 0; i < vaultfileSize - METADATA_LENGH; i++){
			write(fd, "\0", 1);
		}

	}
	//operetion 2: list files

	else if((strcmp(argv[2], "list")==0)){
		lseek(fd, METADATA_LENGH-sizeof(short), SEEK_SET);
		read(fd, &totalFiles, sizeof(short));
		index = 0;

		for (i = 1; i <= totalFiles; i++){
			getFatLine(fl, fd, i + index);
			while(fl->filesize == 0){
				index++;
				getFatLine(fl, fd, i+index);
			}
			printFatLine(fl);
		}

	}


	//operation 3: add file
	else if(strcmp(argv[2], "add")==0){
		read(fd, &totalSize, sizeof(ssize_t));//vault size
		read(fd, &size2, sizeof(ssize_t)); //current size

		if(isInVault(fd, argv[3]) != 0){
			printf("ERROR: file already exists\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		lseek(fd, METADATA_LENGH-sizeof(short), SEEK_SET);
		read(fd, &totalFiles, sizeof(short));
		if(totalFiles == 100){
			printf("ERROR: max files capacity has reached, please remove files\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		if(stat(argv[3], &inputStat) < 0){
			printf("ERROR: cannot open input file: %s\n", strerror(errno));
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		if(totalSize - size2 <= inputStat.st_size + FAT_LINE_LENGH+16){
			printf("ERROR: not enough memory\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		remSize = inputStat.st_size+16;
		offset1 = 0;
		offset2 = 0;
		offset3 = 0;
		buff1 = 0;
		buff2 = 0;
		buff3 = 0;
		//start looking for space
		lseek(fd, START_OF_DATA, SEEK_SET);
		count = (off_t)START_OF_DATA;


		//buffer 1
		filled = fillBuffer(fd, &buff1, &offset1, remSize, totalSize-count);
		count += filled;
		if(buff1 >=  remSize){
			placeFile(fd, argv[3], inputStat, offset1, buff1, offset2, buff2, offset3, buff3);
			measureTime(start);
			close(fd);
			free(fl);
			return 0;
		}
		//buffer 2
		lseek(fd, START_OF_DATA+offset1+buff1, SEEK_SET);

		remSize = remSize - buff1 + 16;


		filled = fillBuffer(fd, &buff2, &offset2, remSize, totalSize-count);
		offset2 += buff1+offset1;
		count += filled;
		if(buff2 == remSize){
			placeFile(fd, argv[3], inputStat, offset1, buff1, offset2, buff2, offset3, buff3);
			measureTime(start);
			close(fd);
			free(fl);
			return 0;
		}
		if(filled == 0){
			printf("ERROR: no room to place the file\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		//buffer 3
		lseek(fd, START_OF_DATA+offset2+buff2, SEEK_SET);
		remSize = remSize - buff1 + 16;
		filled = fillBuffer(fd, &buff3, &offset3, remSize, totalSize-count);
		offset3 += buff2+offset2;
		count += filled;
		if(buff3 == remSize){
			placeFile(fd, argv[3], inputStat, offset1, buff1, offset2, buff2, offset3, buff3);
			measureTime(start);
			close(fd);
			free(fl);
			return 0;
		}
		if(filled == 0){
			printf("ERROR: no room to place the file\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		lseek(fd, START_OF_DATA+offset3+buff3, SEEK_SET);

		//the 3 buffers were not enough, continue to search further
		while(count < totalSize){
			//check who is the smallest
			if(buff1 <= buff2 && buff1 <= buff3){
				buff1 = buff2;
				offset1 = offset2;
				buff2 = buff3;
				offset2 = offset3;
			}
			else if(buff2 <= buff3){
				buff2 = buff3;
				offset2 = offset3;
			}

			filled = fillBuffer(fd, &buff3, &offset3, remSize, totalSize-count);
			count += filled;
			offset3+= offset2+buff2;
			if(buff3 == remSize){
				placeFile(fd, argv[3], inputStat, offset1, buff1, offset2, buff2, offset3, buff3);
				measureTime(start);
				close(fd);
				free(fl);
				return 0;
			}
			if(filled == 0){
				printf("ERROR: no room to place the file\n");
				measureTime(start);
				close(fd);
				free(fl);
				return -1;
			}

		}
		printf("ERROR: no room to place the file\n");
		measureTime(start);
		close(fd);
		free(fl);
		return -1;

	}

	//operation 4: delete
	else if(strcmp(argv[2], "rm")==0){
		index = isInVault(fd, argv[3]);
		if(index == 0){
			printf("ERROR: file not found\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}

		md = (vaultMetadata*)malloc(sizeof(vaultMetadata));
		getFatLine(fl, fd, index);
		getMetadata(md, fd);

		//update metadata
		gettimeofday(&current, NULL);

		if(fl->offBlock2 == 0)
			md->overallSize -= (fl->filesize + 16);
		else if (fl->offBlock3 == 0)
			md->overallSize -= (fl->filesize + 32);
		else
			md->overallSize -= (fl->filesize + 48);
		md->modTime = current.tv_sec;
		md->numfiles -= 1;


		writeMetadata(md, fd);
		//clear fat line
		lseek(fd,METADATA_LENGH + (index-1)*FAT_LINE_LENGH, SEEK_SET);
		for(i = 0; i < FAT_LINE_LENGH; i++)
			write(fd, "\0", 1);
		//clear blocks
		lseek(fd,START_OF_DATA + fl->offBlock1, SEEK_SET);
		for(i = 0; i < fl->sizeBlock1; i++)
			write(fd, "\0", 1);

		if(fl->offBlock2 != 0){
			lseek(fd,START_OF_DATA + fl->offBlock2, SEEK_SET);
			for(i = 0; i < fl->sizeBlock2; i++)
				write(fd, "\0", 1);
		}
		if(fl->offBlock3 != 0){
			lseek(fd,START_OF_DATA + fl->offBlock3, SEEK_SET);
			for(i = 0; i < fl->sizeBlock3; i++)
				write(fd, "\0", 1);
		}
		free(md);

	}

	//operation 5: fetch
	else if(strcmp(argv[2], "fetch")==0){
		index = isInVault(fd, argv[3]);
		if(index == 0){
			printf("ERROR: requested file not found\n");
			measureTime(start);
			close(fd);
			free(fl);
			return -1;
		}
		getFatLine(fl, fd, index);
		fetchFile(fd, fl);
	}
	//operation 6: defrag
	else if(strcmp(argv[2], "defrag") == 0){
		md = (vaultMetadata*)malloc(sizeof(vaultMetadata));
		lseek(fd, 0, SEEK_SET);
		read(fd, &totalSize, sizeof(ssize_t)); //total size

		getMetadata(md, fd);
		if(md->numfiles == 0){
			printf("no files to defrag\n");
			measureTime(start);
			close(fd);
			free(fl);
			free(md);
			return 0;
		}
		size = 0;
		off = 0;
		maxOff = findMaxOffset(fd, md);
		lseek(fd, START_OF_DATA, SEEK_SET);
		//go to the end of the file
		while(off < maxOff){
			//measure the empty space
			size = countEmptySpace(fd, maxOff - off);
			if (size != 0){

				//find the corresponding FAT line of the block
				blockNum = findLineOfOffset(fd, md, fl, off+size);

				if (blockNum == -1)
					break;

				//move the block and update
				moveBlockBack(fd, fl, size, blockNum);
				updateFATLine(fd, fl, md, size, blockNum);
				maxOff = findMaxOffset(fd, md);
				//continue
				lseek(fd, START_OF_DATA+off, SEEK_SET);
			}
			off += findNextSpace(fd, maxOff - off);
		}
		gettimeofday(&current, NULL);
		md->modTime = current.tv_sec;
		writeMetadata(md, fd);

		free(md);
	}

	//operation 7: status
	else if(strcmp(argv[2], "status") == 0){
		md = (vaultMetadata*)malloc(sizeof(vaultMetadata));
		getMetadata(md, fd);
		printf("Number of files: %d\n", md->numfiles);
		sizeToString(getTotalFilesSize(fd, md), str);
		printf("Total size: %s\n", str);
		printf("Fragmentation ratio: %lf\n", calcFragmentRatio(fd, md));
		free(md);
	}
	else{
		printf("Error: unknown command\n");
	}

	close(fd);
	free(fl);
	measureTime(start);
	return 0;
}

