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
#define SA struct sockaddr

struct client_arguments {
	char ip_address[16]; /* You can store this as a string, but I probably wouldn't */
	int port; /* is there already a structure you can store the address
	           * and port in instead of like this? */
	int hashnum;
	int smin;
	int smax;
	char *filename; /* you can store this as a string, but I probably wouldn't */
};

error_t client_parser(int key, char *arg, struct argp_state *state) {
	struct client_arguments *args = state->input;
	error_t ret = 0;
	int len;
	switch(key) {
	case 'a':
		/* validate that address parameter makes sense */
		strncpy(args->ip_address, arg, 16);
		if (0 /* ip address is goofytown */) {
			argp_error(state, "Invalid address");
		}
		break;
	case 'p':
		/* Validate that port is correct and a number, etc!! */
		args->port = atoi(arg);
		if (0 /* port is invalid */) {
			argp_error(state, "Invalid option for a port, must be a number");
		}
		break;
	case 'n':
		/* validate argument makes sense */
		args->hashnum = atoi(arg);
		break;
	case 300:
		/* validate arg */
		args->smin = atoi(arg);
		break;
	case 301:
		/* validate arg */
		args->smax = atoi(arg);
		break;
	case 'f':
		/* validate file */
		len = strlen(arg);
		args->filename = malloc(len + 1);
		strcpy(args->filename, arg);
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}


struct client_arguments client_parseopt(int argc, char *argv[]) {
	struct argp_option options[] = {
		{ "addr", 'a', "addr", 0, "The IP address the server is listening at", 0},
		{ "port", 'p', "port", 0, "The port that is being used at the server", 0},
		{ "hashreq", 'n', "hashreq", 0, "The number of hash requests to send to the server", 0},
		{ "smin", 300, "minsize", 0, "The minimum size for the data payload in each hash request", 0},
		{ "smax", 301, "maxsize", 0, "The maximum size for the data payload in each hash request", 0},
		{ "file", 'f', "file", 0, "The file that the client reads data from for all hash requests", 0},
		{0}
	};

	struct argp argp_settings = { options, client_parser, 0, 0, 0, 0, 0 };

	struct client_arguments args;
	bzero(&args, sizeof(args));

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, &args) != 0) {
		printf("Got error in parse\n");
	}

    return args;
}

/* -------------------------------------------------------------------------------------------------------------------------- */

void chat_loop(int client_socket) {
    char buff[MAX_DATASIZE];
    int n;
    for (;;) {
        bzero(buff, sizeof(buff));
        printf("Enter the string : ");
        n = 0;
        while ((buff[n++] = getchar()) != '\n');
        write(client_socket, buff, strlen(buff));
        bzero(buff, sizeof(buff));
        read(client_socket, buff, sizeof(buff));
        printf("From Server : %s", buff);
        if ((strncmp(buff, "exit", 4)) == 0) {
            printf("Client Exit...\n");
            break;
        }
    }
}

int main(int argc, char *argv[]) {

    struct client_arguments args = client_parseopt(argc, argv);

    char *server_ip_address = args.ip_address;
    int server_port = args.port;
    int num_hashes = args.hashnum;
    int min_size = args.smin;
    int max_size = args.smax;
    char *input_file = args.filename;

	printf("Got %s on port %d with n=%d smin=%d smax=%d filename=%s\n",
	       server_ip_address, server_port, num_hashes, min_size, max_size, input_file);

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);

	/* Create */
    if (client_socket == -1) {
        perror("socket creation failed...\n");
        exit(1);
    }
    else {
        printf("Socket successfully created!\n");
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(server_port);

	/* Connect */
    if (connect(client_socket, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        perror("Failed to connect to server");
        exit(1);
    }
    else {
        printf("Successfully connected to server!\n");
    }
    
    chat_loop(client_socket);

	/* Exit */
	close(client_socket);
    if (input_file) free(input_file);
    return 0;
}