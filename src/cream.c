#include "helpers.h"
#include "signal.h"

hashmap_t *g_map;
queue_t *g_queue;



void *worker_thread(void *arg)
{
	request_header_t req_header;
	int *conn_fdp, nbytes;
	while(1) {
		conn_fdp = dequeue(g_queue);
		bzero(&req_header, sizeof(request_header_t));

		// read request
		if((nbytes = Read(*conn_fdp, &req_header, 
			sizeof(request_header_t))) < sizeof(request_header_t)) {
			if(nbytes >= 0) { 	
				invalid_request(*conn_fdp, 0, 0, NULL);
			}
			else if(nbytes == -1) {
				bad_req_response(*conn_fdp);
			}
		}
		else {
			(get_response_function(req_header))(*conn_fdp, 
				req_header.key_size, req_header.value_size, g_map);
		}
		close(*conn_fdp);
		free(conn_fdp);
	}
}


int main(int argc, char *argv[]) {

	int num_workers, port_number, max_entries;
	if(argc <= 1)
		goto cream_invalid_cl;

	if(!strcmp(argv[1], "-h")) {
		printf(USAGE);
		exit(0);
	}
	if(argc != 4)
		goto cream_invalid_cl;
	if((num_workers = parse_command_to_int(argv[1])) <= 0)
		goto cream_invalid_cl;
	if((port_number = parse_command_to_int(argv[2])) <= 0)
		goto cream_invalid_cl;
	if((max_entries = parse_command_to_int(argv[3])) <= 0)
		goto cream_invalid_cl;

	if((g_map = create_map(max_entries, jenkins_one_at_a_time_hash, 
		map_destroyer)) == NULL)
		exit(3);
	if((g_queue = create_queue()) == NULL)
		goto cream_cleanup_err_3;

	pthread_t *threads;
	if((threads = malloc(sizeof(pthread_t) * num_workers)) == NULL) {
		goto cream_cleanup_err_2;
	}

	for(int i = 0; i < num_workers; i++) {
		if(pthread_create(&threads[i], NULL, worker_thread, NULL) < 0 )
			goto cream_cleanup_err_1;
	}

	// block sigpipe
	sigset_t sig_pipe;
	sigemptyset(&sig_pipe);
	sigaddset(&sig_pipe, SIGPIPE);
	sigprocmask(SIG_BLOCK, &sig_pipe, NULL);

	int listen_fd, *conn_fdp;
	socklen_t client_len;
	struct sockaddr_storage client_addr;

	if((listen_fd = open_listenfd(port_number)) < 0)
		goto cream_cleanup_err_1;

	while(1) {
		client_len = sizeof(struct sockaddr_storage);
		conn_fdp = malloc(sizeof(int));
		if((*conn_fdp = accept(listen_fd, (struct sockaddr *)&client_addr, 
			&client_len)) >= 0) {
			enqueue(g_queue, conn_fdp);
		}
	}


	//cream_cleanup:
	free(threads);
	free(g_map);
	free(g_queue);
    exit(0);

    cream_invalid_cl:
    fprintf(stderr, USAGE);
    exit(1);

    cream_cleanup_err_1:
    free(threads);
    cream_cleanup_err_2:
    free(g_queue);
    cream_cleanup_err_3:
    free(g_map);
    exit(2);




}
