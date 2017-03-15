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
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

// Number of allowed connections on the incoming queue
#define BACKLOG 10
// Max size for data
#define MAX_BUFFER_SIZE 1024

void redirection(char * path);
bool getUserAgent(char * http_header);
bool isUserAgent(char * request_line);
void isAuthenticatedClient(char * http_header);

int main (int argc, char *argv[]) {
	// the port users will be connecting to
	const char * PORT_NUM = argv[1];
	// Root directory
	char * ROOT_PATH = argv[2];
	// Append website folder
	strcat(ROOT_PATH, "/website");
	int status;
	// sock_fd is the socket file descriptor (initial)
	// new_fd is the new file descriptor (used for accepeted connectin to send() and receieve())
	int sock_fd, new_fd;  
	// addrinfo hints returns a structure that contains the Internet address
	// serverinfos points to the res, results linked-list 
	struct addrinfo hints, *serverinfos, *p;
	// Client's information 
	struct sockaddr_storage client_addr; 
	// Address size of client 
	socklen_t client_addr_size;
	// optval 
	int yes=1, rv;

	// Empties the struct 
	memset(&hints, 0, sizeof hints);
	// Impartial to IPv4 or IPv6
	hints.ai_family = AF_UNSPEC;
	// Socket stream for relible data transfer 
	hints.ai_socktype = SOCK_STREAM;
	// Use local IP 
	hints.ai_flags = AI_PASSIVE;

	// Getting ready to connect
	// Checks that servoinfo points to linked-list of length greater than 0 
	if ((status = getaddrinfo(NULL, PORT_NUM, &hints, &serverinfos)) != 0) {
		fprintf(stderr, "ERROR - getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// Checks all the values in the linked-list obtained and bind to the first possible 
	for(p = serverinfos; p != NULL; p = p->ai_next) {
		// socket() - gets the file descriptor 
		// domain - IPv4 or IPv6; type - stream or datagram; protocol - TCP or UDP
		if ((sock_fd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("ERROR - server: socket");
			continue;
		}

		// set the socket
		// sock_fd - current socket; SOL_SOCKET - level; SO_REUSEADDR - allow other sockets to bind
		// if there is no active listening socket bounded to the port; &yes - indicating value;
		// sizeof(int) - size of &yes argument 
		if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("ERROR - setsockopt");
			exit(1);
		}
		// Bind to port passed in getaddrinfo 
		// sock_fd - socket file descriptor; ai_addr - address info; 
		// ai_addrlen - length of address in bytes
		if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sock_fd);
			perror("ERROR - server: bind");
			continue;
		}
		break;
	}

	// Free the linked-list, no longer need the structure 
	freeaddrinfo(serverinfos); 

	// Exit program if cannot bind to a port after it has looped through the linked-list 
	if (p == NULL)  {
		fprintf(stderr, "ERROR - server: failed to bind\n");
		exit(1);
	}

	// Exit program if backlog exceeded or bind failed 
	if (listen(sock_fd, BACKLOG) == -1) {
		perror("ERROR - listen");
		exit(1);
	}

	printf("server: connecting...\n");

	// Need to parse HTTP request
	char buffer[MAX_BUFFER_SIZE];
	char buffer_user_agent[MAX_BUFFER_SIZE];
	char buffer_post[MAX_BUFFER_SIZE];
	// File path 
	char path[MAX_BUFFER_SIZE];
	int received;
	// accept() loop
	while (1) {
		client_addr_size = sizeof client_addr;
		new_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &client_addr_size);
		// Error checking: if accept failed 
		if (new_fd == -1) {
			perror("ERROR - accept");
			exit(1);
			continue;
		}	
		// Fork() new process for child process because accept() will use new file descriptor 
		if (!fork()) { 
			// Child process doesn't need to listen
			close(sock_fd); 
			// HTTP Request 
			received = recv(new_fd, buffer, MAX_BUFFER_SIZE, 0);
			strcpy(buffer_user_agent, buffer);
			strcpy(buffer_post, buffer);
			// Error 
			if (received < 0)
		        fprintf(stderr,("ERROR - receieve\n"));
		    // Connection closed 
		    else if (received == 0)  
		        fprintf(stderr,"ERROR - client disconnected upexpectedly.\n");
		    else {
				char * http_header_method = strtok(buffer, " \t");
				char * http_header_path = strtok(NULL, " \t"); 
				char * http_header_protocol = strtok(NULL, " \t\n"); 
				// HTTP Response
				if (send(new_fd, "HTTP/1.1 200 OK\n\n", 17, 0) == -1)
					perror("ERROR - send (response)");
				// Check if / is the path request, send default index.html
				if (strcmp(http_header_path, "/") == 0)
	                    strcpy(http_header_path, "/index.html");
	            // Initialize path with root directory 
				strcpy(path, ROOT_PATH);
				// Check for POST
				if (strstr(http_header_method, "POST") != NULL) 
					strcat(path, "/secret");
				// Check for user agent 
				if (getUserAgent(buffer_user_agent))
					strcat(path, "/mobile");
				// Concatenate request file path 
	            strcat(path, http_header_path);
				// Data to be sent to client 
				char data_to_send[MAX_BUFFER_SIZE];
				int bytes_read, file_descriptor;
				if ((file_descriptor = open(path, O_RDONLY)) != -1) { 
					while ((bytes_read = read(file_descriptor, data_to_send, MAX_BUFFER_SIZE)) > 0)
	                    write (new_fd, data_to_send, bytes_read);
	            }
	            // Error in finding file 
	            else {
	            	strcpy(path, "/Users/haolu/Desktop/website/nodir/nofile.html");
	            	file_descriptor = open(path, O_RDONLY);
	            	bytes_read = read(file_descriptor, data_to_send, MAX_BUFFER_SIZE);
	            	// Omit [Request Path] and add the request path of the client 
	            	int req_path_start_pos = 0, req_path_end_pos = 0 ;
	            	for (int i = 0; i < strlen(data_to_send); i++) {
	            		if (data_to_send[i] == '[') 
	            			req_path_start_pos = i;
	            		if (data_to_send[i] == ']') {
	            			req_path_end_pos = i;
	            			break;
	            		}
	            	}
	            	char not_found_404_start[req_path_start_pos];
	            	char not_found_404_end[req_path_end_pos];
	            	// From begin to [
	            	strncpy(not_found_404_start, data_to_send, (req_path_start_pos));
	            	// Null terminator 
	            	not_found_404_start[req_path_start_pos] = '\0';
	            	// From ] to end
	            	strncpy(not_found_404_end, &data_to_send[req_path_end_pos + 1], 
	            		strlen(data_to_send) - req_path_end_pos);
	            	// Null terminator
	            	not_found_404_end[strlen(data_to_send) - req_path_end_pos - 2] = '\0';
	            	// Make the new 404 Not Found html file with request path 
	            	strcpy(data_to_send, not_found_404_start);
	            	strcat(data_to_send, http_header_path);
	            	strcat(data_to_send, not_found_404_end);
	            	// Send new nofile.html
	     			write (new_fd, data_to_send, bytes_read);
	     			exit(0);
	            }
				close(new_fd);
				exit(0);
			}
		}
		close(new_fd);
	}
	return 0;
}

void redirection(char * path) {
	char * path_split_go = strtok(path, "\\");
	char * path_split_name = strtok(NULL, "\\"); 
	char redirect_path[15 + strlen(path_split_name)];
	strcpy(redirect_path, "http://www.");
	strcat(redirect_path, path_split_name);
	strcat(redirect_path, ".com");
	path = redirect_path;
}

bool getUserAgent(char * http_header) {
	char * request_message = strtok(http_header, "\n");
	while (!isUserAgent(request_message)) {
		request_message = strtok(NULL, "\n"); 
	}
	// If the HTTP header message contains mobile 
	if (strstr(request_message, "Mobile") != NULL)
		return true;
	return false;
}

bool isUserAgent(char * request_line) {
	char temp[strlen(request_line)];
	strcpy(temp, request_line);
	if (strstr(temp, "User-Agent:") != NULL) 
		return true;
	return false;
}

void isAuthenticatedClient(char * http_header) {
	char * request_line = strtok(http_header, "\n\r");
	while (request_line != NULL) {
		printf("%s", request_line);
		request_line = strtok(NULL, "\n\r");
		// Check for blank line for body entity
		if (strlen(request_line) == 0)
			break;
	}
	// ?id=yonsei&pw=network
}


