/*
** peer.c -- a stream socket peer demo
*/

#include <stdio.h>
#include <stdlib.h>
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

#define PORT "4301"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold
#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define FNAME "blockchain.txt" // name of blockchain file
//##########function prototypes##############

//Tries to get the blockchain.txt file from the first arg found in argv. If it cannot connect to the computer then it uses a local copy.
void get_file_from_ip(int argc, char* argv[]);
int connect_to_peer(char* addr);
char* get_data(int sockfd);
void use_local_copy();
int wait_for_FD(int sockfd);
int recvd_connection(struct sockaddr_storage their_addr, int new_fd, int sockfd);
const char* get_file_contents();

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

int main(int argc, char* argv[])
{
	//THE NEXT THING I SHOULD START IS THE LOGIC FOR WHEN I CANNOT CONNECT TO THE IP (i.e. USE LOCAL COPY OF BLOCKCHAIN.TXT)
	int sockfd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sigaction sa;
	int yes=1;
	char port_buf[5];
	int rv;

	argc--;
	argv++;
	if(!argc){
		use_local_copy();
	}else
		get_file_from_ip(argc, argv);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	sprintf(port_buf,"%d",atoi(PORT)+argc);

	if ((rv = getaddrinfo(NULL, port_buf, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}
		//printf("Working in peer #:\t%d",argc);
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}
		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections [indefinitely]...\n");

	wait_for_FD(sockfd);
	return 0;
}

const char* get_file_contents(){
	FILE* fp;
	fp = fopen(FNAME, "r");
	char* fileconts = (char*) malloc(sizeof(char)*64);
	for(int i = 0; i < 64; i++){
		fileconts[i] = '\0';
	}
	int c; // note: int, not char, required to handle EOF
	int d = 0;
	fgets(fileconts,64,fp);
	fclose(fp);
	return fileconts;
}

int recvd_connection(struct sockaddr_storage their_addr, int new_fd, int sockfd){
	char s[INET6_ADDRSTRLEN];
	inet_ntop(their_addr.ss_family,
		get_in_addr((struct sockaddr *)&their_addr),
		s, sizeof s);
	printf("server: got connection from %s\n", s);
	
	if (!fork()) { // this is the child process
		close(sockfd); // child doesn't need the listener
		if (send(new_fd, get_file_contents(), 13, 0) == -1) {
				printf("Error");
				perror("send");
			}
		close(new_fd);
		exit(0);
	}
	close(new_fd);  // parent doesn't need this
	return 1;
}

int wait_for_FD(int sockfd){
	socklen_t sin_size;
	struct sockaddr_storage their_addr; // connector's address information
	int new_fd;
	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}
		recvd_connection(their_addr, new_fd, sockfd);		
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

char* get_data(int sockfd){
	int numbytes;
	char* buf = (char*) malloc(sizeof(char)*MAXDATASIZE);
	struct addrinfo hints, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
	    perror("recv");
	    exit(1);
	}

	buf[numbytes] = '\0';

	printf("client: received '%s'\n",buf);

	close(sockfd);
	return buf;
}

int connect_to_peer(char* addr){
	int sockfd;  
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(addr, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			close(sockfd);
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return -1;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);
	freeaddrinfo(servinfo); // all done with this structure
	return sockfd;
}