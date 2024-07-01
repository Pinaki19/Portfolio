#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#define bool int
#define true 1
#define false 0

#define error(msg) {perror(msg);printf("\n");exit(1);}

const char* error_headers = "HTTP/1.1 404 Not Found\nContent-Type: text/html\n\n";
const char *ok_headers = "HTTP/1.1 200 OK\nContent-Type: ";

#define READ_SIZE 256000

void lower(char* data){
	for(int i=0;i<strlen(data);i++){
		if(data[i]>='A' && data[i]<='Z')
			data[i]+=32;
	}
}

void write_to_temp_file(const char *content, const char *filename)
{
    FILE *fp = fopen(filename, "w");
    if (!fp)
    {
        perror("Failed to open temporary file");
        exit(1);
    }
    fprintf(fp, "%s", content);
    fclose(fp);
}

bool equal(const char* s1,const char* s2){
	if(strcmp(s1,s2)==0) return true;
    return false;
}

bool image(const char* ext){
	if(equal(ext,"png")||equal(ext,"jpeg")||equal(ext,"jpg")||equal(ext,"ico")||equal(ext,"webp")||equal(ext,"avif")||equal(ext,"svg")) return true;
	return false;
}

int set_content_type(char* file_path,char* content_type){
	char ext[64]={0};
	char file_path_temp[512]={0};
	strcpy(file_path_temp,file_path);
	char file_name[128]={0};
	
	char* name=strtok(file_path_temp,"/");
    if(!name) return 0;
	while(name){
		strcpy(file_name,name);
		name=strtok(NULL,"/");
	}
	printf("File name: %s\n",file_name);
	int len=strlen(file_name);
	char* extension=strtok(file_name,".");
	if(strlen(extension)==len){
        strcpy(&file_path[strlen(file_path)],".html");
        strcpy(ext, "html");
    }
	else{
		while(extension){
			strcpy(ext,extension);
			extension=strtok(NULL,".");
		}
	}
    printf("Ext: %s\n", ext);
    lower(ext);
    if (!image(ext))
        strcpy(content_type, "text/");
    else
        strcpy(content_type, "image/");
    strcpy(&ext[strlen(ext)],"\n\n");
	
	strcpy(&content_type[strlen(content_type)],ext);
    return 1;
}

int getpath(char * buffer,char* file_path,char *content_type){
	bzero(file_path,sizeof(file_path));
	bzero(content_type,sizeof(content_type));
	char * headers=strtok(buffer,"{");
	
	char *data=strtok(NULL,"}");
	printf("Headers: %s\n",headers);
	char * delim=" ";
	char* req_type=strtok(headers,delim);
	char *req_path=strtok(NULL,delim);
	if(!headers || !req_type || !req_path || req_path[0]=='.'){
		strcpy(file_path,"NONE");
		return 0;
	}
	if(strlen(req_path)>1 && req_path[strlen(req_path)-1]=='/')
		req_path[strlen(req_path)-1]='\0';
	printf("\nRequest path: %s\n",req_path);
	
	if(strcmp(req_type,"GET")==0){
        if(req_path[0]=='.') return 0;

        char *result = strstr(req_path, "error.css");
        if (result != NULL){
            strcpy(file_path, "error.css");
            strcpy(content_type,"text/css\n\n");
            return 1;
        }
        if(strcmp(req_path,"/")==0){
			strcpy(file_path,"index.html");
		}
		else{
			strcpy(file_path,&req_path[1]);
		}
		if(!set_content_type(file_path,content_type)) return 0;
	
	}else{
		printf("Data received: %s\n",data);
	}
    return 1;
}
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Provide port number!\n");
        exit(1);
    }

   
    struct sockaddr_in server, client;
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[1]));
    server.sin_addr.s_addr = INADDR_ANY;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    int reuse = 1,res;
    res=setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
    if(res<0)
    	error("set sockopt fail!");
    
    res=bind(sockfd, (struct sockaddr*)&server, sizeof(server));
    if(res<0)
    	error("Bind fail!");
    listen(sockfd, 10);
    FILE* fs;
    socklen_t len = sizeof(client);
    char buffer[READ_SIZE], file[1024];
    char content_type[64];
    while (1) {
        bzero(buffer, sizeof(buffer));
        int newfd = accept(sockfd, (struct sockaddr*)&client, &len);
        if (newfd < 0) {
            perror("accept fail");
            continue;
        }
        read(newfd, buffer, sizeof(buffer));
        if(strlen(buffer)==0) continue;
        printf("Data received: %s\n",buffer);
        int result=getpath(buffer, file, content_type);
        if(!result && strcmp(content_type,"text/html/n/n")!=0) continue;
        bzero(buffer, sizeof(buffer));
        fs = fopen(file, "r");
        int readBytes = 0;
        if(result && fs){
            write(newfd, ok_headers, strlen(ok_headers));
            write(newfd, content_type, strlen(content_type));
            while ((readBytes = fread(buffer, sizeof(char), READ_SIZE, fs)) > 0)
            {
                write(newfd, buffer, readBytes);
            }
        }else{
            write(newfd, error_headers, strlen(error_headers));
            fs=fopen("error.html","r");
            while ((readBytes = fread(buffer, sizeof(char), READ_SIZE, fs)) > 0)
            {
                write(newfd, buffer, readBytes);
            }
        }
        fclose(fs);
        close(newfd);
    }

    close(sockfd);
    return 0;
}
