#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>


#define BUFFER_SIZE 8192
#define BYTES_PER_LINE 16
#define DEFAULT_THREADS 1
#define CHUNK_SIZE 4096


// Create a struct for each task
typedef struct {
    size_t start; // Start byte of the chunk
    size_t end; // End byte of the chunk
    char *addr; // address of the chunk
    char *result; // encoded result of the chunk
    size_t result_len; // length in bytes of the encoded result
    char last_char; // last char of encoded result
    unsigned int last_count; // last count of encoded char
} Task;

typedef struct{
    Task **tasks; // Array of pointers to tasks
    int front, rear, count; // Initialize variables for accessing queue data
    int capacity; // Initial capacity of queue which will be increased if necessary
    pthread_cond_t condition; // Condidtion variable to block a thread when queue is empty allowing other tasks to be queued
    pthread_mutex_t mutex; // Mutex for locking access
} TaskQueue;


int num_thread = DEFAULT_THREADS; // Global num_thread variable
pthread_t *threads; // Array of working threads in pool
TaskQueue *task_queue; // Create a global task queue
TaskQueue *completed_queue; // Create a global completed queue
int num_queued_tasks = 0; // Total tasks created
int tasks_remaining = 0; // Count of tasks remaining. Used for tracking in main thread and joining in order

volatile int stop_running = 0; // Flag to stop threads 


void resize_task_queue(TaskQueue *queue){
    int new_capacity = 2*queue->capacity;

    Task **new_tasks = malloc(sizeof(Task *) * new_capacity);
    if (!new_tasks) {
        fprintf(stderr, "Error: resizing of queue failed");
        exit(1);
    }

    // Re add tasks from the old array to the new array
    // Tasks are reset to start at 0th index (front)
    for (int i = 0; i < queue->count; i++) {
        int index = (queue->front + i) % queue->capacity;
        new_tasks[i] = queue->tasks[index];
    }

    
    // Free the old array and update queue properties
    free(queue->tasks);
    queue->tasks = new_tasks;
    queue->capacity = new_capacity;
    queue->front = 0;
    queue->rear = queue->count;  // Rear should point to the next available slot in the resized array
}

// Adds task to the task queue
void queue_task(TaskQueue *queue, Task *t){
    // Lock mutex
    pthread_mutex_lock(&queue->mutex);
    // if at max capacity resize the queue
    while(queue->count == queue->capacity){
        resize_task_queue(queue);
    }
    // add task to end of queue
    queue->tasks[queue->rear] = t;
    // // Increase rear counter by 1 and account for overflow of capacity
    queue->rear = (queue->rear+1) % queue->capacity;
    queue->count++;

    // If it is a new task (not completed) add to task remaining and num
    tasks_remaining++;
    num_queued_tasks++;

    pthread_cond_signal(&queue->condition); // Signal that a new task has been queued
    pthread_mutex_unlock(&queue->mutex); // Unlock mutex
}

// Adds task to the task queue at specified index
void queue_task_at(TaskQueue *queue, Task *t, size_t index){
    // Lock mutex
    pthread_mutex_lock(&queue->mutex);
    // if at max capacity resize the queue
    while(queue->count == queue->capacity || index >= (size_t)queue->capacity){
        resize_task_queue(queue);
    }
    // add task to the specific index
    queue->tasks[index] = t;
    // Increase rear counter by 1 and account for overflow of capacity
    if(index >= (size_t)queue->rear){
        queue->rear = index % queue->capacity;
    }
    queue->count++;
    pthread_mutex_unlock(&queue->mutex); // Unlock mutex
}

// Takes a task off of the queue
Task* dequeue_task(TaskQueue *queue){
    // Lock mutex
    pthread_mutex_lock(&queue->mutex);

    // Wait while count is 0 and not stopping threads
    // if not for wait, we could immediately try to access the queue which may be empty
    // so we use wait to give up access to the mutex
    // when we recieve signal on the cond, go to top of while loop, check if the count is not zero or thread has been signaled to stop
    //      if either is false (there is a task or thread should stop), keep mutex locked and handle the retrieval of a task
    //      if they are still true (no tasks and still running) then we give back up the mutex and keep waiting
    while(queue->count == 0 && !stop_running){
        pthread_cond_wait(&queue->condition, &queue->mutex);
    }
    // If signaled to stop and queue is still empty
    if(stop_running && queue->count == 0){
        pthread_cond_signal(&queue->condition); // Send signal to next
        pthread_mutex_unlock(&queue->mutex); // Unlock the mutex before stopping so other threads can stop
        return NULL;
    }
    // otherwise queue must not be empty, return task
    Task *task = queue->tasks[queue->front];
    queue->front = (queue->front+1) % queue->capacity; // move front to next task
    queue->count--; // decrease count

    // Because this is consuming, we do not need to signal
    // the signal is only to block a thread when the queue is empty
    pthread_mutex_unlock(&queue->mutex); // unlock queue mutex
    return task;
}

// Initializes a task queue and returns it
TaskQueue* create_task_queue(int initial_capacity){
    TaskQueue *queue = malloc(sizeof(TaskQueue));
    queue->count = queue->front = queue->rear = 0; // Initialize the vars to 0
    queue->capacity = initial_capacity;
    queue->tasks = malloc(sizeof(Task*) * initial_capacity);
    pthread_mutex_init(&queue->mutex, NULL); // Initialize the mutex
    pthread_cond_init(&queue->condition, NULL);  // Initialize the condition variable
    return queue;
}

void *worker_thread(void* arg){
    // cast arg to the task queue
    TaskQueue *queue = (TaskQueue *)arg;  


    // Continue to take tasks and encode them
    while(1){
        Task *t = dequeue_task(queue);
        // If no more tasks, exit thread
        if(t == NULL){
            pthread_cond_signal(&queue->condition);
            break;
        }

        unsigned int count = 0;
        int len = 0;
        char cur = '\0';

        // allocate a buffer to store result
        t->result = malloc(BUFFER_SIZE);
        if (t->result == NULL) {
            fprintf(stderr, "Error: memory allocation for result failed\n");
            exit(1);
        }

        // DO THE ENCODING HERE
        for(size_t i = t->start; i < t->end; i++){
            if(i==t->start){
                // If the beginning of the file, set first char
                cur = (t->addr)[i];
                count = 1;
            }else if(cur != (t->addr)[i]){ // if encounted a new character
                // create encoding of the last chars
                if (len < BUFFER_SIZE - 2) {
                    len += snprintf((t->result) + len, BUFFER_SIZE - len, "%c%c", (unsigned char) cur, count);
                } else {
                    fprintf(stderr, "Warning: buffer size exceeded during encoding\n");
                    exit(1);
                }
                cur = (t->addr)[i]; // change cur to the next address
                count = 1; // reset count
            }else{
                count +=1; // if duplicate char, add to count
            }
        }

        if (len < BUFFER_SIZE - 2) {
            len += snprintf((t->result) + len, BUFFER_SIZE - len, "%c%c", (unsigned char) cur, count);
        } else {
            fprintf(stderr, "Warning: buffer size exceeded during encoding\n");
            break;
        }

        // add the byte length to result_len for printing
        t->last_char = cur;
        t->last_count = count;
        t->result_len = len;
        // Queue task in order
        queue_task_at(completed_queue, t, (t->start)/(size_t)4096);
        
        // Wait for mutex and then signal that a task is finished
        // then let another task use the lock
        pthread_mutex_lock(&queue->mutex);
        tasks_remaining--;
        pthread_cond_broadcast(&queue->condition); // signal to all that it is unlocked, that way the main thread has a chance to intercept the signal and check if num_tasks is 0
        pthread_mutex_unlock(&queue->mutex);
    }
    return NULL;
}

void create_threads(int num){
    threads = malloc(sizeof(pthread_t) * num);
    for(int i = 0; i < num; i++ ){
        pthread_create(&threads[i], NULL, worker_thread, (void *)task_queue);
    }
}

void reset_queue(TaskQueue *queue){
    Task *t;
    while( queue->count != 0 && (t = dequeue_task(queue)) != NULL){
        free(t);
    }

    pthread_mutex_lock(&queue->mutex);
    queue->count = queue->front = queue->rear = 0;
    pthread_cond_broadcast(&queue->condition);
    pthread_mutex_unlock(&queue->mutex);
}

void exit_threads(void){
    pthread_mutex_lock(&task_queue->mutex); // lock to change shared memory
    stop_running = 1; // set stop_running to 1 (stop signal)
    pthread_cond_broadcast(&task_queue->condition); // signal to ALL waiting on condition
    pthread_mutex_unlock(&task_queue->mutex); // unblock the mutex
}


void join_threads(void){
    for(int i = 0; i < num_thread; i++){
        pthread_join(threads[i], NULL);
    }
    free(threads);
}

int main(int argc, char *argv[]) {
    int opt;


    while((opt = getopt(argc, argv, "j:")) != -1){
        switch(opt){
            case 'j':
                num_thread = atoi(optarg);
                if(num_thread <= 0){
                    fprintf(stderr, "Error: number of threads must be greater than 0\n");
                }
                break;
        }
    }

    // if no arguments are specified, error
    if(optind >= argc){
        fprintf(stderr, "Error: no files specified\n");
        return 1;
    }
    

    task_queue = create_task_queue(400);
    completed_queue = create_task_queue(400);
    create_threads(num_thread);
    // save an empty task for writing out
    // this is the last task of the last file (null if this is the first file)
    Task *prev_task = NULL;
    int was_stitched = 0;


    for(int file_index = optind; file_index < argc; file_index++){
        // CREATE AN EMPTY TASK QUEUE TO STORE TASKS
        // task_queue = create_task_queue(400);
        // completed_queue = create_task_queue(400);
        // open files and create new threads
        // create_threads(num_thread);

        int fd = open(argv[file_index], O_RDONLY);
        if(fd == -1){
            fprintf(stderr, "Error: unable to open file\n");
            return 1;
        }

        struct stat sb;
        if(fstat(fd, &sb) == -1){
            fprintf(stderr, "Error: unable to access file\n");
            return 1;
        }

        // Map file into memory of size sb.st_size (the full file)
        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED){
            fprintf(stderr, "Error: unable to read file\n");
            close(fd);
            return 1;
        }

        // Create 4kb task structs
        for(int i = 0; i < sb.st_size; i+=CHUNK_SIZE){
            Task *t = malloc(sizeof(Task));
            t->start = i;
            // End of the chunk points to either 4kb from start(i)
            //     or the end of file if the end is less than 4kb
            t->end = (i+CHUNK_SIZE > sb.st_size) ? sb.st_size : i+CHUNK_SIZE;
            // Passing the pointer of the start of the address to new Task
            t->addr = addr;
            queue_task(task_queue, t);
        }




        // wait for tasks remaining to be 0
        // must use mutex and cond because this is reading the shared tasks_remaining variable
        // the last thread will send a signal whenever it finishes a task
        // when this recieves the cond signal, it will check if tasks remaining is 0, if so, begin writing
        pthread_mutex_lock(&task_queue->mutex);
        while(tasks_remaining > 0){
            pthread_cond_wait(&task_queue->condition, &task_queue->mutex);
        }
        pthread_mutex_unlock(&task_queue->mutex);

        for(int i = 0; i < num_queued_tasks; i++){
            Task *cur_task = dequeue_task(completed_queue);

            if(prev_task != NULL){
                // If the first and last chars are the same, stitch the count
                if(prev_task->last_char == cur_task->result[0]){

                    char combined_count[4];

                    // get the count of the current task
                    unsigned int cur_count = cur_task->result[1];
                    // combine the last (prev) task's count with the next (cur) task count
                    snprintf(combined_count, sizeof(combined_count), "%c", (prev_task->last_count + cur_count));

                    // print the previous result minus the last count number
                    write(STDOUT_FILENO, prev_task->result + was_stitched, prev_task->result_len-(1+was_stitched));
                    // write the combined count of the last char
                    write(STDOUT_FILENO, combined_count, strlen(combined_count));
                    // remove the stitched encoding of the next (cur) chunk
                    // if this cur_task was stitched, skip over the first 2 encoded chars
                    was_stitched = 2;
                }
                // If chars don't match, just print last section
                else{
                    write(STDOUT_FILENO, prev_task->result + was_stitched, prev_task->result_len - was_stitched);
                    was_stitched = 0;
                }
            }

            // move to next task
            prev_task = cur_task;
        }

        if(munmap(addr, sb.st_size) == -1){
            fprintf(stderr, "Error: unable to unmap file");
            return 1;
        }
        // reset number of tasks total
        num_queued_tasks = 0;
        tasks_remaining = 0;
        // reset the task queue values
        reset_queue(task_queue);
        reset_queue(completed_queue);
    }

    exit_threads();
    join_threads();
    // Print the last task’s result after the last encoding to ensure the last segment is written

    if (prev_task != NULL) {
        write(STDOUT_FILENO, prev_task->result + was_stitched, prev_task->result_len - was_stitched);
    }
    free(prev_task);
    return 0;
}
