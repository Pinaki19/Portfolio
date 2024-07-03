#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <linux/limits.h>
#define bool int
#define true 1
#define false 0
#define READ_SIZE 51200
#define error(msg) {perror(msg);printf("\n");exit(1);}

const char* error_headers = "HTTP/1.1 404 Not Found\nContent-Type: text/html\n\n";
const char *ok_headers = "HTTP/1.1 200 OK\nContent-Type: ";

void lower(char* data){
	for(int i=0;i<strlen(data);i++){
		if(data[i]>='A' && data[i]<='Z')
			data[i]+=32;
	}
}


bool equal(const char* s1,const char* s2){
	if(strcmp(s1,s2)==0) return true;
    return false;
}

bool image(const char* ext){
	if(equal(ext,"png")||equal(ext,"jpeg")||equal(ext,"jpg")||equal(ext,"ico")||equal(ext,"webp")||equal(ext,"avif")||equal(ext,"svg")) return true;
	return false;
}

bool exists(const char* haystack,const char* needle){
    if (strstr(haystack, needle))
        return true;
    return false;
}

bool set_folder_name(const char *extension, char *folder_name)
{
    bzero(folder_name, sizeof(folder_name));
    if (equal(extension, "js"))
    {
        strcpy(folder_name, "/static/js/");
    }
    else if (equal(extension, "html"))
    {
        strcpy(folder_name, "/static/html/");
    }
    else if (equal(extension, "css"))
    {
        strcpy(folder_name, "/static/css/");
    }
    else if (image(extension))
    {
        strcpy(folder_name, "/assets/images/");
    }
    else
        return false;
    return true;
}

bool set_content_type(char* file_name,char* folder_name,char* content_type){
	char ext[64]={0};
	char file_name_temp[512]={0};
	strcpy(file_name_temp,file_name);
	char file_name_raw[256]={0};
	
	char* name=strtok(file_name_temp,"/");
    if(!name) return false;
	while(name){
		strcpy(file_name_raw,name);
		name=strtok(NULL,"/");
	}
	printf("File name: %s\n",file_name_raw);
	int len=strlen(file_name_raw);
	
	if(!strstr(file_name_raw,".")){
        strcat(file_name,".html");
        strcpy(ext, "html");
    }
	else{
        char *extension = strtok(file_name_raw, ".");
        while(extension){
			strcpy(ext,extension);
			extension=strtok(NULL,".");
		}
	}
    printf("Ext: %s\n", ext);
    lower(ext);
    if(!set_folder_name(ext,folder_name)) return false;
    if (!image(ext))
        strcpy(content_type, "text/");
    else
        strcpy(content_type, "image/");

    if(exists(ext,"svg"))
        strcpy(ext,"svg+xml");

    strcpy(&ext[strlen(ext)],"\n\n");

	strcpy(&content_type[strlen(content_type)],ext);
    printf("Content type: %s",content_type);
    return true;
}



bool getpath(char * buffer,char* folder_name,char* file_name,char *content_type){
	bzero(file_name,sizeof(file_name));
	bzero(content_type,sizeof(content_type));
    bzero(folder_name,sizeof(folder_name));
	char * headers=strtok(buffer,"{");
	if(!headers) return false;
	char *data=strtok(NULL,"}");
	char * delim=" ";
	char* req_type=strtok(headers,delim);
	char *req_path=strtok(NULL,delim);
	if(!req_type || !req_path || req_path[0]=='.'){
		strcpy(file_name,"NONE");
		return false;
	}
	if(strlen(req_path)>1 && req_path[strlen(req_path)-1]=='/')
		req_path[strlen(req_path)-1]='\0';
	printf("\nRequest path: %s\n",req_path);
	printf("REQ TYPE: %s\n",req_type);
	if(equal(req_type,"GET") || equal(req_type,"OPTIONS")||equal(req_type,"HEAD")){
        if(req_path[0]=='.') return false;

        char *result = strstr(req_path, "error.css");
        if (result != NULL){
            strcpy(file_name, "error.css");
            strcpy(folder_name,"/static/css/");
            strcpy(content_type,"text/css\n\n");
            return true;
        }
        if(equal(req_path,"/")){
			strcpy(file_name,"index.html");
		}
		else 
			strcpy(file_name,&req_path[1]);
		
		if(!set_content_type(file_name,folder_name,content_type)) return false;
	}else{
		printf("POST Data received: %s\n",data);
	}
    return true;
}
int main(int argc, char **argv) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("Current working dir: %s\n", cwd);
    else{
        perror("getcwd() error");
        return 1;
    }
    if (argc < 2) {
        printf("Provide port number!\n");
        exit(1);
    }
    struct sockaddr_in server, client;
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[1]));
    if(argc==3)
        server.sin_addr.s_addr = inet_addr(argv[2]);
    else
        server.sin_addr.s_addr = INADDR_ANY;
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1,res;
    res=setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
    if(res<0)
    	error("set sockopt fail!");
    
    res=bind(sockfd, (struct sockaddr*)&server, sizeof(server));
    if(res<0)
    	error("Bind fail!");
    listen(sockfd, 50);
    FILE* fs;
    socklen_t len = sizeof(client);
    char buffer[READ_SIZE], file_name[512],folder_name[512];
    char full_path[1024];
    char content_type[128];
    while (1) {
        bzero(buffer, sizeof(buffer));
        bzero(full_path,sizeof(full_path));
        strcpy(full_path,cwd);
        fflush(stdout);
        int newfd = accept(sockfd, (struct sockaddr*)&client, &len);
        if (newfd < 0) {
            perror("accept fail");
            continue;
        }
        int cnt=read(newfd, buffer, sizeof(buffer));
        if (cnt==0)
            continue;
        bool result=getpath(buffer,folder_name, file_name, content_type);
        strcat(full_path,folder_name);
        strcat(full_path,file_name);
        printf("Full request file path: %s\n",full_path);
        if (!result && !strstr(content_type, "html")){
            write(newfd, error_headers, strlen(error_headers));
            close(newfd);
            continue;
        }
        bzero(buffer, sizeof(buffer));
        fs = fopen(full_path, "r");
        int readBytes = 0;
        if(result && fs){
            write(newfd, ok_headers, strlen(ok_headers));
            write(newfd, content_type, strlen(content_type));
            while ((readBytes = fread(buffer, sizeof(char), READ_SIZE, fs)) > 0)
                write(newfd, buffer, readBytes);
            
        }else{
            write(newfd, error_headers, strlen(error_headers));
            strcpy(full_path,cwd);
            strcat(full_path,"/static/html/error.html");
            fs=fopen(full_path,"r");
            while ((readBytes = fread(buffer, sizeof(char), READ_SIZE, fs)) > 0)
                write(newfd, buffer, readBytes);
            
        }
        fclose(fs);
        close(newfd);
    }

    close(sockfd);
    return 0;
}
