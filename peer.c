/*
** peer.c -- a stream socket peer demo
*/

#include <stdio.h>
#include <stdlib.h>
//#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#define WAITING_FOR_FD 0
#define WANTS_TO_PEEK 1
#define READING 2
#define NOT_EXISTING 3

#define NUMBERTHREADS 100
#define NOT_CONNECTION_THREADS 2
#define PORT "4301"  // the port users will be connecting to

#define BACKLOG 50	 // how many pending connections queue will hold
#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define FNAME "blockchain.txt" // name of blockchain file
//##########function prototypes##############

//Tries to get the blockchain.txt file from the first arg found in argv. If it cannot connect to the computer then it uses a local copy.
void get_file_from_ip(int argc, char* argv[]);
int connect_to_peer(char* addr);
char* get_data(int sockfd);
void use_local_copy();
int wait_for_FD(int sockfd);
void * recvd_connection(void* connectionData);//, struct sockaddr_storage their_addr, int new_fd, int sockfd);
void read_file_contents(char* item);

struct Node{
	int socket;
	char ip_address[INET_ADDRSTRLEN];
	struct Node* next;
};

struct Node* newNode(int socket, char* ip_address){
	struct Node* node = (struct Node*) malloc(sizeof(struct Node));
	node->socket = socket;
	for(int newNode_i = 0; newNode_i < INET_ADDRSTRLEN; newNode_i++){
		node->ip_address[newNode_i] = ip_address[newNode_i];
	}
	node->next = NULL;
	return node;
}

struct thread_data{
	int id;
	struct sockaddr_storage their_addr;
	int new_fd;
	int sockfd;
	struct thread_data* next;
};

struct thread_data* newData(){
	struct thread_data* data = (struct thread_data*) malloc(sizeof(struct thread_data));
	data->next = NULL;
	return data;
}

void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

struct Node* list_of_connections = NULL;
char blockchain_txt_contents[64];
char new_contents[64];
pthread_t threads[NUMBERTHREADS];
struct thread_data* data_list = NULL;
int status;
int next_thread = 0;
int next_data = 0;

//define pthread_mutex_t as semaphor
typedef pthread_mutex_t semaphore;
int state[NUMBERTHREADS-NOT_CONNECTION_THREADS];
//a semaphore
semaphore socket_lock;
semaphore blockchain_lock;
semaphore s[NUMBERTHREADS-NOT_CONNECTION_THREADS];

void * monitor_blockchain_txt(void* tid){
	
	int j;
	while(1){
		//printf("The contents:\t%s\n",blockchain_txt_contents);
		j = 0;
		read_file_contents(&new_contents);
		while(1){
			if(new_contents[j] == blockchain_txt_contents[j]){
				if(new_contents[j] == '\0'){
					break;
				}
				j++;
			}
			else{
				printf("A change is detected\n");
				strcpy(blockchain_txt_contents,new_contents);
				//blockchain_txt_contents = new_contents;
				struct Node* curr = list_of_connections;
				if(curr != NULL){
					pthread_mutex_lock(&socket_lock);
					for(int monitor_i = 0; monitor_i < next_data; monitor_i++) {
						if(blockchain_txt_contents[0] != 0){
							send(curr->socket, blockchain_txt_contents, sizeof blockchain_txt_contents, 0);
							curr = curr->next;
						}
					}
					pthread_mutex_unlock(&socket_lock);
				}
				break;
			}
		}
	}
}

void* start_listening_socket(void * tid){
	next_thread++;
	//THE NEXT THING I SHOULD START IS THE LOGIC FOR WHEN I CANNOT CONNECT TO THE IP (i.e. USE LOCAL COPY OF BLOCKCHAIN.TXT)
	int sockfd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sigaction sa;
	int yes=1;
	char port_buf[5];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	sprintf(port_buf,"%d",atoi(PORT));

	if ((rv = getaddrinfo(NULL, port_buf, &hints, &servinfo)) == 0) {
		// loop through all the results and bind to the first we can
		for(p = servinfo; p != NULL; p = p->ai_next) {
			if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
				perror("server: socket");
				continue;
			}
			if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
				perror("setsockopt");
				exit(NULL);
			}
			//printf("Working in peer #:\t%d",argc);
			if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
				//close(sockfd);
				perror("server: bind");
				continue;
			}
			break;
		}

		freeaddrinfo(servinfo); // all done with this structure

		if (p == NULL)  {
			fprintf(stderr, "server: failed to bind\n");
			exit(NULL);
		}

		if (listen(sockfd, BACKLOG) == -1) {
			perror("listen");
			exit(NULL);
		}

		sa.sa_handler = sigchld_handler; // reap all dead processes
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		if (sigaction(SIGCHLD, &sa, NULL) == -1) {
			perror("sigaction");
			exit(NULL);
		}
		printf("server: waiting for connections [indefinitely]...\n");
		status = pthread_create(&threads[next_thread], NULL, monitor_blockchain_txt, (void*)next_thread);
		next_thread++;
		wait_for_FD(sockfd);
	}
	else{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	}

}

int main(int argc, char* argv[])
{
	//handle some essential tasks before spawning daemons (getting a local blockchain file; establishing my first connection; etc)
	argc--;
	argv++;
	if(!argc){
		use_local_copy();
	}else
		get_file_from_ip(argc, argv);
	//create a mutex
	if (pthread_mutex_init(&socket_lock, NULL) != 0){
		printf("\n mutex init failed\n");
		return 1;
	}
	if (pthread_mutex_init(&blockchain_lock, NULL) != 0){
		printf("\n mutex init failed\n");
		return 1;
	}
	//run listening thread
	printf("Main here. Creating listening thread %d\n", next_thread);
	status = pthread_create(&threads[next_thread], NULL, start_listening_socket, (void *)next_thread);
	//check for errors with creating threads
	if (status != 0) {
		printf("Oops. pthread create returned error code %d\n", status);
		exit(-1);
	}



// THINK ABOUT WHETHER I REALLY NEED THIS HERE
	//have the main thread wait for all the threads created
	for (int main_i = 0; main_i < NUMBERTHREADS; main_i++){
		pthread_join(threads[main_i], NULL);
	}
	//delete the mutex
	pthread_mutex_destroy(&socket_lock);
	pthread_mutex_destroy(&blockchain_lock);
	exit(0);
}

void read_file_contents(char * item){
	FILE* fp;
	pthread_mutex_lock(&blockchain_lock);
	fp = fopen(FNAME, "r");
	//for(int i = 0; i < 64; i++){
	//	item[i] = '\0';
	//}
	fgets(item,64,fp);
	fclose(fp);
	pthread_mutex_unlock(&blockchain_lock);
}

void * recvd_connection(void* connectionData){//, struct sockaddr_storage their_addr, int new_fd, int sockfd){
	struct thread_data * data = (struct thread_data *) connectionData;
	pthread_mutex_lock(&socket_lock);
	while (send(data->new_fd, blockchain_txt_contents, 64, 0) == -1);
	pthread_mutex_unlock(&socket_lock);
	int a_value = 1;
	while(1) {
		if(a_value == NULL){
			next_thread--;
			return;
		}
		a_value = get_data(data->new_fd);
	}
}

//Here sockfd is the socket I communicate thru. (I send & recieve data through this socket)
int wait_for_FD(int sockfd){
	struct Node* tail = list_of_connections;
	struct thread_data* data_tail = data_list;
	socklen_t sin_size;
	struct sockaddr_storage their_addr; // connector's address information
	int got_a_connection = 0;
	while(1) {  // main accept() loop
		
		if(data_list == NULL){
			data_list = newData();
			data_tail = data_list;
		}
		else if(got_a_connection == 1){
			while(data_tail->next != NULL){
				data_tail = data_tail->next;
			}
			got_a_connection = 0;
		}
		sin_size = sizeof their_addr;
		data_tail->new_fd = accept(sockfd, (struct sockaddr *)&(data_tail->their_addr), &sin_size);//(struct sockaddr *)&their_addr, &sin_size);
		if (data_tail->new_fd == -1) {
			perror("accept");
			continue;
		}

		char s[INET_ADDRSTRLEN];
		inet_ntop(data_tail->their_addr.ss_family, get_in_addr((struct sockaddr *)&(data_tail->their_addr)), s, sizeof s);
		printf("server: got connection from %s\n", s);

		if(list_of_connections == NULL){
			list_of_connections = newNode(data_tail->new_fd, s);
			tail = list_of_connections;
		}
		else{
			while(tail->next != NULL)
				tail = tail->next;
			tail->next = newNode(data_tail->new_fd, s);
			tail = tail->next;
		}



		if(next_thread < NUMBERTHREADS){
			data_tail->id = next_data;
			data_tail->sockfd = sockfd;
			status = pthread_create(&threads[next_thread], NULL, recvd_connection, data_tail);		
			got_a_connection = 1;
			if(status == 0)
				next_thread++;
				next_data++;
		}
		if(next_data == 3){
			while(1){
				sleep(5);
				printf("Mass send\n");
				struct Node* temp = list_of_connections;
				for(int j = 0; j < 3; j++){
					char sending[2];
					printf("Sending data to %s on socket #%d\n",temp->ip_address,temp->socket);
					sprintf(sending,"%d",temp->socket);
					while(send(temp->socket, sending, 2, 0) == -1);
					temp = temp->next;
				}
			}
		}
	}
}

void use_local_copy(){
	FILE* fp;
	fp = fopen(FNAME, "r");
	if(fp == NULL){
		fp = fopen(FNAME, "w");
		fputs("1",fp);
	}
	fclose(fp);
	fp = fopen(FNAME, "r");
	read_file_contents(&blockchain_txt_contents);
}

void write_to_blockchain_txt(char* argv){
	FILE* fp;
	fp = fopen(FNAME,"w");
	fputs(argv,fp);
	fclose(fp);
}

void get_file_from_ip(int argc, char* argv[]){
	int ret;
	char* rets;
	if(argc < 1){
		use_local_copy();
	}
	char addr[16];
	sscanf(argv[0],"%s",addr);
	ret = connect_to_peer(addr);
	// Connection unsuccessful. Use local copy.
	if(ret == -1){
		use_local_copy();
		return;
	}
	// Connection successful. Receive data from owner & save to local blockchain.txt
	rets = get_data(ret);
	write_to_blockchain_txt(rets);
}

void announce_remove(struct Node* to_remove){
	struct Node* curr = list_of_connections;
	char announcement[64];
	strcpy(announcement,"Disconnecting from ");
	for(int ann_i = 0; ann_i < 16; ann_i++)
		announcement[19+ann_i] = to_remove->ip_address[ann_i];
	pthread_mutex_lock(&socket_lock);
	for(; curr != NULL; curr = curr->next) {
		while (send(curr->socket, announcement, sizeof announcement, 0) == -1);
	}
	pthread_mutex_lock(&socket_lock);
}

void remove_from_list(int sockfd){
	struct Node* tail = list_of_connections;
	struct Node* to_remove;
	struct thread_data* data_tail = data_list;
	struct thread_data* data_to_remove;
	if(tail == NULL)
		list_of_connections = NULL;
	else if(tail->next == NULL){
		free(tail);
		list_of_connections = NULL;
	}
	else{
		while(tail->next->socket != sockfd){
			tail = tail->next;
		}
		to_remove = tail->next;
		tail->next = to_remove->next;
		announce_remove(to_remove);
		free(&to_remove);
	}
	if(data_tail == NULL)
		data_list = NULL;
	else if(data_tail->next == NULL){
		free(data_tail);
		data_list = NULL;
	}
	else{
		while(data_tail->next->new_fd != sockfd){
			data_tail = data_tail->next;
		}
		data_to_remove = data_tail->next;
		data_tail->next = data_to_remove->next;
		free(data_to_remove);
	}
	next_data--;
}

char* get_data(int sockfd){
	int numbytes;
	char* buf = (char*) malloc(sizeof(char)*MAXDATASIZE);
	struct addrinfo hints, *p;
	int rv;
	char s[INET_ADDRSTRLEN];
	if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
	    perror("recv");
	    exit(1);
	}
	if(numbytes <= 0){
		remove_from_list(sockfd);
		if(list_of_connections == NULL)
			return NULL;
	}
	buf[numbytes] = '\0';

	printf("client: received '%s'\n",buf);

	//close(sockfd);
	return buf;
}

int connect_to_peer(char* addr){
	int sockfd;  
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(addr, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			//close(sockfd);
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return -1;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
	printf("client: connecting to %s\n", s);
	list_of_connections = newNode(sockfd, s);
	freeaddrinfo(servinfo); // all done with this structure
	return sockfd;
}