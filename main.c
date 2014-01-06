
/* Sample UDP server */

#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include "GPIOhenglong.h"

uint64_t get_us(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (uint64_t)tv.tv_usec + 1000000* (uint64_t)tv.tv_sec;
}

typedef struct henglongservconf_t
{
    char outdevname[256];
    uint16_t port;
    uint64_t timeout_ms;
} henglongservconf_t;

typedef struct output_thread_t
{
    char* filename;
    int frame;
    uint64_t timeout_master_us, timeout_slave_us;
    uint8_t client_selected;
} output_thread_t;



henglongservconf_t getservconfig(char* conffilename)
{
    FILE *configFile;
    char line[256];
    char parameter[16], value[256];
    henglongservconf_t conf;
    configFile = fopen(conffilename, "r");

    // defaults
    conf.timeout_ms = 200;
    strcpy(conf.outdevname, "8");
    conf.port = 32000;

    while(fgets(line, 256, configFile)){
        sscanf(line, "%16s %256s", parameter, value);
        if(0==strcmp(parameter,"OUTPUTDEV")){
            sscanf(value, "%256s", conf.outdevname);
        }
        if(0==strcmp(parameter,"PORT")){
            sscanf(value, "%" SCNu16, &conf.port);
        }
        if(0==strcmp(parameter,"TIMEOUT_MS")){
            sscanf(value, "%" SCNu64 , &conf.timeout_ms);
        }
    }
    return conf;
}


void *output_thread_fcn(void * arg)
{
    printf("pthread output started\n");

    output_thread_t* args;

    args = (output_thread_t*) arg;


    while (1)
    {
        if(get_us()>args->timeout_master_us){
            args->frame = 0xfe400f2c; // timeout
            printf("*** Master timeout!\n");
        }
        if(get_us()>args->timeout_slave_us){
            args->frame = 0xfe400f2c; // timeout
            printf("*** Slave timeout! -- Slave %d\n", args->client_selected);
        }

        sendCode(args->frame);
        printf("OUTPUT_THREAD -- FRAME: %#x\n", args->frame);


    }

    printf("Exiting output thread.\n");
    //close(fevdev);

    pthread_exit(0);
}



int main(int argc, char**argv)
{
    int sockfd, n, i;
    struct sockaddr_in servaddr,cliaddr;
    socklen_t len;
    unsigned char recvline[64];
    uint16_t frame_nbr_recv;
    uint64_t time_us_recv;
    int frame_recv;
    henglongservconf_t conf;
    uint8_t clisel = 0 , clinbr = 0, client_selected = 0;
    unsigned char servoff;
    pthread_t outthread;
    output_thread_t output_thread_args;


    if(2!=argc){
        printf("\nThis program is intented to be run on the raspberry pi in the heng long tank. \n\n USAGE: UDPserver server.config\n\n Copyright (C) 2014 Stefan Helmert <stefan.helmert@gmx.net>\n\n");
        return 0;
    }

    setup_io();

    memset(recvline, 0, 64*sizeof(unsigned char));

    output_thread_args.client_selected = 0;
    if (pthread_create(&outthread, NULL, output_thread_fcn , (void *) &output_thread_args)) printf("failed to create thread\n");


    conf = getservconfig(argv[1]);

    sockfd=socket(AF_INET,SOCK_DGRAM,0);

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(conf.port);
    bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

    for (;;)
    {
        len = sizeof(cliaddr);
        n = recvfrom(sockfd,recvline,64,0,(struct sockaddr *)&cliaddr,&len);
        sendto(sockfd,recvline,32,0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));

        frame_nbr_recv = 0;
        for(i=0;i<2;i++){
            frame_nbr_recv |= recvline[i] << i*8;
        }
        time_us_recv = 0ULL;
        for(i=0;i<8;i++){
            time_us_recv |= ((uint64_t)recvline[i+2]) << i*8;
        }
        frame_recv = 0;
        for(i=0;i<4;i++){
            frame_recv |= recvline[i+10] << i*8;
        }
        clinbr = recvline[14];
        clisel = recvline[15];
        servoff = recvline[16];
        recvline[n] = 0;

        printf("RECV FRAME from %s:%u -- FRM_NBR: %5d, CLK_ERR: %16" PRIi64 ", BYTES recv: %3d, REFL_FRM: %#x, CLINBR: %d, CLISEL: %d, SERVOFF: %d\n", inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port, frame_nbr_recv, get_us() - time_us_recv, n, frame_recv, clinbr, clisel, servoff);
        if(client_selected==clinbr){
            output_thread_args.frame = frame_recv;
            output_thread_args.timeout_slave_us = get_us() + (uint64_t)conf.timeout_ms*1000;
        }
        output_thread_args.client_selected = client_selected;
        if(0==clinbr){
            output_thread_args.timeout_master_us = get_us() + (uint64_t)conf.timeout_ms*1000;
            client_selected = clisel;
            if(servoff){
                printf("Server stoped on client request.\n");
                return 0; // Server beenden
            }
        }

    }
}

