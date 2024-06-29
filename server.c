#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#define error(msg) {perror(msg);printf("\n");exit(1);}

const char* error_message = "HTTP/1.1 404 Not Found\nContent-Type: text/html\n\n\
        <!DOCTYPE html>\
        <html lang='en'>\
        <head>\
            <meta charset='UTF-8'>\
            <meta http-equiv='X-UA-Compatible' content='IE=edge'>\
            <meta name='viewport' content='width=device-width, initial-scale=1.0'>\
            <title>Error</title>\
        </head>\
        <body>\
            <div>\
                <h2>The Requested URL was not found!</h2>\
            </div>\
        </body>\
        </html>";



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
	return strcmp(s1,s2)==0;
}

bool image(const char* ext){
	if(equal(ext,"png")||equal(ext,"jpeg")||equal(ext,"jpg")||equal(ext,"ico")||equal(ext,"webp")||equal(ext,"avif")) return true;
	return false;
}

void set_content_type(const char* file_path,char* content_type){
	char ext[64]={0};
	char file_path_temp[512]={0};
	strcpy(file_path_temp,file_path);
	char file_name[128]={0};
	
	char* name=strtok(file_path_temp,"/");
	while(name){
		strcpy(file_name,name);
		name=strtok(NULL,"/");
	}
	printf("File name: %s\n",file_name);
	int len=strlen(file_name);
	char* extension=strtok(file_name,".");
	if(strlen(extension)==len)
		strcpy(ext,"html");
	else{
		while(extension){
			strcpy(ext,extension);
			extension=strtok(NULL,".");
		}
	}
	
	strcpy(&ext[strlen(ext)],"\n\n");
	
	printf("Ext: %s\n",ext);
	lower(ext);
	if(!image(ext))
		strcpy(content_type,"text/");
	else
		strcpy(content_type,"image/");
	strcpy(&content_type[strlen(content_type)],ext);
}

void getpath(char * buffer,char* file_path,char *content_type){
	bzero(file_path,sizeof(file_path));
	bzero(content_type,sizeof(content_type));
	char * headers=strtok(buffer,"{");
	
	char *data=strtok(NULL,"}");
	printf("Headers: %s\n",headers);
	char * delim=" ";
	char* req_type=strtok(headers,delim);
	char *req_path=strtok(NULL,delim);
	if(!headers || !req_type || !req_path){
		strcpy(file_path,"NONE");
		return;
	}
	if(strlen(req_path)>1 && req_path[strlen(req_path)-1]=='/')
		req_path[strlen(req_path)-1]='\0';
	printf("\nRequest path: %s\n",req_path);
	
	if(strcmp(req_type,"GET")==0){
		if(strcmp(req_path,"/")==0){
			strcpy(file_path,"index.html");
		}
		else{
			strcpy(file_path,&req_path[1]);
		}
		set_content_type(file_path,content_type);
	
	}else{
		
		printf("Data received: %s\n",data);
	
	}
	
}
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Provide port number!\n");
        exit(1);
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(1);
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION); // Example for TLS 1.2
    // Load server certificate and private key
    if (SSL_CTX_use_certificate_file(ctx, "/etc/secrets/server.crt", SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, "/etc/secrets/server.key", SSL_FILETYPE_PEM) <= 0 ||
        !SSL_CTX_check_private_key(ctx))
    {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
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

    char* ok_headers = "HTTP/1.1 200 OK\nContent-Type: ";
    FILE* fs;
    socklen_t len = sizeof(client);
    char buffer[READ_SIZE], file[1024];
    char content_type[64];
    while (1) {
        bzero(buffer, sizeof(buffer));
        int newfd = accept(sockfd, (struct sockaddr*)&client, &len);
        if (newfd < 0) {
            perror("accept");
            continue;
        }

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, newfd);

        if (SSL_accept(ssl) <= 0) {
            printf("ssl accept error! \n")
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(newfd);
            continue;
        }

        printf("Accepted connection...\n");
        fflush(stdout);
        SSL_read(ssl, buffer, sizeof(buffer));
        getpath(buffer, file, content_type);

        bzero(buffer, sizeof(buffer));
        fs = fopen(file, "r");
        int readBytes = 0;
        if (fs) {
            SSL_write(ssl, ok_headers, strlen(ok_headers));
            SSL_write(ssl, content_type, strlen(content_type));
            while ((readBytes = fread(buffer, sizeof(char), READ_SIZE, fs)) > 0) {
                SSL_write(ssl, buffer, readBytes);
            }
            fclose(fs);
        } else {
            SSL_write(ssl, error_message, strlen(error_message));
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(newfd);
    }

    close(sockfd);
    SSL_CTX_free(ctx);
    EVP_cleanup();
    return 0;
}
