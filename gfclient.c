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

#include "gfclient.h"

#define BUFSIZE 4096

struct gfcrequest_t  {
    char *server;
    char *path;
    unsigned short port;
    gfstatus_t status;
    char *statusText;
    size_t *bytesreceived;
    size_t *filelength;
    void *writerargument;
    void *headerargument;
    void *chunk;
    void *header;
    size_t chunklength;
    size_t headerlength;
    void (*writerfunc)(void *, size_t, void *);
    void (*headerfunc)(void *, size_t, void *);
    void *fileContent;
};

gfcrequest_t *gfc_create(){
    struct gfcrequest_t *gfr = malloc(sizeof *gfr);
    return gfr;
}

void gfc_set_server(gfcrequest_t *gfr, char* server){
    gfr->server = server;
}

void gfc_set_path(gfcrequest_t *gfr, char* path){
    gfr->path = path;
}

void gfc_set_port(gfcrequest_t *gfr, unsigned short port){
    gfr->port = port;
}

void gfc_set_headerfunc(gfcrequest_t *gfr, void (*headerfunc)(void*, size_t, void *)){
    gfr->headerfunc = headerfunc;
}

void gfc_set_headerarg(gfcrequest_t *gfr, void *headerarg){
    gfr->headerargument = headerarg;
}

void gfc_set_writefunc(gfcrequest_t *gfr, void (*writefunc)(void*, size_t, void *)){
    gfr->writerfunc = writefunc;
}

void gfc_set_writearg(gfcrequest_t *gfr, void *writearg){
    gfr->writerargument = writearg;
}

int gfc_perform(gfcrequest_t *gfr){
    int clientSocket = 0;
    struct sockaddr_in serverSocketAddress;
    char receivedData[BUFSIZE];
    memset(receivedData, '0', sizeof(receivedData));
    size_t readSize;
    size_t writeSize;
    char *fullData = "";
    int set_reuse_addr = 1;
    //struct timeval timeout;
    //timeout.tv_sec = 3;
    //timeout.tv_usec = 5000;

    clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    //setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, &set_reuse_addr, sizeof(set_reuse_addr));

    struct hostent *he = gethostbyname(gfr->server);
    unsigned long server_addr_nbo = *(unsigned long *)(he->h_addr_list[0]);

    bzero(&serverSocketAddress, sizeof(serverSocketAddress));
    serverSocketAddress.sin_family = AF_INET;
    serverSocketAddress.sin_port = htons(gfr->port);
    serverSocketAddress.sin_addr.s_addr = server_addr_nbo;

    connect(clientSocket, (struct sockaddr *)&serverSocketAddress, sizeof(serverSocketAddress));

    char * message = (char *) malloc(22 + strlen(gfr->path) );
    strcpy(message, "GETFILE GET ");
    strcat(message, gfr->path);
    strcat(message, " \r\n\r\n");

    //TODO send in bytes and track

    send(clientSocket, message, strlen(message), 0);

    fprintf(stdout, "Wrote: %s.\n", message);
    fflush(stdout);

    char buffer[BUFSIZE];
    int bytesTotal = 0;

    while (1) {
        //fprintf(stderr, "Attempting read");
        //fflush(stderr);

        int bytesRead = recv(clientSocket, buffer, BUFSIZE, 0);
        bytesTotal = bytesTotal + bytesRead;

        //fprintf(stderr, "Read bytes: %d. Data: %s.\n", bytesRead, buffer);
        //fflush(stderr);

        if (bytesRead == 0) {// We're done reading from the file
            fprintf(stderr, "Done reading.\n");
            fflush(stderr);

            char *scheme = strtok(fullData, " ");

            //fprintf(stdout, "Isolated scheme: %s.\n", scheme);
            //fflush(stdout);

            char *statusText = strtok(NULL, " ");
            gfr->statusText = statusText;
            gfr->status = gfc_get_status(gfr);

            //fprintf(stdout, "Isolated status: %s.\n", statusText);
            //fflush(stdout);

            if (strncmp(statusText, "OK", strlen("OK")) == 0) {
                //fprintf(stdout, "OK'd.\n");
                //fflush(stdout);

                char *filelength = strtok(NULL, " ");
                gfr->filelength = atol(filelength);

                fprintf(stdout, "Isolated file length. File Length: %d.\n", gfr->filelength);
                fflush(stdout);

                char *endofmarker = strtok(NULL, " ");

                //fprintf(stdout, "Isolated end of marker. End of Marker: %s.\n", endofmarker);
                //fflush(stdout);

                char *fileContent = strtok(NULL, "");
                gfr->fileContent = fileContent;

                //fprintf(stdout, "Isolated file content. File Content: %s.\n", gfr->fileContent);
                //fflush(stdout);

                gfr->bytesreceived = bytesTotal;

                //fprintf(stdout, "Bytes Received: %d.\n", gfr->bytesreceived);
                //fflush(stdout);

                bytesTotal = bytesTotal - 8 - strlen(statusText) - 1 - strlen(filelength) - 1 - 5; //Account for header
                gfr->chunklength = bytesTotal;

                //fprintf(stdout, "Content Bytes: %d.\n", gfr->chunklength);
                //fflush(stdout);

                //gfr->chunk = fileContent;
                //gfr->chunklength = bytesTotal;

                gfr->writerfunc(gfr->fileContent, fileContent, gfr->writerargument);

                //fprintf(stdout, "Wrote to writer function:\nFile Content: %s.\n\n\nBytes Total: %d.\n\n\nWriter Argument: %s.\n", gfr->fileContent, gfr->filelength, gfr->writerargument);
                //fflush(stdout);

                //fprintf(stdout, "Check1\n");
                //fflush(stdout);

                char *header = (char *) malloc(strlen(scheme) + 1 + strlen(statusText) + 1 + strlen(filelength) + 9);
                strcpy(header, scheme);
                strcat(header, " ");
                strcat(header, statusText);

                strcat(header, " ");
                strcat(header, filelength);
                strcat(header, " \r\n\r\n ");

                //fprintf(stdout, "Check2\n");
                //fflush(stdout);

                gfr->header = header;

                //fprintf(stdout, "Check3\n");
                //fflush(stdout);

                gfr->headerlength = strlen(header);

                //fprintf(stdout, "Check4. %d.\n", gfr->headerlength);
                //fflush(stdout);

                if (gfr->headerargument != NULL) {
                    gfr->headerfunc(gfr->header, gfr->headerlength, gfr->headerargument);
                }

                //fprintf(stdout, "Wrote to header\n");
                //fflush(stdout);

                //fprintf(stdout, "Wrote to header function:\nHeader Content: %s.\n\n\nBytes Total: %d.\n\n\nHeader Argument: %s.\n", gfr->header, gfr->headerlength, gfr->headerargument);
                //fflush(stdout);

                return 0;
            }
            else {
                gfr->filelength = 0;
                gfr->fileContent = "";
                gfr->bytesreceived = 0;

                //gfr->chunk = "";
                //gfr->chunklength = 0;

                char *header = (char *) malloc(strlen(scheme) + 1 + strlen(statusText) + 1 + 9);
                strcpy(header, scheme);
                strcat(header, " ");
                strcat(header, statusText);
                strcat(header, " ");
                strcat(header, "\r\n\r\n");
                return 0;
            }
        }
        if (bytesRead < 0) {
            fprintf(stdout, "Couldn't read. Breaking.\n");
            fflush(stdout);

            gfr->statusText = "ERROR";
            gfr->status = gfc_get_status(gfr);
            gfr->filelength = 0;
            gfr->fileContent = "";
            gfr->bytesreceived = 0;

            //gfr->chunk = "";
            //gfr->chunklength = 0;

            char *header = "GETFILE ERROR 0 \r\n\r\n";
            return -1;
        }

        char * finalData = (char *) malloc(strlen(fullData) + sizeof(buffer));
        strcpy(finalData, fullData);
        strcat(finalData, buffer);

        fullData = finalData;
    }
}

gfstatus_t gfc_get_status(gfcrequest_t *gfr){
    gfstatus_t status;
    int result;

    if ((result = strcmp(gfr->statusText, "OK")) == 0) {
        status = GF_OK;
    }
    else if ((result = strcmp(gfr->statusText, "FILE_NOT_FOUND")) == 0) {
        status = GF_FILE_NOT_FOUND;
    }
    else if ((result = strcmp(gfr->statusText, "ERROR")) == 0) {
        status = GF_ERROR;
    }
    else {
        status = GF_INVALID;
    }

    return status;
}

char* gfc_strstatus(gfstatus_t status){
    char* strstatus;

    if (status == GF_OK) {
        strstatus = "OK";
    }
    else if (status == GF_FILE_NOT_FOUND) {
        strstatus = "FILE_NOT_FOUND";
    }
    else if (status == GF_ERROR) {
        strstatus = "ERROR";
    }
    else {
        strstatus = "INVALID";
    }

    return strstatus;
}

size_t gfc_get_filelen(gfcrequest_t *gfr){
    return gfr->filelength;
}

size_t gfc_get_bytesreceived(gfcrequest_t *gfr){
    return gfr->chunklength;
}

void gfc_cleanup(gfcrequest_t *gfr){
    free(gfr);
}


void gfc_global_init(){
}

void gfc_global_cleanup(){
}