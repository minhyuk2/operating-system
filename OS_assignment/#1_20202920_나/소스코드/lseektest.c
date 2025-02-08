#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]){

	int fd, offset,len;
	int buffersize = 512;
	int readBuffer;
	int fileLength = 0;
	//because xv6 block is 512
	char buffer[buffersize];

	if(argc < 4){
	  printf(1,"usage : lseektest <filename> <offset> <string>\n");
	  exit();
	}
	
	//open file for print
	fd = open(argv[1],O_RDWR);
	if(fd < 0){
		printf(1,"File open error!\n");
		exit();
	}
	
	//print for before
	printf(1,"Before : ");
	
	//print until all comment is done
	while((readBuffer = read(fd,buffer, buffersize))>0){
		//print to screen
		fileLength += write(1,buffer,readBuffer);
		
	}

	//move to end-1
	if(lseek(fd,fileLength-1,SEEK_SET)<0){
		printf(1,"lseek error\n");
		exit();
	}
	//read last word
	if(read(fd,buffer,1)<0){
		printf(1,"read error\n");
		exit();
	}
	
	//if last word is \n don't print \n
	if(buffer[0] != '\n'){
		printf(1,"\n");
	}


	//move for print lseek
	offset = atoi(argv[2]);
	if(lseek(fd,offset,SEEK_SET)<0){
		printf(1,"lseek error\n");
		exit();
	}
	
	len = strlen(argv[3]);

	if(write(fd,argv[3],len)<0){
		printf(1,"write error\n");
		exit();
	}

	//close file
	close(fd);

	//reopen file
	fd = open(argv[1],O_RDWR);
	if(fd < 0){
		printf(1,"File reopen error\n");
		exit();
	}
	//print after data
	printf(1,"After : ");

	//read file data
	while((readBuffer = read(fd,buffer, buffersize))>0){
		write(1,buffer,readBuffer);
	}
	printf(1,"\n");
	//close file
	close(fd);

	exit();
}