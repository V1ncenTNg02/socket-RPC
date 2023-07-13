#include "rpc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <assert.h>
#include <pthread.h>
#define NONBLOCKING

#define MAX_CLIENT 20
#define MAX_HANDLE 100
#define FIND_INDEX 10001
#define CALL_INDEX 10002
#define MAX_DATA2_LEN 100000
#define MAX_HANDLER 100
#define MAX_FUNCTION_NAME_LEN 1001
#define PORT_NUM_LEN 10

struct rpc_handle {
    char* func_name;
    rpc_handler* handler;
};

struct rpc_server {
    int newsockfd;
    rpc_handle handles[MAX_HANDLER];
    int num_function;
    int sockfd;
};

struct rpc_client {
    int sockfd;
    char func_name[MAX_HANDLER][MAX_FUNCTION_NAME_LEN];
    int func_num;
    struct rpc_handle* handles[MAX_HANDLER];
};


int create_listening_socket(char* service);
uint64_t HostToNetwork(uint64_t data_host_order);
uint64_t NetworkToHost(u_int64_t data_network_order);
void* handle_thread(void* server);

/*Initial rpc-server*/
rpc_server *rpc_init_server(int port) {
    int sockfd;
    char port_num[PORT_NUM_LEN];
    sprintf(port_num, "%d",port);
    sockfd = create_listening_socket(port_num);
    //create server and allocate memory for it
    struct rpc_server* server = (struct rpc_server * )malloc(sizeof(struct rpc_server));
    if(server == NULL){
        return NULL;
    }
    else{  
        server -> num_function = 0;
        server -> sockfd = sockfd;
        return server;
    }
}

/*Register function in a server*/
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    //check if any of parameters is null
    if(srv == NULL || name == NULL || handler == NULL){
        return -1;
    }
    int name_len = strlen(name);
    
    //Store handlers in rpc-server based on conditions
    if(srv -> num_function == 0){
        //Allocate memory and register the first handler in the rpc-server
        srv -> handles[srv -> num_function].handler = (rpc_handler*)malloc(sizeof(rpc_handler));
        memcpy(srv -> handles[srv -> num_function].handler,&handler,sizeof(rpc_handler));
        srv -> handles[srv -> num_function].func_name = (char*)malloc(name_len * sizeof(char));
        strcpy(srv -> handles[srv -> num_function].func_name,name);
        srv -> num_function += 1;
	
        return 1;
    }
    else{
        int is_same = 0;
        int same_pos = 0;
        for(int i = 0; i < srv -> num_function;i++){
            if(strcmp(name,srv -> handles[i].func_name) == 0){
                is_same = 1;
                same_pos = i;
            }
        }
        if(is_same == 0){
            // Register the new handler has no same name as handlers in the rpc-server
            srv -> handles[srv -> num_function].handler = (rpc_handler*)malloc(sizeof(rpc_handler));
            memcpy(srv -> handles[srv -> num_function].handler,&handler,sizeof(rpc_handler));
            srv -> handles[srv -> num_function].func_name = (char*)malloc(name_len * sizeof(char));
            strcpy(srv -> handles[srv -> num_function].func_name,name);
            srv -> num_function += 1;
	        return 1;
        }
        else{
            // Register the new handler with same name as another handler in the server, replace it. 
            memcpy(srv -> handles[same_pos].handler, &handler, sizeof(rpc_handler));
            return 2;
        }
    }
    return -1;
}

/*Serve all the requests from clients*/
void rpc_serve_all(rpc_server *srv) {
    //Variables for socket
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_size;
    client_addr_size = sizeof(client_addr);
    
    // Listen on socket
    if (listen(srv -> sockfd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    //Let server keep accepting data
    while(1){
         //Accept client
        srv -> newsockfd =
            accept(srv -> sockfd, (struct sockaddr*)&client_addr, &client_addr_size);
        if (srv -> newsockfd < 0) {
            continue;
        }
        else{
            //Create thread for each client accepted
            pthread_t t;
            pthread_create(&t,NULL,handle_thread,(void*)srv);
            pthread_detach(t);
        }
    }
} 

/*Function handling requests in every thread*/
void* handle_thread(void* server){
    rpc_server* srv = (rpc_server*) server;
    int newsockfd = srv->newsockfd;
    //keep server running
    while(1){
        int found = 0;
        int read_num;
        uint64_t found64;
        uint64_t read_num64;
        int text_len;
        uint64_t text_len64;
        //Read request index from client.
        if(read(newsockfd, &read_num64, sizeof(uint64_t)) == 0){
            close(newsockfd);
            break;
        }

        else{
            read_num = (int)(NetworkToHost(read_num64));
        }
        
        //Read length of function name
        read(newsockfd, &text_len64, sizeof(uint64_t));
        text_len = (int)(NetworkToHost(text_len64));

        //Read function name
        char* read_text = (char*)malloc((text_len+1)*sizeof(char));
        read(newsockfd, read_text, text_len * sizeof(char));

        read_text[text_len] = '\0';
        
        if(read_num == FIND_INDEX){
            // handle find request
            for(int i = 0; i<srv->num_function;i++){
                if(strcmp(read_text,srv -> handles[i].func_name) == 0){
                    found = 1;
                    found64 = HostToNetwork(((uint64_t)found));
                    write(newsockfd,&found64,sizeof(uint64_t));
                    write(newsockfd, &(srv -> handles[i]), sizeof(struct rpc_handle*));
                    break;
                }           
            }
            if(found == 0){
                found64 = HostToNetwork(((uint64_t)found));
                write(newsockfd,&found64, sizeof(uint64_t));
            }
        }
        else{
            //handle call request
            int valid = 0;
            uint64_t valid64;
            //allocate memory for client's rpc_data
            rpc_data* in = (rpc_data*)malloc(sizeof(rpc_data));
            rpc_data* out;
            uint64_t in_data1_64,out_data1_64;
            //read data1 from client
            read(newsockfd, &(in_data1_64), sizeof(uint64_t));
            in->data1 = (int)(NetworkToHost(in_data1_64));
            //read data2 len from client
            read(newsockfd, &(in -> data2_len), sizeof(size_t));
            //handle different situation based on data2 len
            if(in -> data2_len == 0){
                in->data2 = NULL;
            }
            else if(in->data2_len > MAX_DATA2_LEN){
                perror("Overlength error");
                exit(EXIT_FAILURE);
            }
            else{
                in-> data2 = malloc(in->data2_len);   
                read(newsockfd, in -> data2, in->data2_len);
            }
            //apply function on data read
            for(int i = 0; i<srv->num_function;i++){
                if(strcmp(read_text,srv -> handles[i].func_name) == 0){
                    out = (*srv -> handles[i].handler)(in);
                }
            }
            if(out == NULL){
                // check if result is null
                valid64 = HostToNetwork(((uint64_t)valid));
                write(newsockfd,&valid64,sizeof(uint64_t));
            }
            else if((out -> data2_len == 0 && out->data2 != NULL) ||(out->data2_len != 0 && out->data2 == NULL)){
                // check if result is valid 
                valid64 = HostToNetwork(((uint64_t)valid));
                write(newsockfd,&valid64,sizeof(uint64_t));
            }
            else{
                //send rpc_data back to client
                valid = 1;
                valid64 = HostToNetwork(((uint64_t)valid));
                write(newsockfd,&valid64,sizeof(uint64_t));
                out_data1_64 = HostToNetwork(((uint64_t)out->data1));
                write(newsockfd,&out_data1_64, sizeof(uint64_t));
                uint64_t data2_len_64 = HostToNetwork(((uint64_t)out->data2_len));
                write(newsockfd,&(data2_len_64), sizeof(uint64_t));
                if(out->data2_len != 0){
                    // only send data2 to client if data2 len is not 0
                    write(newsockfd,out -> data2, out->data2_len);  
                }
            }
            free(in);
        }
        free(read_text);
    }
    return NULL;
}

/*Create rpc-client*/
rpc_client *rpc_init_client(char *addr, int port) {
    int sockfd,  s; 
	struct addrinfo hints, *servinfo, *rp;
    struct rpc_client* client = (struct rpc_client*)malloc(sizeof(struct rpc_client));

    if(client == NULL){
        return NULL;
    }

    //set information for socket of client
    client -> func_num = 0;
	char port_num[PORT_NUM_LEN];
    sprintf(port_num,"%d",port);
    // Create address
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

	// Get addrinfo of server
	s = getaddrinfo(addr, port_num, &hints, &servinfo);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	//create socket and store it in rpc-client
	for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sockfd == -1)
			continue;
		if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break; // success

		close(sockfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(servinfo); 
    client -> sockfd = sockfd;
    return client;
}

/*find handler in the rpc-server*/
rpc_handle *rpc_find(rpc_client *cl, char *name) {
    int len_name = 0;
    int n;
    len_name = strlen(name);
    uint64_t f_type = HostToNetwork(((uint64_t)FIND_INDEX));

    //Send request find index to server
    n = write(cl -> sockfd,&f_type,sizeof(uint64_t));
    if (n < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
    //Send function name length to server 
    uint64_t len_name64 = HostToNetwork(((uint64_t)len_name));
    n = write(cl -> sockfd, &len_name64, sizeof(uint64_t));
    if (n < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
    //Send function name to server
    n = write(cl -> sockfd, name, len_name *sizeof(char));
	if (n < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
    
    // Read message from server
    int read_index;
    uint64_t read_index64;
    //Read index from server
    read(cl -> sockfd, &read_index64, sizeof(uint64_t));
    read_index = (int)(NetworkToHost(read_index64));
    if(read_index == 0){
        //Handle call failure from server
        return NULL;
    }
    else{
        //If call succeeds, read address of the handler from server
        rpc_handle* handle = (rpc_handle*)malloc(sizeof(rpc_handle));
	    n = read(cl -> sockfd, handle, sizeof(rpc_handle*));
	    if (n < 0) {
		    perror("read");
		    exit(EXIT_FAILURE);
	    }
        strcpy(cl->func_name[cl->func_num],name);
        cl->handles[cl->func_num] = handle;
        cl->func_num = cl->func_num + 1;
        return handle;
    }
}
/*Client call handler in the server*/
rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    //Return null if any of arguments is null
    if(cl == NULL||h == NULL||payload == NULL){
        return NULL;
    }
    //Return null when data2 length and data2 are inconsistent
    if ((!payload->data2 && payload->data2_len != 0) || (payload->data2 && payload->data2_len == 0)) {
        return NULL;
    }
    int len_name = 0;
    int n;
    char func_name[1001];
    for(int i = 0; i<cl->func_num;i++){
        if(cl->handles[i] == h){
            strcpy(func_name,cl->func_name[i]);
            len_name = strlen(cl->func_name[i]);
        }
    }
    //Send find index to server
    int f_type = CALL_INDEX;
    uint64_t f_type64 = HostToNetwork(((uint64_t)f_type));
    rpc_data* accept_data = (rpc_data*)malloc(sizeof(rpc_data));

    n = write(cl -> sockfd,&f_type64,sizeof(uint64_t));
    if (n < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
    //Send function name length to server
    uint64_t len_name64 = HostToNetwork(((uint64_t)len_name));
    n = write(cl -> sockfd, &len_name64, sizeof(uint64_t));
    if (n < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
    //Send function name to server
    n = write(cl -> sockfd, func_name, len_name *sizeof( char));
	if (n < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
    //Send rpc-data to server
    uint64_t data1_64 = HostToNetwork(((uint64_t)payload -> data1));
    write(cl -> sockfd, &(data1_64), sizeof(uint64_t));
    write(cl -> sockfd, &(payload -> data2_len), sizeof(size_t));
    if(payload -> data2_len != 0){
        write(cl -> sockfd, payload -> data2,payload -> data2_len);
    }
    int valid;
    uint64_t valid_64;
    //Read validity index from server
    read(cl -> sockfd, &valid_64,sizeof(uint64_t));
    valid = (int)(NetworkToHost(valid_64));
    if(valid == 0){
        //Return null if validity index is invalid
        return NULL;
    }
    uint64_t acc_data1_64;
    // read rpc-data from server
    read(cl -> sockfd, &(acc_data1_64),sizeof(uint64_t));
    accept_data -> data1 = (int)(NetworkToHost(acc_data1_64));
    uint64_t acc_data2_len_64;
    read(cl->sockfd,&(acc_data2_len_64),sizeof(uint64_t));
    accept_data ->data2_len = (int)(NetworkToHost(acc_data2_len_64));
    
    if(accept_data->data2_len == 0){
        // Assign null to data2 if data2 len is 0
        accept_data -> data2 = NULL;
    }
    else{
        // Read data2 from server
        accept_data -> data2 = malloc(accept_data->data2_len);
        read(cl->sockfd,accept_data->data2,accept_data -> data2_len);
    }
    return accept_data;
}

/*Close client socket and free memory of rpc-client*/
void rpc_close_client(rpc_client *cl) {
    close(cl->sockfd);
    free(cl);
}

/*free rpc-data*/
void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

/*create listening socket in server*/
int create_listening_socket(char* service) {
	int re, s, sockfd;
	struct addrinfo hints, *res;

	// Create address we're going to listen on
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;       // IPv6
	hints.ai_socktype = SOCK_STREAM; 
	hints.ai_flags = AI_PASSIVE;
	s = getaddrinfo(NULL, service, &hints, &res);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	// Create socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Reuse port if possible
	re = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(int)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

    // required by spec
    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

	// Bind address to the socket
	if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(res);

	return sockfd;
}

/*Convert data from host byte order to network byte order*/
uint64_t HostToNetwork(uint64_t data_host_order){
    uint64_t data_network_order;
    const uint64_t one = 1;
    const int littleEndian = *((const uint8_t*)(&one)) == 1;
    if(littleEndian){
        data_network_order = htobe64(data_host_order);
    }
    else{
        data_network_order = data_host_order;
    }
    return data_network_order;
}

/*Convert data from network byte order to host byte order*/
uint64_t NetworkToHost(u_int64_t data_network_order){
    uint64_t data_host_order;
    const uint64_t one = 1;
    const int littleEndian = *((const uint8_t*)(&one)) == 1;
    if(littleEndian){
        data_host_order = be64toh(data_network_order);
    }
    else{
        data_host_order = data_network_order;
    }
    return data_host_order;
}

