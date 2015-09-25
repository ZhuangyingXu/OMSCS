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
    size_t *datalength;
    void *writerargument;
    void *headerargument;
    char *header;
    char *data;
    size_t chunklength;
    size_t headerlength;
    void (*writerfunc)(void *, size_t, void *);
    void (*headerfunc)(void *, size_t, void *);
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

char* reallocate_and_add_buffer(char* textToExtend, char* bufferToAdd){
    char* newScheme = (char *) malloc(strlen(textToExtend) + strlen(bufferToAdd) + 1);
    strcpy(newScheme, textToExtend);
    strcat(newScheme, bufferToAdd);
    return newScheme;
}

int gfc_perform(gfcrequest_t *gfr){
    int clientSocket = 0;
    struct sockaddr_in serverSocketAddress;
    char receivedData[BUFSIZE];
    memset(receivedData, '0', sizeof(receivedData));
    size_t writeSize;
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
    
    writeSize = send(clientSocket, message, strlen(message), 0);
    
    fprintf(stdout, "Wrote: %s.\n", message);
    fflush(stdout);
    
    char* buffer;
    size_t bytesTotal = 0;
    char* header;
    char* data;
    char* scheme = "";
    char* status = "";
    char* fileLength = "";
    char* endMarker = "";
    char* space = " ";
    int dataSize = 0;
    
    int fileSize = 0;
    
    int isScheme = 1;
    int isStatus = 0;
    int isFileLength = 0;
    int isEndMarker = 0;
    int isFileData = 0;
    int numberOfSpaces = 0;
    int byteLocation = 0;
    
    while (1) {
        size_t bytesRead = recv(clientSocket, buffer, 1, 0);
        bytesTotal = bytesTotal + bytesRead;
        gfr->bytesreceived = &bytesTotal;
        
        if (bytesRead == 0) {
            fprintf(stdout, "Connection closed.\n");
            fflush(stdout);
            
            return 0;
        }
        
        if (bytesRead < 0) {
            fprintf(stdout, "Couldn't read. Breaking.\n");
            fflush(stdout);
            
            char *header = "GETFILE ERROR 0 \r\n\r\n";
            gfr->headerfunc(header, strlen(header), gfr->headerargument);
            
            return -1;
        }
        
        if (isScheme) {
            if (strcmp(buffer, space) == 0) {
                numberOfSpaces = numberOfSpaces + 1;
                isScheme = 0;
                isStatus = 1;
                byteLocation = 0;
            }
            else {
                char* newScheme = (char*)malloc(strlen(scheme)+strlen(buffer)+1);
                strcpy(newScheme, scheme);
                strcat(newScheme, buffer);
                char* scheme = newScheme;
            }
            gfr->headerfunc(buffer, 1, gfr->headerargument);
        }
        
        if (isStatus) {
            if (strcmp(buffer, space) == 0) {
                numberOfSpaces = numberOfSpaces + 1;
                isStatus = 0;
                isFileLength = 1;
            }
            else {
                status = reallocate_and_add_buffer(status, buffer);
            }
            gfr->headerfunc(buffer, 1, gfr->headerargument);
        }
        
        if (isFileLength) {
            if (strcmp(buffer, space) == 0) {
                numberOfSpaces = numberOfSpaces + 1;
                isFileLength = 0;
                isEndMarker = 1;
                fileSize = atoi(fileLength);
            }
            else {
                fileLength = reallocate_and_add_buffer(fileLength, buffer);
            }
            gfr->headerfunc(buffer, 1, gfr->headerargument);
        }
        
        if (isEndMarker) {
            if (strcmp(buffer, space) == 0) {
                numberOfSpaces = numberOfSpaces + 1;
                isEndMarker = 0;
                isFileData = 1;
            }
            else {
                endMarker = reallocate_and_add_buffer(endMarker, buffer);
            }
            gfr->headerfunc(buffer, 1, gfr->headerargument);
        }
        
        if (isFileData) {
            if (strcmp(buffer, space) == 0) {
                numberOfSpaces = numberOfSpaces + 1;
                return 1;
            }
            else {
                dataSize = dataSize + strlen(buffer);
                gfr->datalength = dataSize;
                if (dataSize >= fileSize) {
                    fprintf(stderr, "Done reading.\n");
                    fflush(stderr);
                    return 1;
                }
                gfr->writerfunc(buffer, 1, gfr->writerargument);
            }
        }
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
    return *gfr->datalength;
}

size_t gfc_get_bytesreceived(gfcrequest_t *gfr){
    return *gfr->bytesreceived;
}

void gfc_cleanup(gfcrequest_t *gfr){
    free(gfr);
}


void gfc_global_init(){
}

void gfc_global_cleanup(){
}