#include "utils.h"
#include "errno.h"
#include "strings.h"
#include "string.h"
#include "debug.h"

#define MAP_KEY(base, len) (map_key_t) {.key_base = base, .key_len = len}
#define MAP_VAL(base, len) (map_val_t) {.val_base = base, .val_len = len}
#define MAP_NODE(key_arg, val_arg, tombstone_arg) (map_node_t) {.key = key_arg, .val = val_arg, .tombstone = tombstone_arg}



bool key_equals(map_key_t one, map_key_t two)
{
	if(one.key_len != two.key_len)
		return false;

	if(strncmp(one.key_base, two.key_base, one.key_len))
		return false;
	return true;
}

hashmap_t *create_map(uint32_t capacity, hash_func_f hash_function, destructor_f destroy_function) {

	hashmap_t *hmap;

    // check args
    if(capacity == 0 || hash_function == NULL || destroy_function == NULL) {
    	errno = EINVAL;
    	return NULL;
    }

    // allocate space for the hashmpa
    if((hmap = calloc(1, sizeof(hashmap_t))) == NULL)
    	return NULL;

    hmap->capacity = capacity;
    hmap->size = 0;
    hmap->hash_function = hash_function;
    hmap->destroy_function = destroy_function;
    hmap->num_readers = 0;
    hmap->invalid = false;

    if(pthread_mutex_init(&(hmap->write_lock), NULL))
    	goto hmap_after_alloc_error;

    if(pthread_mutex_init(&(hmap->fields_lock), NULL))
    	goto hmap_after_alloc_error;

    if((hmap->nodes = calloc(capacity, sizeof(map_node_t))) == NULL)
    	goto hmap_after_alloc_error;

    return hmap;
    hmap_after_alloc_error:
    free(hmap);
    return NULL;
}

bool put(hashmap_t *self, map_key_t key, map_val_t val, bool force) {

    // check args
    if(self == NULL || key.key_base == NULL || key.key_len == 0 ||
    	val.val_base == NULL || val.val_len == 0 || self->invalid) {
    	errno = EINVAL;
    	return false;
    }

    // lock the map for writing
    if(pthread_mutex_lock(&(self->write_lock)))
    	return false;

    if(self->invalid) { // check that map hasnt been invalidated since last check
    	pthread_mutex_unlock(&(self->write_lock));
    	errno = EINVAL;
    	return false;
    }

    int index = get_index(self, key);

    if(self->capacity == self->size) {
    	if(!force) {
	    	pthread_mutex_unlock(&(self->write_lock));
	    	errno = ENOMEM;
	    	return false;
	    }
	    self->destroy_function((self->nodes+index)->key, (self->nodes+index)->val);
	    (self->nodes+index)->key = key;
	    (self->nodes+index)->val = val;
	    pthread_mutex_unlock(&(self->write_lock));
	    return true;
    }

    // search for an open sopt or tombstone
    while((self->nodes+index)->key.key_base != NULL &&
    	!((self->nodes+index)->tombstone)) {
    	if(key_equals((self->nodes+index)->key, key)) {
    		self->destroy_function((self->nodes+index)->key, (self->nodes+index)->val);
    		break;
    	}
		index = (index+1) % self->capacity;
    }

    (self->nodes+index)->key = key;
	(self->nodes+index)->val = val;
	(self->nodes+index)->tombstone = false;
	self->size++;
	pthread_mutex_unlock(&(self->write_lock));
	return true;
}

map_val_t get(hashmap_t *self, map_key_t key) {

	// check args
	if(self == NULL || self->invalid ||
		key.key_base == NULL || key.key_len == 0) {
		errno = EINVAL;
		return MAP_VAL(NULL, 0);
	}

	// aquire feilds lock
	if(pthread_mutex_lock(&(self->fields_lock)))
		return MAP_VAL(NULL, 0);

	if(self->num_readers == 0) {
		// aquire write lock
		if(pthread_mutex_lock(&(self->write_lock))){
			pthread_mutex_unlock(&(self->fields_lock));
			return MAP_VAL(NULL, 0);
		}
		// recheck validity
		if(self->invalid) {
			pthread_mutex_unlock(&(self->write_lock));
			pthread_mutex_unlock(&(self->fields_lock));
			return MAP_VAL(NULL, 0);
		}
	}
	self->num_readers++;
	pthread_mutex_unlock(&(self->fields_lock));

	int index = get_index(self, key);
	map_val_t ret = MAP_VAL(NULL, 0);
	map_node_t *node;

	for(int i = 0; i < self->capacity; i++) {
		node = self->nodes+((index+i) % self->capacity);
		if (node->key.key_len == 0) {
			if(node->tombstone)
				continue;
			else
				break;
		}
		else if(key_equals(node->key, key)) {
			ret = MAP_VAL(node->val.val_base, node->val.val_len);
			debug("found");
			break;
		}
	}

	// reaquire fields lock
	pthread_mutex_lock(&(self->fields_lock));
	self->num_readers--;

	// release write lock if no other readers are running
	if(self->num_readers == 0)
		pthread_mutex_unlock(&(self->write_lock));

	// release fields lock
	pthread_mutex_unlock(&(self->fields_lock));

    return ret;
}

map_node_t delete(hashmap_t *self, map_key_t key) {

	if(self == NULL || key.key_base == NULL ||
		key.key_len == 0 || self->invalid) {
		errno = EINVAL;
		return MAP_NODE(MAP_KEY(NULL, 0), MAP_VAL(NULL, 0), false);
	}

	if(pthread_mutex_lock(&(self->write_lock)))
		return MAP_NODE(MAP_KEY(NULL, 0), MAP_VAL(NULL, 0), false);

	int index = get_index(self, key);
	map_node_t *node, *to_remove;
	to_remove = NULL;

	for(int i = 0; i < self->capacity; i++) {
		node = self->nodes+((index+i) % self->capacity);
		if (node->key.key_len == 0) {
			if(node->tombstone)
				continue;
			else
				break;
		}
		else if(key_equals(node->key, key)) {
			to_remove = node;
			break;
		}
	}

	map_node_t ret;

	if(to_remove == NULL) {
		debug("not found: %i", *(int *)key.key_base);
		ret = MAP_NODE(MAP_KEY(NULL, 0), MAP_VAL(NULL, 0), false);
	}
	else {
		ret = *node;
		bzero(node, sizeof(map_node_t));
		node->tombstone = true;
		self->size--;
	}

	pthread_mutex_unlock(&(self->write_lock));
	return ret;
}

bool clear_map(hashmap_t *self) {

	if(self == NULL || self->invalid) {
		errno = EINVAL;
		return false;
	}

	if(pthread_mutex_lock(&(self->write_lock)))
		return false;

	if(self->invalid) {
		pthread_mutex_unlock(&(self->write_lock));
		errno = EINVAL;
		return false;
	}

	bzero(self->nodes, sizeof(map_node_t) * self->capacity);

	self->size = 0;

	pthread_mutex_unlock(&(self->write_lock));
	return true;
}

bool invalidate_map(hashmap_t *self) {

    if(self == NULL || self->invalid) {
		errno = EINVAL;
		return false;
	}

	if(pthread_mutex_lock(&(self->write_lock)))
		return false;

	if(self->invalid) {
		pthread_mutex_unlock(&(self->write_lock));
		errno = EINVAL;
		return false;
	}

	map_node_t *node;
	for(int i = 0; i < self->capacity; i++) {
		node = self->nodes+i;
		if(node->key.key_base != NULL)
			(self->destroy_function)(node->key, node->val);
	}

	free(self->nodes);
	self->invalid = true;

	pthread_mutex_unlock(&(self->write_lock));
	return true;

}





















