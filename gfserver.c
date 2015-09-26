#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "gfserver.h"

/*
 * Modify this file to implement the interface specified in
 * gfserver.h.
 */

#define BUFSIZE 4096

struct gfserver_t {
    unsigned short port;
    int max_npending;
    void *handlerargument;
    ssize_t (*handler)(gfcontext_t *context, char *requestedPath, void *handlerargument);
    gfcontext_t *context;
    char *requestedPath;
};

struct gfcontext_t {
    int listeningSocket;
    int connectionSocket;
    char* filepath;
};

void check(char* message) {
    fprintf(stdout, "%s.\n", message);
    fflush(stdout);
    
}

ssize_t gfs_sendheader(gfcontext_t *ctx, gfstatus_t status, size_t file_len){
    
    fprintf(stdout, "Starting header send.\n");
    fflush(stdout);
    
    if (status == GF_ERROR) {
        ssize_t sendSize = write(ctx->connectionSocket, "GETFILE ERROR \r\n\r\n", strlen("GETFILE ERROR \r\n\r\n") + 1);
        fprintf(stdout, "GETFILE ERROR \r\n\r\n");
        fflush(stdout);
        gfs_abort(ctx);
        return sendSize;
    }
    
    if (status == GF_FILE_NOT_FOUND) {
        ssize_t sendSize = write(ctx->connectionSocket, "GETFILE FILE_NOT_FOUND \r\n\r\n", strlen("GETFILE FILE_NOT_FOUND \r\n\r\n") + 1);
        fprintf(stdout, "GETFILE FILE_NOT_FOUND \r\n\r\n");
        fflush(stdout);
        gfs_abort(ctx);
        return sendSize;
    }
    
    fprintf(stdout, "Assumed status OK.\n");
    fflush(stdout);
    
    char filesizestring[500];
    sprintf(filesizestring, "%zu", file_len);
    
    char *header = (char *) malloc(17 + strlen(filesizestring));
    strcpy(header, "GETFILE OK ");
    strcat(header, filesizestring);
    strcat(header, " \r\n\r\n");
    ssize_t sendSize;
    sendSize = write(ctx->connectionSocket, header, strlen(header));
    
    fprintf(stdout, "Header: %s.\n", header);
    fflush(stdout);
    
    return sendSize;
}

ssize_t gfs_send(gfcontext_t *ctx, void *data, size_t len){
    
    fprintf(stdout, "Length: %zu.\n", len);
    fflush(stdout);
    
    ssize_t writtenContents;
    writtenContents = write(ctx->connectionSocket, data, len);
    
    return writtenContents;
}

void gfs_abort(gfcontext_t *ctx){
    fprintf(stdout, "Abort Called.\n");
    fflush(stdout);
    
    close(ctx->connectionSocket);
    //close(ctx->listeningSocket);
}

gfserver_t* gfserver_create(){
    struct gfserver_t *gfs = malloc(sizeof(*gfs));
    return gfs;
}

void gfserver_set_port(gfserver_t *gfs, unsigned short port){
    gfs->port = port;
}
void gfserver_set_maxpending(gfserver_t *gfs, int max_npending){
    gfs->max_npending = max_npending;
}

void gfserver_set_handler(gfserver_t *gfs, ssize_t (*handler)(gfcontext_t *, char *, void*)){
    gfs->handler = handler;
}

void gfserver_set_handlerarg(gfserver_t *gfs, void* arg){
    gfs->handlerargument = arg;
}

void gfserver_serve(gfserver_t *gfs){
    int listeningSocket = 0;
    int connectionSocket = 0;
    int set_reuse_addr = 1;
    struct sockaddr_in clientSocketAddress;
    char *readData = "";
    ssize_t readDataSize = BUFSIZE;
    
    readData = (char*) malloc(BUFSIZE);
    
    while(1) {
        listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
        
        setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &set_reuse_addr, sizeof(set_reuse_addr));
        
        memset(&clientSocketAddress, '0', sizeof(clientSocketAddress));
        
        clientSocketAddress.sin_family = AF_INET;
        clientSocketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        clientSocketAddress.sin_port = htons(gfs->port);
        
        bind(listeningSocket, (struct sockaddr *) &clientSocketAddress, sizeof(clientSocketAddress));
        
        listen(listeningSocket, gfs->max_npending);
        
        fprintf(stderr, "listening.\n");
        fflush(stderr);
        
        connectionSocket = accept(listeningSocket, (struct sockaddr *) NULL, NULL);
        
        fprintf(stderr, "connected.\n");
        fflush(stderr);
        
        
        readDataSize = recv(connectionSocket, readData, BUFSIZE, 0);
        
        fprintf(stdout, "Received: %s.\n", readData);
        fflush(stdout);
        
        char *scheme = strtok(readData, " ");
        
        char *request = strtok(NULL, " ");
        
        char *filenamefromclient = strtok(NULL, " ");
        
        struct gfcontext_t *ctx = malloc(sizeof *ctx);
        ctx->filepath = filenamefromclient;
        ctx->listeningSocket = listeningSocket;
        ctx->connectionSocket = connectionSocket;
        
        char filename = *filenamefromclient;
        
        if (scheme == NULL || request == NULL || filename == NULL) {
            fprintf(stdout, "Here.\n");
            fflush(stdout);
            char *statusString = "FILE_NOT_FOUND";
            
            char *header = (char *) malloc(13 + strlen(statusString) + 1);
            strcpy(header, "GETFILE ");
            strcat(header, statusString);
            strcat(header, " \r\n\r\n");
            
            write(ctx->connectionSocket, header, strlen(header));
        }
        else if (strcmp(scheme, "GETFILE") != 0 || strcmp(request, "GET") != 0 || filename != '/') {
            fprintf(stdout, "Here instead.\n");
            fflush(stdout);
            char *statusString = "FILE_NOT_FOUND";
            
            char *header = (char *) malloc(13 + strlen(statusString) + 1);
            strcpy(header, "GETFILE ");
            strcat(header, statusString);
            strcat(header, " \r\n\r\n");
            
            write(ctx->connectionSocket, header, strlen(header));
        }
        else {
            fprintf(stdout, "referencing header send.\n");
            fflush(stdout);
            gfs->handler(ctx, filenamefromclient, gfs->handlerargument);
        }
        
        fprintf(stdout, "Scheme: %s. Request: %s. Filename: %s\n.", scheme, request, filenamefromclient);
    }
}