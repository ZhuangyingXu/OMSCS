#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>

#include "workload.h"
#include "gfclient.h"
#include "steque.h"

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  webclient [options]\n"                                                     \
"options:\n"                                                                  \
"  -s [server_addr]    Server address (Default: 0.0.0.0)\n"                   \
"  -p [server_port]    Server port (Default: 8888)\n"                         \
"  -w [workload_path]  Path to workload file (Default: workload.txt)\n"       \
"  -t [nthreads]       Number of threads (Default 1)\n"                       \
"  -n [num_requests]   Requests download per thread (Default: 1)\n"           \
"  -h                  Show this help message\n"                              \

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server",        required_argument,      NULL,           's'},
    {"port",          required_argument,      NULL,           'p'},
    {"workload-path", required_argument,      NULL,           'w'},
    {"nthreads",      required_argument,      NULL,           't'},
    {"nrequests",     required_argument,      NULL,           'n'},
    {"help",          no_argument,            NULL,           'h'},
    {NULL,            0,                      NULL,             0}
};

static void Usage() {
    fprintf(stdout, "%s", USAGE);
}

static void localPath(char *req_path, char *local_path){
    static int counter = 0;
    
    sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

static FILE* openFile(char *path){
    char *cur, *prev;
    FILE *ans;
    
    /* Make the directory if it isn't there */
    prev = path;
    while(NULL != (cur = strchr(prev+1, '/'))){
        *cur = '\0';
        
        if (0 > mkdir(&path[0], S_IRWXU)){
            if (errno != EEXIST){
                perror("Unable to create directory");
                exit(EXIT_FAILURE);
            }
        }
        
        *cur = '/';
        prev = cur;
    }
    
    if( NULL == (ans = fopen(&path[0], "w"))){
        perror("Unable to open file");
        exit(EXIT_FAILURE);
    }
    
    return ans;
}

/* Callbacks ========================================================= */
static void writecb(void* data, size_t data_len, void *arg){
    FILE *file = (FILE*) arg;
    
    fwrite(data, 1, data_len, file);
}

/* Multithreaded ========================================================= */

typedef struct {
    char *req_path;
    char *local_path;
    char *server;
    unsigned short port;
    int returncode;
} request_t;

pthread_mutex_t gSharedMemoryLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gListActivePhase = PTHREAD_COND_INITIALIZER;

steque_t *queue;

void createAndQueueRequest(char* req_path, char* local_path, char* server, unsigned short port, int returncode) {
    request_t* request = malloc(sizeof(request_t));
    request->req_path = req_path;
    request->local_path = local_path;
    request->server = server;
    request->port = port;
    request->returncode = returncode;
    steque_push(queue, request);
}

request_t* retrieveRequest() {
    request_t* request = malloc(sizeof(request_t));
    request = (request_t*)  steque_pop(queue);
    return request;
}

void performClientStuffGivenPath(char* req_path, char* local_path, char* server, unsigned short port, int returncode) {
    gfcrequest_t *gfr;
    FILE *file;
    
    if(strlen(req_path) > 256){
        fprintf(stderr, "Request path exceeded maximum of 256 characters\n.");
        exit(EXIT_FAILURE);
    }
    
    localPath(req_path, local_path);
    
    file = openFile(local_path);
    
    gfr = gfc_create();
    gfc_set_server(gfr, server);
    gfc_set_path(gfr, req_path);
    gfc_set_port(gfr, port);
    gfc_set_writefunc(gfr, writecb);
    gfc_set_writearg(gfr, file);
    
    fprintf(stdout, "Requesting %s%s\n", server, req_path);
    
    if ( 0 > (returncode = gfc_perform(gfr))){
        fprintf(stdout, "gfc_perform returned an error %d\n", returncode);
        fclose(file);
        if ( 0 > unlink(local_path))
            fprintf(stderr, "unlink failed on %s\n", local_path);
    }
    fclose(file);
    
    if ( gfc_get_status(gfr) != GF_OK){
        if ( 0 > unlink(local_path))
            fprintf(stderr, "unlink failed on %s\n", local_path);
    }
    
    fprintf(stdout, "Status: %s\n", gfc_strstatus(gfc_get_status(gfr)));
    fprintf(stdout, "Received %zu of %zu bytes\n", gfc_get_bytesreceived(gfr), gfc_get_filelen(gfr));
}

void handler (char* req_path, char* local_path, char* server, unsigned short port, int returncode) {
    pthread_mutex_lock(&gSharedMemoryLock);
    
    createAndQueueRequest(req_path, local_path, server, port, returncode);
    
    pthread_mutex_unlock(&gSharedMemoryLock);
    
    pthread_cond_signal(&gListActivePhase);
}

void performerMain (void *threadArgument) {

    pthread_mutex_lock(&gSharedMemoryLock);
    while (steque_isempty(queue)) {
        pthread_cond_wait(&gListActivePhase, &gSharedMemoryLock);
    }
    
    request_t *request = retrieveRequest();
    
    pthread_mutex_unlock(&gSharedMemoryLock);
    
    performClientStuffGivenPath(request->req_path, request->local_path, request->server, request->port, request->returncode);

    pthread_exit(0);
}

/* Main ========================================================= */
int main(int argc, char **argv) {
    /* COMMAND LINE OPTIONS ============================================= */
    char *server = "localhost";
    unsigned short port = 8888;
    char *workload_path = "workload.txt";
    
    int i;
    int option_char = 0;
    int nrequests = 1;
    int nthreads = 1;
    int returncode;
    char *req_path;
    char local_path[512];
    queue = malloc(sizeof(steque_t));
    steque_init(queue);
    
    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:w:n:t:h", gLongOptions, NULL)) != -1) {
        switch (option_char) {
            case 's': // server
                server = optarg;
                break;
            case 'p': // port
                port = atoi(optarg);
                break;
            case 'w': // workload-path
                workload_path = optarg;
                break;
            case 'n': // nrequests
                nrequests = atoi(optarg);
                break;
            case 't': // nthreads
                nthreads = atoi(optarg);
                break;
            case 'h': // help
                Usage();
                exit(0);
                break;
            default:
                Usage();
                exit(1);
        }
    }
    
    if( EXIT_SUCCESS != workload_init(workload_path)){
        fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
        exit(EXIT_FAILURE);
    }
    
    gfc_global_init();
    
    int threadsToCreate = nrequests * nthreads;
    
    int performerNum[threadsToCreate];
    
    pthread_t performerThreadIDs[threadsToCreate];
    
    for(i = 0; i < (threadsToCreate); i++) {
        fprintf(stdout, "%d.\n", i);
        fflush(stdout);
        performerNum[i] = i;
        pthread_create(&performerThreadIDs[i], NULL, performerMain, &performerNum[i]);
    }
    
    /*Making the requests...*/
    for(i = 0; i < threadsToCreate; i++){
        req_path = workload_get_path();
        handler(req_path, local_path, server, port, returncode);
    }
    
    for(i = 0; i < threadsToCreate; i++) {
        pthread_join(performerThreadIDs[i], NULL);
    }

    steque_destroy(queue);
    
    gfc_global_cleanup();
    
    return 0;
}