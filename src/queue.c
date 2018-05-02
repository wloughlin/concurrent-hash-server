#include "queue.h"
#include "errno.h"

queue_t *create_queue(void) {

	void *ptr;
	if((ptr = calloc(1, sizeof(queue_t))) == NULL) // initialize queue
		return NULL;

	queue_t *queue_ptr = (queue_t *)ptr; 

	// Initialize node pointers
	queue_ptr->front = NULL;
	queue_ptr->rear = NULL;
	queue_ptr->invalid = false;

	// initialize item semaphore
	if(sem_init(&(queue_ptr->items), 0, 0)) {
		free(queue_ptr);
		return NULL;
	}

	// initialize queue lock
	if(pthread_mutex_init(&(queue_ptr->lock), NULL)) {
		free(queue_ptr);
		return NULL;
	}

    return queue_ptr;
}

bool invalidate_queue(queue_t *self, item_destructor_f destroy_function) {

	//check args
	if(self == NULL || self->invalid || destroy_function == NULL)
		goto queue_invalidate_err;

	// aquire lock
	if(pthread_mutex_lock(&(self->lock)))
		goto queue_invalidate_err;

	// iterate through queue, destroying node items and freeing nodes
	queue_node_t *node = self->front;
	queue_node_t *temp;
	while(node != NULL) {
		(*destroy_function)(node->item);
		temp = node->next;
		free(node);
		node = temp;
	}

	self->invalid = true;
	pthread_mutex_unlock(&(self->lock));
    return true;
    queue_invalidate_err:
    errno = EINVAL;
    return false;
}

bool enqueue(queue_t *self, void *item) {

	queue_node_t *new_node;
	// check arguements
	if(self == NULL || item == NULL || self->invalid)
		goto enqueue_err_no_lock;

	// aquire lock on queue
	if(pthread_mutex_lock(&(self->lock)))
		goto enqueue_err_no_lock;

	// error if queue is invalid
	if(self->invalid) 
		goto enqueue_err;

	// calloc space for new node
	if((new_node = calloc(1, sizeof(queue_node_t))) == NULL)
		goto enqueue_err;

	// initialize node
	new_node->item = item;
	new_node->next = NULL;

	// place the item in the queue
	if(self->front == NULL) 
		self->front = new_node;
	else 
		self->rear->next = new_node;
	self->rear = new_node;

	// increment the number of items in the queue
	if(sem_post(&(self->items)))
		goto enqueue_err;

	pthread_mutex_unlock(&(self->lock));
    return true;

    // Cleaup after errors
	enqueue_err:
	pthread_mutex_unlock(&(self->lock));
	enqueue_err_no_lock:
	errno = EINVAL;
	return false;
}

void *dequeue(queue_t *self) {

	// check args
	if(self == NULL)
		goto dequeue_err_no_lock;

	// wait for an item to become available
	if(sem_wait(&self->items))
		goto dequeue_err_no_lock;
	
	// aquire lock on queue
	if(pthread_mutex_lock(&(self->lock)))
		goto dequeue_err_no_lock;

	if(self->invalid)
		goto dequeue_err;

	queue_node_t *front = self->front;
	void *item = front->item;
	if(self->rear == front) // in the case of 1 item, set rear to null
		self->rear = NULL;

	self->front = front->next;
	free(front);

	pthread_mutex_unlock(&(self->lock));

    return item;

    dequeue_err:
    pthread_mutex_unlock(&(self->lock));
    dequeue_err_no_lock:
    errno = EINVAL;
    return NULL;
}





















