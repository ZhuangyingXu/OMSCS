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
    //int listeningSocket;
    //int connectionSocket;
};

struct gfcontext_t {
    int listeningSocket;
    int connectionSocket;
    char* filepath;

};

ssize_t gfs_sendheader(gfcontext_t *ctx, gfstatus_t status, size_t file_len){

    char filesizestring[500];
    sprintf(filesizestring, "%d", file_len);

    char *header = (char *) malloc(19 + strlen(status) + strlen(filesizestring));
    strcpy(header, "GETFILE ");
    strcat(header, status);
    strcat(header, " ");
    strcat(header, filesizestring);
    strcat(header, " \r\n\r\n ");
    
    ssize_t sendSize;
    sendSize = write(ctx->connectionSocket, header, strlen(header));

    return sendSize;
}

ssize_t gfs_send(gfcontext_t *ctx, void *data, size_t len){

    ssize_t writtenContents;
    writtenContents = write(ctx->connectionSocket, data, len);

    return writtenContents;
}

void gfs_abort(gfcontext_t *ctx){
    fprintf(stderr, "Abort Called.\n");
    fflush(stderr);

    close(ctx->listeningSocket);
    close(ctx->connectionSocket);
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
    ssize_t writtenDataSize = BUFSIZE;
    char *status;

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

        //fprintf(stderr, "listening.\n");
        //fflush(stderr);

        connectionSocket = accept(listeningSocket, (struct sockaddr *) NULL, NULL);
        //gfs->context->listeningSocket = listeningSocket;
        //gfs->context->connectionSocket = connectionSocket;

        fprintf(stderr, "connected.\n");
        fflush(stderr);

        readDataSize = recv(connectionSocket, readData, BUFSIZE, 0);

        char *scheme = strtok(readData, " ");

        char *request = strtok(NULL, " ");

        char *filenamefromclient = strtok(NULL, " ");

        fprintf(stdout, "Scheme: %s. Request: %s. Filename: %s\n.", scheme, request, filenamefromclient);

        char *filename = (char *) malloc(13 + strlen(filenamefromclient));
        strcpy(filename, "server_root");
        strcat(filename, filenamefromclient);

        //char *filename = filenamefromclient;

        struct gfcontext_t *ctx = malloc(sizeof *ctx);
        //ctx->filepath = filename;
        //ctx->listeningSocket = listeningSocket;
        //ctx->connectionSocket = connectionSocket;

        gfs->handler(ctx, filenamefromclient, gfs->handlerargument);
        /*
        fprintf(stdout, "File name on server: %s.\n", filename);
        fflush(stdout);

        FILE *fp;
        FILE *file;

        if (fp = fopen(filename,"r")) {
            status = "OK";
            long size;

            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            fprintf(stdout, "File size: %d.\n", size);
            fflush(stdout);

            char filesizestring[500];
            sprintf(filesizestring, "%d", size);

            fprintf(stdout, "File size in string: %s.\n", filesizestring);
            fflush(stdout);

            char *header = (char *) malloc(19 + strlen(status) + strlen(filesizestring));
            strcpy(header, "GETFILE ");
            strcat(header, status);
            strcat(header, " ");
            strcat(header, filesizestring);
            strcat(header, " \r\n\r\n ");

            fprintf(stdout, "Header string: %s.\n", header);
            fflush(stdout);

            write(connectionSocket, header, strlen(header));

            fprintf(stdout, "Wrote header. %s.\n", header);
            fflush(stdout);

            char send_buffer[size];

            fprintf(stdout, "Size of buffer: %d.\n", sizeof(send_buffer));
            fflush(stdout);

            size_t readContents;
            size_t writtenContents;

            while(!feof(fp)) {
                readContents = fread(send_buffer, 1, sizeof(send_buffer), fp);

                //fprintf(stdout, "Buffer: %s. Size: %d. Read: %d.\n", send_buffer, sizeof(send_buffer), readContents);
                //fflush(stdout);

                if (readContents == 0) {
                    //fprintf(stdout, "Done reading: %d.\n", readContents);
                    //fflush(stdout);
                    break;
                }

                if (readContents < 0) {
                    //fprintf(stdout, "Error: %d.\n", readContents);
                    //fflush(stdout);
                    break;
                }

                writtenContents = write(connectionSocket, send_buffer, sizeof(send_buffer));

                //fprintf(stdout, "Buffer: %s. Size: %d. Written: %d.\n", send_buffer, sizeof(send_buffer), writtenContents);
                //fflush(stdout);

                bzero(send_buffer, sizeof(send_buffer));

                //fprintf(stdout, "Buffer: %s. Size: %d.\n", send_buffer, sizeof(send_buffer));
                //fflush(stdout);
            }

            fclose(fp);
            close(connectionSocket);
            close(listeningSocket);

        } else {
            status = "FILE_NOT_FOUND";

            char *header = "GETFILE FILE_NOT_FOUND 0 \r\n\r\n";

            //fprintf(stdout, "Header string: %s.", header);
            //fflush(stdout);

            write(connectionSocket, header, strlen(header));

            close(connectionSocket);
            close(listeningSocket);
        }
    */
    }
}

