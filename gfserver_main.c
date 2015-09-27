#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>

#include "gfserver.h"
#include "content.h"
#include "steque.h"

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  gfserver_main [options]\n"                                                      \
"options:\n"                                                                  \
"  -p [listen_port]    Listen port (Default: 8888)\n"                         \
"  -t [nthreads]       Number of threads (Default: 1)\n"                      \
"  -c [content_file]   Content file mapping keys to content files\n"          \
"  -h                  Show this help message.\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port",          required_argument,      NULL,           'p'},
    {"content",       required_argument,      NULL,           'c'},
    {"nthreads",      required_argument,      NULL,           't'},
    {"help",          no_argument,            NULL,           'h'},
    {NULL,            0,                      NULL,             0}
};

extern ssize_t handler_get(gfcontext_t *ctx, char *path, void *arg);

typedef struct {
    gfcontext_t *ctx;
    char *path;
    void *arg;
} request_t;

pthread_mutex_t gSharedMemoryLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gListActivePhase = PTHREAD_COND_INITIALIZER;

steque_t *queue;

void createAndQueueRequest(gfcontext_t *ctx, char *path, void *arg) {
    request_t* request = malloc(sizeof(request_t));
    request->ctx = ctx;
    request->path = path;
    request->arg = arg;
    steque_push(queue, request);
}

request_t* retrieveRequest() {
    request_t* request = malloc(sizeof(request_t));
    request = (request_t*)  steque_pop(queue);
    return request;
}

static void _sig_handler(int signo){
    if (signo == SIGINT || signo == SIGTERM){
        exit(signo);
    }
}

void handler (gfcontext_t *ctx, char *path, void* arg) {
    fprintf(stdout, "Handler called!");
    fflush(stdout);
    
    pthread_mutex_lock(&gSharedMemoryLock);
    
    createAndQueueRequest(ctx, path, arg);
    
    pthread_mutex_unlock(&gSharedMemoryLock);
    
    pthread_cond_signal(&gListActivePhase);
}

void performerMain (void *threadArgument) {
    
    while (1) {
        pthread_mutex_lock(&gSharedMemoryLock);
        while (steque_isempty(queue)) {
            pthread_cond_wait(&gListActivePhase, &gSharedMemoryLock);
        }
        
        request_t *request = retrieveRequest();
        
        pthread_mutex_unlock(&gSharedMemoryLock);
        
        handler_get(request->ctx, request->path, request->arg);
    }
    pthread_exit(0);
}

/* Main ========================================================= */
int main(int argc, char **argv) {
    int option_char = 0;
    unsigned short port = 8888;
    char *content = "content.txt";
    gfserver_t *gfs;
    int nthreads = 0;
    queue = malloc(sizeof(steque_t));
    steque_init(queue);
    
    if (signal(SIGINT, _sig_handler) == SIG_ERR){
        fprintf(stderr,"Can't catch SIGINT...exiting.\n");
        exit(EXIT_FAILURE);
    }
    
    if (signal(SIGTERM, _sig_handler) == SIG_ERR){
        fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
        exit(EXIT_FAILURE);
    }
    
    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:t:c:h", gLongOptions, NULL)) != -1) {
        switch (option_char) {
            case 'p': // listen-port
                port = atoi(optarg);
                break;
            case 't': // nthreads
                nthreads = atoi(optarg);
                break;
            case 'c': // file-path
                content = optarg;
                break;
            case 'h': // help
                fprintf(stdout, "%s", USAGE);
                exit(0);
                break;
            default:
                fprintf(stderr, "%s", USAGE);
                exit(1);
        }
    }
    
    content_init(content);
    
    /*Initializing server*/
    gfs = gfserver_create();
    
    /*Setting options*/
    gfserver_set_port(gfs, port);
    gfserver_set_maxpending(gfs, 100);
    gfserver_set_handler(gfs, handler);
    gfserver_set_handlerarg(gfs, NULL);
    
    int i;
    
    int performerNum[nthreads];
    
    pthread_t performerThreadIDs[nthreads];
    
    for(i = 0; i < nthreads; i++) {
        performerNum[i] = i;
        pthread_create(&performerThreadIDs[i], NULL, performerMain, &performerNum[i]);
        fprintf(stdout, "Thread created!");
        fflush(stdout);
    }
    
    gfserver_serve(gfs);
    
    for(i = 0; i < nthreads; i++) {
        pthread_join(performerThreadIDs[i], NULL);
    }
    
    steque_destroy(queue);
}