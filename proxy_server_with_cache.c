#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_BYTES 4096 // defines number of bytes, used for read/write operations
#define MAX_CLIENTS 400//defining maximum number of clients at one time 
#define MAX_SIZE 200*(1<<20) //total cache size limit for your proxy, we can use this to evict old cache if the size exceeds this
#define MAX_ELEMENT_SIZE 10*(1<<10) //sets the maximum size of a single cache element to 10kb
typedef struct cache_element cache_element;


struct cache_element{
    char* data; //pointer to the cached content
    int len; //length of the data
    char* url; // the url this cached data corresponds to
    time_t lru_time_track; //time stamp for tracking last recently used(LRU) policy
    cache_element* next; //pointer to the next cache element
};

cache_element* find(char* url); // looks up the cache to see if the requested url is already stored, if found-returns a pointer to the corresponding cache_element: otherwise, returns NULL
int add_cache_element(char* data, int size, char* url); //adds a new element to the cache, returns 0 on success or -1 if cache is full or memory allocation fails
void remove_cache_element();//removes one or more elements to free up space-most likely the lru or the oldest

int port_number = 8080;// the port number our proxy server listens on , client will connect to this port to send HTTP requests
int proxy_socketId;// this will store the file descriptor(fd) returned by socket() for the proxy server
pthread_t tid[MAX_CLIENTS];//array to hold thread IDs, each client connection gets its own thread, allowing the proxy to handle multiple simultaneous requests
sem_t semaphore;// used to control access to limited resources, if client requests exceeds the max_clients this semaphore puts the waiting threads to sleep and wakes them when traffic on queue decreases
pthread_mutex_t lock;// used to protect critical sections, especially when multiple threads access or modify resources like the cache

cache_element* head;//pointer to the head of a linked list of cached items, it stores fetched HTTP content
int cache_size;//integer, tracks the total size of the cache currently being used

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}
int connectRemoteServer(char* host_addr, int port_num){
    /*creating socket for remote server*/
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket<0){
        printf("Error in creating your socket\n");
        return -1;
    }
    //get host by the name or ip address provided
    struct hostent *host = gethostbyname(host_addr);
    if(host == NULL){
        fprintf(stderr, "No such host exist\n");
        return -1;
    }//insert ip address and port number of host in struct 'server_addr'
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);
    /*connect to remote server*/
    if(connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0){
        fprintf(stderr, "Error in connecting\n");
        return -1;
    }
    return remoteSocket;
}

int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq){
    char *buf = (char*)malloc(sizeof(char)*MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    size_t len = strlen(buf);

    if(ParsedHeader_set(request, "Connection", "close")<0){
        printf("Set header key is not working \n");
    }
    if(ParsedHeader_get(request, "Host") == NULL){
        if(ParsedHeader_set(request, "Host", request->host)<0){
            printf("Set \"Host\"  header key is not working\n");
        }
    }
    if(ParsedRequest_unparse_headers(request, buf+len,(size_t)MAX_BYTES-len)<0){
        printf("unparse failed\n");
        //return -1                 //if this happens still try to send request without header
    }
    strcat(buf, "\r\n");

    int server_port = 80;//default remote server port
    if(request->port != NULL){
        server_port = atoi(request->port);
    }
    int remoteSocketId = connectRemoteServer(request->host, server_port);
    if(remoteSocketId < 0){
        return -1;
    }
    int bytes_send = send(remoteSocketId, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);

    int bytes_received= recv(remoteSocketId, buf, MAX_BYTES-1, 0);
    char *temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES);//temp buffer
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;
    
    while(bytes_received > 0){
       int bytes_sent = send(clientSocket, buf, bytes_received, 0);
        for(int i = 0; i < bytes_send/sizeof(char); i++){
        temp_buffer[temp_buffer_index] = buf[i];
        temp_buffer_index++;//response printing
       }
       temp_buffer_size += MAX_BYTES;
       temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
       if(bytes_send < 0){
            perror("Error in sending data to the client\n");
            break;
    }
    bzero(buf, MAX_BYTES);
    bytes_received = recv(remoteSocketId, buf, MAX_BYTES-1, 0);
  }
  temp_buffer[temp_buffer_index] = '\0';
  free(buf);
  add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
  printf("Done\n");
  free(temp_buffer);
  close(remoteSocketId);
  return 0;
}

int checkHTTPversion(char *msg){
    int version = -1;
    if(strncmp(msg, "HTTP/1.1", 8) == 0){
        version = 1;
    }
    else if(strncmp(msg, "HTTP/1.0", 8) == 0){
        version = 1;//handling this similar to version 1.1
    }
    else
        version = -1;

    return version;
}

void* thread_fn(void* socketNew){
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("semaphore value is: %d\n", p);
    int* t = (int*)(socketNew);
    int socket = *t;//socket is socket descriptor of the connected client
    int bytes_send_client,len;//bytes transferred

    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));//creating buffer of 4kb for a client
    bzero(buffer, MAX_BYTES);//making buffer zero
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);//receiving the request of client by proxy server

    while(bytes_send_client > 0){
        len = strlen(buffer);
        //loop until you find "\r\n\r\n" in the buffer
        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }else{
            break;
        }
    }

    char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);
    //tempReq, buffer both store the http request sent by client
    for(int i = 0; i < strlen(buffer); i++){
        tempReq[i] = buffer[i];
    }
    //checking for the request in cache
    struct cache_element* temp = find(tempReq);
    if(temp != NULL){
        //request found in cache, so sending the response to client from proxy's cache
        int size = temp->len/sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while(pos < size){
            bzero(response, MAX_BYTES);
            for(int i = 0; i < MAX_BYTES; i++){
                response[i] = temp->data[pos];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrieved from the cache\n\n");
        printf("%s\n\n", response);
    }else if(bytes_send_client > 0){
        len = strlen(buffer);
        //parsing the request
       struct ParsedRequest* request = ParsedRequest_create();
        //ParsedRequest_parse returns 0 on success and -1 on failure . On success it stores parsed request in  the request
        if(ParsedRequest_parse(request, buffer, len) < 0){
            printf("parsing failed\n");
        }else{
            bzero(buffer, MAX_BYTES);
           if (request->method && strcmp(request->method, "GET") == 0){
                if(request->host && request->path && (checkHTTPversion(request->version) == 1)){
                    bytes_send_client = handle_request(socket, request, tempReq);//handle GET request
                    if(bytes_send_client == -1){
                        sendErrorMessage(socket, 500);
                    }
                }else{
                    sendErrorMessage(socket, 500);//500 internal error
                }
            }else{
                printf("This code doesn't support any method apart from GET\n");
            }
        }
        //freeing up the request pointer
        ParsedRequest_destroy(request);
    }else if(bytes_send_client <  0){
        perror("Error in receiving from client.\n");
    }else if(bytes_send_client == 0){
        printf("client disconnected");
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore,&p);
    printf("Semaphore post value is : %d\n",p);
    free(tempReq);
    return NULL;
 }
int main(int argc, char * argv[]){
    int client_socketId, client_len;//client_socketId == to store the client socket id
    struct sockaddr_in server_addr, client_addr;//Address of client and server to be assigned
    sem_init(&semaphore, 0, MAX_CLIENTS);//initializing semapore and lock
    pthread_mutex_init(&lock, NULL);//initializing lock for cache
    if(argc == 2){//checking whether two arguments are received or not
        port_number = atoi(argv[1]); //atoi is a function which returns string for the input
    }else{
        printf("Too few arguments\n");
        exit(1);
    }

    printf("Starting Proxy server at port: %d\n", port_number);
    //AF_INET: domain , address family IPv4 here
    //SOCK_STREAM: type of the socket, this is used here for tcp socket , a udp socket can also be used
    //tcp & udp are in the transport layer
    //sock_stream is a full-duplex byte stream, and it is characterized as a type that ensures that data is not lost or duplicated.
    //0:protocol, determines the layer beneath the transport layer that we are using
    //with zero'0' we are specifing that we want the IP layer to be used underneath the transportation layer
    //socket() creates an endpoint for communication and returns a file
    //descriptor(which is an integer) that refers to that endpoint. The file descriptor returned by
    //a successful call will be the lowest-numbered file descriptor not
    //currently open for the process.When an error occurred it will return the value -1
    /*creating the proxy socket*/
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socketId < 0){
        perror("Failed to create a socket\n");
        exit(1);
    }
    int reuse = 1;
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR,(const char*)&reuse, sizeof(reuse)) < 0){
        perror("setSockOpt failed\n");
    }

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);//assigning port to the proxy
    server_addr.sin_addr.s_addr = INADDR_ANY;//any available address assigned
    /*binding the socket*/
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr))<0){
        perror("Port is not available\n");
        exit(1);
    }
    printf("Binding on port %d\n", port_number);
    /*proxy socket listening to the requests*/
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status < 0){
        perror("Error in listening\n");
        exit(1);
    }
    int i = 0;//iterator for thread_id(tid) and accepted Client_Socket for each thread
    int Connected_socketId[MAX_CLIENTS];//this array stores socket descriptors of connected clients
    //infinite loop for accepting connections
    while(1){
        bzero((char*)&client_addr, sizeof(client_addr));//clear struct client_addr
        client_len = sizeof(client_addr);
        //accepting the connections
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);//accepts connection
        if(client_socketId < 0){
            fprintf(stderr, "Error in Accepting connection !\n");
            exit(1);
        }
        else{
            Connected_socketId[i] = client_socketId;//storing accepted client into array
        }
        //getting IP address and port number of client
        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];//INET_ADDRSTRLEN: default ip address size
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client is connected with port number %d and ip address is %s\n",ntohs(client_addr.sin_port),str);
        //printf("Socket values of index %d in main function is %d\n",i, client_socketId);
        pthread_create(&tid[i], NULL, thread_fn, (void*)&Connected_socketId[i]);//creating a thread for each client accepted
        i++;
    }
    close(proxy_socketId);//close socket
    return 0;
}
cache_element* find(char* url){
    //check for url in the cache if found returns pointer to the respective cache element or else retrurn NULL
    cache_element* site = NULL;
   
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove cache lock acquired %d\n", temp_lock_val);
    if(head != NULL){
        site = head;
        while(site != NULL){
            if(!strcmp(site->url, url)){
                printf("LRU time track before: %ld", site->lru_time_track);
                printf("\nurl found\n");
                //updating the time_track
                site->lru_time_track = time(NULL);
                printf("LRU time track after %ld", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }else{
        printf("url not found\n");
    }
    
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Lock is unlocked\n");
    return site;
}
void remove_cache_element(){
    //if cache is not empty search for the node which has the least lru_time_track and delete it
    cache_element * p;//cache_element pointer (prev. pointer)
    cache_element * q;//cache_element pointer (next pointer)
    cache_element * temp;//cache_element to remove
    
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Lock is acquired\n");
    if(head != NULL){//cache != empty
        for(q = head,p = head, temp = head; q->next != NULL; q = q->next){//iterarte through entire cache and search for oldest time track
            if(((q->next)->lru_time_track)<(temp->lru_time_track)){
                temp = q->next;
                p = q;
            }
        }
        if(temp == head){
            head = head->next;//handle the base case
        }else{
            p->next = temp->next;
        }
        //updating the cache size
        cache_size = cache_size - (temp->len)-sizeof(cache_element)- strlen(temp->url)-1;
        free(temp->data);
        free(temp->url);//free the removed element
        free(temp);
    }
    
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove cache lock\n");
}
int add_cache_element(char* data, int size, char* url){
    //adds element to the cache
  
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n",temp_lock_val);
    int element_size = size+1+strlen(url)+sizeof(cache_element);//size of the new elementwhich will be added to the cache
    if(element_size > MAX_ELEMENT_SIZE){
       
        //if element size is greater than MAX_ELEMENT we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add cache lock  unlocked\n");
        
        return 0;
    }
    else{
        while(cache_size+element_size > MAX_SIZE){
            //we keep removing elements from cache until we get space to add the element
            remove_cache_element();
        }
        cache_element* element = (cache_element*)malloc(sizeof(cache_element));//allocating memory for the new cache element
        element->data = (char*)malloc(size+1);//allocating memory for the response to be stored in the cache element
        strcpy(element->data, data);
        element->url = (char*)malloc(1+(strlen(url)*sizeof(char)));//allocating memory for the request to be stored in the cache element (as a key)
        strcpy(element->url, url);
        element->lru_time_track = time(NULL);//updating the time_track
        element->next = head;
        element->len = size;
        head = element;
        cache_size += element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add cache lock unlocked\n");
        return 1;
    }
    return 0;
}