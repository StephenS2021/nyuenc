#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>


#define BUFFER_SIZE 256
#define BYTES_PER_LINE 16
#define DEFAULT_THREADS 1
#define CHUNK_SIZE 4096


// Create a struct for each task
typedef struct {
    size_t start; // Start byte of the chunk
    size_t end; // End byte of the chunk
    char **addr; // address of the chunk
    char **result; // encoded result of the chunk
} Task;

typedef struct{
    Task **tasks; // Array of pointers to tasks
    int front, rear, count; // Initialize variables for accessing queue data
    int capacity; // Initial capacity of queue which will be increased if necessary
    pthread_cond_t condition; // Condition variable for the queue
    pthread_mutex_t mutex; // Mutex for locking access
} TaskQueue;


int num_thread = DEFAULT_THREADS; // Global num_thread variable
pthread_t *threads; // Array of working threads in pool
TaskQueue *task_queue;
int tasks_remaining = 0;

void encode(struct stat sb, char *addr, char *buffer, char *cur, int *len, unsigned int *count){
    for(size_t i = 0; i < (size_t)sb.st_size; i++){
        if(i==0){
            // If the beginning of the file, set first char and add offset to buffer in hexadexcimal
            *cur = addr[i];
            *count += 1;
        }else if(*cur != addr[i]){
            *len = snprintf(buffer, sizeof(buffer)-*len, "%c%c", (unsigned char) *cur, *count);
            write(STDOUT_FILENO, buffer, *len);
            *cur = addr[i];
            *count = 1;
        }else{
            *count +=1;
        }
    }
}
void create_threads(int num_thread){
    threads = malloc(sizeof(pthread_t) * num_thread);
    for( int i = 0; i < num_thread; i++){
        // pthread_create(&threads[i], NULL, WORKER_THREAD_FUNCTION, NULL)
    }
}
void resize_task_queue(TaskQueue *queue){
    queue->capacity = 2*queue->capacity;
    // Increase task size
    queue->tasks = realloc(queue->tasks, sizeof(Task *) * queue->capacity);
    if(!queue->tasks){
        fprintf(stderr, "Error: resizing of queue failed");
        exit(1);
    }
}

// Adds task to the task queue
void queue_task(TaskQueue *queue, Task *t){
    // Lock mutex
    pthread_mutex_lock(&queue->mutex);

    if(queue->count == queue->capacity){
        resize_task_queue(queue);
    }
    // add task to end of queue
    queue->tasks[queue->rear] = t;
    // // Increase rear counter by 1 and account for overflow of capacity
    queue->rear = (queue->rear+1) % queue->capacity;
    queue->count++;
    tasks_remaining++;
    pthread_cond_signal(&queue->condition); // Signal that a new task has been queued
    pthread_mutex_unlock(&queue->mutex); // Unlock mutex
}

Task *dequeue_task(TaskQueue *queue){
    // Lock mutex
    pthread_mutex_lock(&queue->mutex);
    // Wait while count is 0
    while(queue->count == 0){
        pthread_cond_wait(&task_queue->condition, &task_queue->mutex);
    }
    Task *task = queue->tasks[queue->front];
    queue->front = (queue->front+1) % queue->capacity;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    return task;
}

// Initializes a task queue and returns it
TaskQueue* create_thread_queue(int initial_capacity){
    TaskQueue *queue = malloc(sizeof(TaskQueue));
    queue->count = queue->front = queue->rear = 0; // Initialize the vars to 0
    queue->capacity = initial_capacity;
    queue->tasks = malloc(sizeof(Task*) * initial_capacity);
    pthread_mutex_init(&queue->mutex, NULL); // Initialize the mutex
    pthread_cond_init(&queue->condition, NULL);  // Initialize the condition variable
    return queue;
}

void worker_thread(){
    printf("hi im%d", pthread_self());
    // Continue to take tasks and encode them
    while(1){
        Task *t = dequeue_task(task_queue);
        unsigned int count = 0;
        int len = 0;
        char cur = '\0';

        // DO THE ENCODING HERE

        pthread_mutex_lock(&task_queue->mutex);
        tasks_remaining--;
        pthread_cond_signal(&task_queue->condition);
        pthread_mutex_unlock(&task_queue->mutex);
    }

}

void create_threads(int num){
    
    threads = malloc(sizeof(pthread_t) * num);
    for(int i = 0; i < num; i++ ){
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

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

    if(optind >= argc){
        fprintf(stderr, "Error: no files specified\n");
    }

    // CREATE THREADS
    // CREATE AN EMPTY TASK QUEUE TO STORE TASKS
    task_queue = create_thread_queue(100);
    create_threads(num_thread);


    for(int file_index = optind; file_index < argc; file_index++){
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
        // Map file into memory
        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED){
            fprintf(stderr, "Error: unable to read file");
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
            t->addr = &addr;
            queue_task(task_queue, t);
        } 
        
        // wait for tasks remaining to be 0
        // must use mutex and cond because this is reading the shared tasks_remaining variable
        pthread_mutex_lock(&task_queue->mutex);
        while(tasks_remaining > 0){
            pthread_cond_wait(&task_queue->condition, &task_queue->mutex);
        }
        pthread_mutex_unlock(&task_queue->mutex);

        if(munmap(addr, sb.st_size) == -1){
            fprintf(stderr, "Error: unable to unmap file");
            return 1;
        }
    }
    // 1. Create an empty task queue
    // 2. Create thread pool 
    //      2.1. Worker program waits for tasks to be added
    // 3. Loop over files
    //      3.1. For each file, break into 4kb from mmap
    //      3.2. Create new task for every partition
    //      3.3. Create a new task with map address, start => i, end=> i+chunksize or sb.st_size if shorter than i+chunksize
    //      3.4. increment tasks remaining
    // 4. Queue task
    //      4.1. Lock queue mutex
    //      4.2. Add task at rear of queue and increment rear by 1 (watch out for overflow of capacity)
    //          4.2.1. Increase queue capacity if necessary (realloc)
    //      4.3. increase count in queue
    //      4.4. add one to task remaining count
    //      4.5. send cond signal
    //      4.6. send mutex unlock
    // 5. worker thread 
    //      5.1. mutex lock and wait on cond
    //      5.2. get task from front of queue, increment front of queue
    //      5.3. subtract from overall queue count
    //      5.4. get freshly removed task
    //      5.4. unlock mutex and perform encoding on task
    //      5.5. lock mutex and cond and decrement remaining tasks count
    // 6. wait in main for remaining tasks to be 0 in a mutex lock loop
    //      6.1. unmap file and wait for next file to come thru
    //       The condition variable allows the main thread to wait efficiently until tasks_remaining reaches zero. Worker threads signal the condition variable each time they complete a task and decrement tasks_remaining. This signaling wakes up the main thread, which checks if all tasks are done.
    // 7. join threads and destroy task queue

    // char cur = '\0';
    // unsigned int count = 0;
    // char buffer[BUFFER_SIZE];
    // int len = 0;
    // if(argc < 2){
    //     fprintf(stderr, "Error: incorrect input\n");
    //     return 1;
    // }
    // for(int i = 1; i < argc; i++){
    //     int fd = open(argv[i], O_RDONLY);
    //     if(fd == -1){
    //         fprintf(stderr, "Error: unable to open file\n");
    //         return 1;
    //     }

    //     // Get fd information
        // struct stat sb;
        // if(fstat(fd, &sb) == -1){
        //     fprintf(stderr, "Error: unable to access file\n");
        //     return 1;
        // }
        // // Map file into memory
        // char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        // if (addr == MAP_FAILED){
        //     fprintf(stderr, "Error: unable to read file");
        //     close(fd);
        //     return 1;
        // }
    //     // Close input file descriptor after mmaping to memory
    //     close(fd);
        
    //     encode(sb, addr, buffer, &cur, &len, &count);
        

    //     if(munmap(addr, sb.st_size) == -1){
    //         fprintf(stderr, "Error: unable to unmap file");
    //         return 1;
    //     }
    // }
    // len = snprintf(buffer, sizeof(buffer)-len, "%c%c", (unsigned char) cur, count);
    // write(STDOUT_FILENO, buffer, len);

    return 0;
}
