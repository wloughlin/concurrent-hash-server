#include "helpers.h"


int parse_command_to_int(const char *arg)
{
	char *end;
	int result = strtol(arg, &end, 10);
	if(*arg != 0 && *end == 0)
		return result;
	return -1;
}


void map_destroyer(map_key_t key, map_val_t val)
{
	free(key.key_base);
	free(val.val_base);
}

int open_listenfd(int port)
{
	int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;
 
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
		   (const void *)&optval , sizeof(int)) < 0)
		return -1;

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (const struct sockaddr *)&serveraddr, 
    	sizeof(serveraddr)) < 0)
		return -1;

    if (listen(listenfd, 1024) < 0)
		return -1;
    return listenfd;
}

// Wrapper for read. Will attempt to reread if EINTR is returned
int Read(int fd, void *buf, int nbytes)
{
	int ret;
	int olderrno = errno;
	errno = 0;

	reread:
	if((ret = read(fd, buf, nbytes)) < 0 && errno == EINTR) {
		errno = 0;
		goto reread;
	}
	errno = olderrno;
	return ret;
}

// Wrapper for write. Will attempt to rewrite if EINTR is returned and will
// return -2 if EPIPE is recieved
int Write(int fd, void *buf, int nbytes)
{
	int ret;
	int olderrno = errno;
	errno = 0;

	rewrite:
	if((ret = write(fd, buf, nbytes)) < 0 && errno == EINTR) {
		errno = 0;
		goto rewrite;
	}
	if(errno == EPIPE)
		ret = -2;
	errno = olderrno;
	return ret;
}

resp_function get_response_function(request_header_t hdr) 
{
	switch(hdr.request_code) {
		case PUT:
			return put_response;
		case GET:
			return get_response;
		case EVICT:
			return evict_response;
		case CLEAR:
			return clear_response;
		default:
			return invalid_request;
	}
}

void put_response(int fd, int key_size, int val_size, hashmap_t *g_map) 
{
	// check validity of key/value size
	if(key_size < MIN_KEY_SIZE || key_size > MAX_KEY_SIZE || 
		val_size < MIN_VALUE_SIZE || val_size > MAX_VALUE_SIZE) {
		bad_req_response(fd);
		return;
	}

	//malloc space for key/value
	void *key, *value;
	if((key = malloc(key_size)) == NULL) {
		bad_req_response(fd);
		return;
	}
	if((value = malloc(val_size)) == NULL) {
		free(key);
		bad_req_response(fd);
		return;
	}
	int nbytes;

	if((nbytes = Read(fd, key, key_size)) < key_size) 
		goto put_response_err;
	if((nbytes = Read(fd, value, val_size)) < val_size) 
		goto put_response_err;

	map_key_t map_key = {key, key_size};
	map_val_t map_val = {value, val_size};

	if(!put(g_map, map_key, map_val, true))
		goto put_response_err;

	response_header_t resp = {OK, 0};

	Write(fd, &resp, sizeof(response_header_t)); // not sure if this needs to be err checked
	return;

	put_response_err:
	free(key);
	free(value);
	bad_req_response(fd);
	return;
}

void get_response(int fd, int key_size, int val_size, hashmap_t *g_map)
{
	if(key_size < MIN_KEY_SIZE || key_size > MAX_KEY_SIZE) {
		bad_req_response(fd);
		return;
	}
	void *key, *buf;
	if((key = malloc(key_size)) == NULL) {
		bad_req_response(fd);
		return;
	}
	int nbytes;

	if((nbytes = Read(fd, key, key_size)) < key_size)
		goto get_response_err;

	map_key_t map_key = {key, key_size};
	map_val_t map_val = get(g_map, map_key);
	if(map_val.val_base == NULL) {
		response_header_t resp = {NOT_FOUND, 0};
		Write(fd, &resp, sizeof(response_header_t));
		free(key);
		return;
	}

	if((buf = malloc(map_val.val_len + sizeof(response_header_t))) == NULL)
		goto get_response_err;

	(*(response_header_t *)buf).response_code = OK; 
	(*(response_header_t *)buf).value_size = map_val.val_len;
	memcpy(buf+sizeof(response_header_t), map_val.val_base, 
		map_val.val_len);

	Write(fd, buf, map_val.val_len + sizeof(response_header_t));
	free(key);
	free(buf);
	return;

	get_response_err:
	free(key);
	bad_req_response(fd);
	return;
}

void clear_response(int fd, int key_size, int val_size, hashmap_t *g_map)
{
	if(!clear_map(g_map)) {
		bad_req_response(fd);
	}
	else {
		response_header_t resp = {OK, 0};
		Write(fd, &resp, sizeof(response_header_t));
	}
	return;
}

void evict_response(int fd, int key_size, int val_size, hashmap_t *g_map)
{
	if(key_size < MIN_KEY_SIZE || key_size > MAX_KEY_SIZE) {
		bad_req_response(fd);
		return;
	}
	void *key;
	if((key = malloc(key_size)) == NULL) {
		bad_req_response(fd);
		return;
	}
	int nbytes;

	if((nbytes = Read(fd, key, key_size)) < key_size) {
		free(key);
		bad_req_response(fd);
		return;
	}

	map_key_t map_key = {key, key_size};
	delete(g_map, map_key);

	response_header_t resp = {OK, 0};
	Write(fd, &resp, sizeof(response_header_t));

	free(key);
	return;
}

void invalid_request(int fd, int key_size, int val_size, hashmap_t *g_map)
{
	response_header_t resp = {UNSUPPORTED, 0};
	Write(fd, &resp, sizeof(response_header_t));
	return;
}

void bad_req_response(int fd)
{
	response_header_t resp = {BAD_REQUEST, 0};
	Write(fd, &resp, sizeof(response_header_t));
	return;
}






















