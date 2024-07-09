#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <linux/limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>

#define bool int
#define true 1
#define false 0
#define READ_SIZE 51200 //size of file read in bytes
#define error(msg) {perror(msg);printf("\n");exit(1);}
#define MAX_AGE_CACHE 7200       //maximum age of browser cache -->2 hr

#define THREAD_POOL_SIZE 20 // number of threads
#define QUEUE_SIZE 100   //size of task queue

typedef struct
{
    int client_fd;      // file descriptor of the newly accepted client
    struct sockaddr_in client_addr;
} task_t;

task_t task_queue[QUEUE_SIZE];
int queue_front = 0, queue_rear = 0;
pthread_mutex_t queue_lock;     // lock for providing mutual exclusion in accessing the queue
sem_t queue_sem;            //signifies whether or not there is any task on the task queue
sem_t free_slots;           // number of available free slots

char cwd[PATH_MAX];     //name of current working directory
const char* error_headers = "HTTP/1.1 404 Not Found\nContent-Type: text/html\n\n";
const char *ok_headers = "HTTP/1.1 200 OK\nCache-Control: max-age=%d\nContent-Type: %s\nETag: %s\n\n";

void lower(char* data){     //convert upper to lower case
	for(int i=0;i<strlen(data);i++){
		if(data[i]>='A' && data[i]<='Z')
			data[i]+=32;
	}
}

void set_up_json(const char *json_str,char* response){
    char temp[512] = {0};
    snprintf(temp,sizeof(temp),"HTTP/1.1 200 OK\nContent-Type: application/json\n\n %s", json_str);
    strcpy(response, temp);
}

void set_up_response_headers(int max_age,const char* content_type,const char* etag,char* buffer){
    char temp[1500]={0};
    snprintf(temp,sizeof(temp),ok_headers,max_age,content_type,etag);
    strcpy(buffer,temp);
}

// Function to generate ETag based on file modification time
struct stat server_stat;
void generate_etag(const char *file_path, char *etag)
{
    bzero(etag,sizeof(etag));
    struct stat file_stat;
    
    if (stat(file_path, &file_stat)==0)
    {
        snprintf(etag, 128, "\"%lx-%lx-%lx-%lx\"", file_stat.st_mtime, file_stat.st_size, server_stat.st_mtime, server_stat.st_size);
    }else
        strcpy(etag, "None"); // In case of error, set empty ETag
}

bool check_etag(const char *etag, const char *buffer, char *client_etag)
{
    char *etag_header = strstr(buffer, "If-None-Match: ");
    bzero(client_etag,sizeof(client_etag));
    if (etag_header)
    {
        sscanf(etag_header, "If-None-Match: %s", client_etag);
        // Compare ETags
        int n = strlen(client_etag);
        while (n && client_etag[0]!='"'){
            memmove(&client_etag[1],client_etag,n-1);
            n--;
        }
        if (strlen(client_etag) > 0 && strcmp(etag, client_etag) == 0)
        {
            return true;
        }
    }
    return false;
}

bool equal(const char* s1,const char* s2){      //whether two strings are equal case-sensitive
	if(strcmp(s1,s2)==0) return true;
    return false;
}

bool image(const char* extension){      //whether the request is for some image file
    if (equal(extension, "png") || equal(extension, "jpeg") || equal(extension, "jpg") || equal(extension, "ico") || equal(extension, "webp") || equal(extension, "avif") || equal(extension, "svg"))
        return true;
    return false;
}

bool exists(const char* haystack,const char* needle){   //whether needle exists in haystack
    if (strstr(haystack, needle))
        return true;
    return false;
}

bool set_folder_name(const char *extension, char *folder_name){     //sets the folder from where the file is to be served
    bzero(folder_name, sizeof(folder_name));
    if (equal(extension, "js"))
        strcpy(folder_name, "/static/js/");
    else if (equal(extension, "webmanifest"))
        strcpy(folder_name, "/static/json/");
    else if (equal(extension, "html"))
        strcpy(folder_name, "/static/html/");
    else if (equal(extension, "css"))
        strcpy(folder_name, "/static/css/");
    else if (image(extension))
        strcpy(folder_name, "/assets/images/");
    else
        return false;
    return true;
}

bool set_content_type(char* file_name,char* folder_name,char* content_type){    //sets the type of content to be returned according to request received
	char ext[64]={0};   //word after the last '.' , taken as extension
	char file_name_temp[512]={0};   //copy of name of the file + the request path
	strcpy(file_name_temp,file_name);
	char file_name_raw[256]={0};    // to store the file name only. The word after the last '/' 
	
	char* name=strtok(file_name_temp,"/");
    if(!name) return false;
	while(name){
		strcpy(file_name_raw,name);
		name=strtok(NULL,"/");
	}
	
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
   
    lower(ext);
    if(!set_folder_name(ext,folder_name)) return false;
    if (!image(ext))
        strcpy(content_type, "text/");
    else
        strcpy(content_type, "image/");

    if(exists(ext,"svg"))   // svgs are special, their content type includes +xml extra
        strcpy(ext,"svg+xml");

	strcpy(&content_type[strlen(content_type)],ext);
   
    return true;
}


bool getpath(const char * buffer_original,char* folder_name,char* file_name,char *content_type){
	bzero(file_name,sizeof(file_name));
	bzero(content_type,sizeof(content_type));
    bzero(folder_name,sizeof(folder_name));
    char buffer[4096]={0};
    strncpy(buffer,buffer_original,4095);

	char * headers=strtok(buffer,"{");  //get the headers only headers are delimited by a "{"
	if(!headers) return false;
	char *data=strtok(NULL,"}");    //get the data from the request. Data is delimited by a "}" 
                                    // data is unused as post requests are not handled

	char * delim=" ";   // delimiter for request type and path from headers
	char* req_type=strtok(headers,delim);   // request type --> get post etc
	char *req_path=strtok(NULL,delim);      // request path --> / , /projects, /index etc
	if(!req_type || !req_path || req_path[0]=='.'){
		strcpy(file_name,"NONE");
		return false;
	}
	if(strlen(req_path)>1 && req_path[strlen(req_path)-1]=='/')
		req_path[strlen(req_path)-1]='\0';
	
	if(equal(req_type,"GET") || equal(req_type,"OPTIONS")||equal(req_type,"HEAD")){
        if(req_path[0]=='.') return false;

        char *result = strstr(req_path, "error.css");       //request for error.css comes from error.html file
        if (result != NULL){
            strcpy(file_name, "error.css");
            strcpy(folder_name,"/static/css/");
            strcpy(content_type,"text/css");
            return true;
        }
        if(equal(req_path,"/")){
			strcpy(file_name,"index.html");
		}
		else 
			strcpy(file_name,&req_path[1]);     // remove the leading '/' from the request path
		
		if(!set_content_type(file_name,folder_name,content_type)) return false;
	}else{
		printf("POST Data received: %s\n",data);
        return false;
	}
    return true;
}

void *handle_client(void *arg)
{
    long thread_num=*(pthread_t*) arg;
    char buffer[READ_SIZE], file_name[512], folder_name[512];
    char full_path[1024];
    char content_type[128];
    char etag[128];
    char cli_tag[128];
    FILE *fs;
    bool flag;  //false if content requested is html
    int max_age;    //max age of cache
    while (1){
        max_age=7200;
        fs=NULL;
        flag=true;
        sem_wait(&queue_sem);   //wait for data to be available in the task queue

        pthread_mutex_lock(&queue_lock);    //lock the task mutex
        task_t task = task_queue[queue_front];  //get the leading task
        queue_front = (queue_front + 1) % QUEUE_SIZE;

        pthread_mutex_unlock(&queue_lock);      //unlock the task queue mutex

        sem_post(&free_slots);      //increase the number of free slots
        int newfd = task.client_fd; //get the request fd
        // Set a receive timeout of 10 sec after which request will be rejected
        struct timeval timeout;
        timeout.tv_sec = 10; // 10 sec
        timeout.tv_usec = 0;

        if (setsockopt(newfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            perror("setsockopt(SO_RCVTIMEO) failed");
            close(newfd);
            continue;
        }

        bzero(buffer, sizeof(buffer));
        int cnt = read(newfd, buffer, sizeof(buffer));
        if (cnt == 0||!buffer) {     //nothing was received in allocated time
            close(newfd);
            continue;
        }

        bzero(full_path, sizeof(full_path));
        strcpy(full_path, cwd);
        bool result = getpath(buffer, folder_name, file_name, content_type);
        strcat(full_path, folder_name);
        strcat(full_path, file_name);
        generate_etag(full_path, etag);
        

        if (strstr(file_name, ".html"))
            flag=false;
        if (flag && check_etag(etag, buffer, cli_tag))      //htmls are not cached
        {
            printf("Etag %s path:%s matched\n", etag, full_path);
            write(newfd, "HTTP/1.1 304 Not Modified\n\n", strlen("HTTP/1.1 304 Not Modified\n\n"));
            close(newfd);
            continue;
        }
        else
        {
            if(!flag)
                printf("html file!  path: %s\n", full_path);
            else
                printf("Etag for path: %s not matched! etag:%s prev:%s \n", full_path, etag, cli_tag);
        }

        bzero(buffer, sizeof(buffer));          // buffer is reused to read the file

        if(strstr(file_name,"health_check")){   //this is health-check path, also used keep the service awake
            set_up_json("OK",buffer);
            write(newfd, buffer, strlen(buffer));
            close(newfd);
            continue;
        }

        if (strstr(file_name, "webmanifest")){
            char content[1024]={0};
            fs=fopen(full_path,"r");
            fread(content,1,1024,fs);
            set_up_json(content, buffer);
            write(newfd, buffer, strlen(buffer));
            close(newfd);
            fclose(fs);
            continue;
        }

        if (!result && !strstr(content_type, "html"))   //if request was for a html file,the path is unknown so error.html will be served
        {                                               // else some error occured and request will be rejected with 404
            write(newfd, error_headers, strlen(error_headers));
            close(newfd);
            continue;
        }

        fs= fopen(full_path, "r");
        int readBytes = 0;
        if (result && fs)   //path was parsed correctly and file was found
        {
            if(!flag)
                max_age=0;
            else
                max_age=MAX_AGE_CACHE;
            set_up_response_headers(max_age,content_type, etag, buffer);
           
            write(newfd, buffer, strlen(buffer));
            while ((readBytes = fread(buffer, sizeof(char), READ_SIZE, fs)) > 0)
                write(newfd, buffer, readBytes);
        }
        else    //path was not found, error.html will be served
        {
            write(newfd, error_headers, strlen(error_headers));
            strcpy(full_path, cwd);
            strcat(full_path, "/static/html/error.html");
            fs = fopen(full_path, "r");
            while ((readBytes = fread(buffer, sizeof(char), READ_SIZE, fs)) > 0)
                write(newfd, buffer, readBytes);
        }
        close(newfd);   //close the file descriptor of client
        if(fs) fclose(fs);     // close the file
        fflush(stdout);
    }
    pthread_exit(NULL);     // we can never reach here as threads run in a while(1) loop
}     


int main(int argc, char **argv) {
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
    char server_path[512] = {0};
    strcpy(server_path, cwd);
    strcat(server_path, "/server.c");
    if(stat(server_path, &server_stat)!=0){
        printf("Server stat error!\n");
        exit(1);
    }
    
    pthread_mutex_init(&queue_lock, NULL); // default mutex initialisation
    sem_init(&queue_sem, 0, 0); 
    sem_init(&free_slots, 0, QUEUE_SIZE);

    pthread_t thread_pool[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++)  //create all the threads
        pthread_create(&thread_pool[i], NULL, handle_client, &thread_pool[i]);

    // threads created will wait for some data to be available in the task queue as queue_sem is set to 0

    socklen_t len = sizeof(client);
    while (1)
    {
        int newfd = accept(sockfd, (struct sockaddr *)&client, &len); // accept client connection
        if (newfd < 0)
        {
            perror("accept fail");
            continue;
        }

        sem_wait(&free_slots);  //wait for some slot to become free

        pthread_mutex_lock(&queue_lock);    //lock the queue mutex
        task_t task = {newfd, client};  //create a new task
        task_queue[queue_rear] = task;
        queue_rear = (queue_rear + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&queue_lock);  //unlock the queue mutex

        sem_post(&queue_sem);       //signal that data is available in the queue
    }

    close(sockfd);  //we will never reach here
    return 0;
}
