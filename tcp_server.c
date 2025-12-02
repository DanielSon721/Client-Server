#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <time.h>
#include <sys/select.h>

#define MAX_DATASIZE 224
#define INITIALIZATION_TYPE 1
#define ACKNOWLEDGEMENT_TYPE 2
#define HASH_REQUEST_TYPE 3
#define HASH_RESPONSE_TYPE 4
#define SHA256_HASH_SIZE 32
#define MAX 80 
#define PORT 8080
#define SA struct sockaddr

struct server_arguments {
	int port;
	char *salt;
	size_t salt_len;
};

error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
	error_t ret = 0;
	switch(key) {
	case 'p':
		/* Validate that port is correct and a number, etc!! */
		args->port = atoi(arg);
		if (0 /* port is invalid */) {
			argp_error(state, "Invalid option for a port, must be a number");
		}
		break;
	case 's':
		args->salt_len = strlen(arg);
		args->salt = malloc(args->salt_len+1);
		strcpy(args->salt, arg);
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

struct server_arguments server_parseopt(int argc, char *argv[]) {
    struct server_arguments args;
    bzero(&args, sizeof(args));

    struct argp_option options[] = {
        { "port", 'p', "port", 0, "The port to be used for the server", 0 },
        { "salt", 's', "salt", 0, "The salt to be used for the server", 0 },
        { 0 }
    };

    struct argp argp_settings = { options, server_parser, 0, 0, 0, 0, 0 };
    if (argp_parse(&argp_settings, argc, argv, 0, NULL, &args) != 0) {
        printf("Error parsing arguments.\n");
    }

    // Default values if user doesn’t provide arguments
    if (!args.port) args.port = 8080;
    if (!args.salt) {
        args.salt = strdup("default");
        args.salt_len = strlen(args.salt);
    }

    printf("Got port %d and salt \"%s\" (len=%zu)\n", args.port, args.salt, args.salt_len);

    return args;
}

/* -------------------------------------------------------------------------------------------------------------------------- */

void chat_loop(int connfd) 
{ 
    char buff[MAX]; 
    int n; 
    // infinite loop for chat 
    for (;;) { 
        bzero(buff, MAX); 
  
        // read the message from client and copy it in buffer 
        read(connfd, buff, sizeof(buff)); 
        // print buffer which contains the client contents 
        printf("From client: %s\t To client : ", buff); 
        bzero(buff, MAX); 
        n = 0; 
        // copy server message in the buffer 
        while ((buff[n++] = getchar()) != '\n') 
            ; 
  
        // and send that buffer to client 
        write(connfd, buff, strlen(buff));
  
        // if msg contains "Exit" then server exit and chat ended. 
        if (strncmp("exit", buff, 4) == 0) { 
            printf("Server Exit...\n"); 
            break; 
        } 
    } 
}

int main(int argc, char *argv[]) {

    int connfd, len;
    struct sockaddr_in servaddr, cli;

    struct server_arguments args = server_parseopt(argc, argv);

    int port = args.port;
    char *salt = args.salt;
    size_t salt_len = args.salt_len;


	printf("Server starting on 0.0.0.0:%d with salt=\"%s\" (len=%zu)\n", port, salt, salt_len);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

	/* Create */
    if (server_socket == -1) {
        perror("socket creation failed...\n");
        exit(1);
    }
    else {
        printf("Socket successfully created!\n");
    }

	// assign IP, PORT 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port = htons(port); 
    
	/* Bind */
    if ((bind(server_socket, (SA*)&servaddr, sizeof(servaddr))) != 0) { 
        perror("socket bind failed");
        exit(1); 
    } 
    else {
        printf("Socket successfully binded..\n");
	}

    /* Listen */
    if ((listen(server_socket, 5)) != 0) { 
        printf("Listen failed...\n"); 
        exit(1); 
    } 
    else {
        printf("Server listening..\n"); 
	}
    len = sizeof(cli); 

    /* Accept */
    connfd = accept(server_socket, (SA*)&cli, &len); 
    if (connfd < 0) { 
        printf("server accept failed...\n"); 
        exit(1); 
    } 
    else {
        printf("server accept the client...\n"); 
	}

    chat_loop(connfd);

	/* Exit */
    close(connfd);
	close(server_socket);
}
