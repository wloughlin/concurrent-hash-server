#ifndef HELPER
#define HELPER

#include "cream.h"
#include "utils.h"
#include "queue.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "pthread.h"
#include "sys/socket.h"
#include <netinet/ip.h>
#include "sys/types.h"
#include "unistd.h"
#include "strings.h"
#include "errno.h"


#define USAGE "./cream [-h] NUM_WORKERS PORT_NUMBER MAX_ENTRIES\n" \
"-h                 Displays this help menu and returns EXIT_SUCCESS.\n" \
"NUM_WORKERS        The number of worker threads used to service requests.\n" \
"PORT_NUMBER        Port number to listen on for incoming connections.\n" \
"MAX_ENTRIES        The maximum number of entries that can be stored in `cream`'s underlying data store.\n" \

typedef void (*resp_function)(int, int, int, hashmap_t*);


int parse_command_to_int(const char *arg);


void map_destroyer(map_key_t key, map_val_t val);
int open_listenfd(int port);


int Read(int fd, void *buf, int nbytes);

resp_function get_response_function(request_header_t hdr);
void put_response(int fd, int key_size, int val_size, hashmap_t *g_map);
void get_response(int fd, int key_size, int val_size, hashmap_t *g_map);
void clear_response(int fd, int key_size, int val_size, hashmap_t *g_map);
void evict_response(int fd, int key_size, int val_size, hashmap_t *g_map);
void invalid_request(int fd, int key_size, int val_size, hashmap_t *g_map);
void bad_req_response(int fd);




#endif