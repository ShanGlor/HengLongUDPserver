
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
#include <fcntl.h>      // File control definitions
#include <termios.h>    // POSIX terminal control definitions
#include "GPIOhenglong.h"

uint64_t get_us(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (uint64_t)tv.tv_usec + 1000000* (uint64_t)tv.tv_sec;
}

typedef enum outtype_et
{
    GPIO = 0,
    TTY = 1
} outtype_et;

typedef struct henglongservconf_t
{
    outtype_et outtype;
    uint8_t outdev;
    char outdevfile[256];
    uint16_t port;
    uint64_t timeout_ms;
} henglongservconf_t;


typedef struct outtty_t
{
    int16_t motor_r;
    int16_t motor_l;
    int16_t servo_tilt;
    int16_t servo_pan;
} outtty_t;

typedef struct output_thread_t
{
    char* filename;
    int frame;
    outtty_t outtty;
    int outfh;
    uint64_t timeout_master_us, timeout_slave_us;
    uint8_t client_selected;
} output_thread_t;

typedef struct RCdatagram_t
{
    uint16_t frame_nbr;
    uint64_t time_us;
    int32_t frame_recv;
    uint8_t clisel;
    uint8_t clinbr;
    uint8_t client_selected;
    uint8_t servoff;
    outtty_t outtty;
} __attribute__ ((packed)) RCdatagram_t;

henglongservconf_t getservconfig(char* conffilename)
{
    FILE *configFile;
    char line[256];
    char parameter[16], value[256];
    char souttype[8];
    henglongservconf_t conf;
    configFile = fopen(conffilename, "r");

    // defaults
    conf.timeout_ms = 200;
    conf.outdev = 8;
    conf.port = 32000;

    while(fgets(line, 256, configFile)){
        sscanf(line, "%16s %256s", parameter, value);
        if(0==strcmp(parameter,"OUTPUTDEV")){
            sscanf(value, "%256s", conf.outdevfile);
        }
        if(0==strcmp(parameter,"PORT")){
            sscanf(value, "%" SCNu16, &conf.port);
        }
        if(0==strcmp(parameter,"TIMEOUT_MS")){
            sscanf(value, "%" SCNu64 , &conf.timeout_ms);
        }
        if(0==strcmp(parameter,"OUTTYPE")){
            sscanf(value, "%s", souttype);
        }
    }
    if(0==strcmp(souttype, "tty")){
        sscanf(conf.outdevfile, "%" SCNu8, &conf.outdev);
        conf.outtype = TTY;
    }else{
        conf.outtype = GPIO;
    }
    return conf;
}


void *output_thread_fcn(void * arg)
{


    printf("pthread output started\n");

    output_thread_t* args;

    args = (output_thread_t*) arg;

    //set to realtime - maybe bad
/*
    struct sched_param params;
    pthread_t this_thread;
    this_thread = pthread_self();
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(this_thread, SCHED_FIFO, &params);
*/

    while (1)
    {
        if(get_us()>args->timeout_master_us){
            args->frame = 0xfe3c0f00; // timeout
            printf("*** Master timeout!\n");
        }
        if(get_us()>args->timeout_slave_us){
            args->frame = 0xfe3c0f00; // timeout
            printf("*** Slave timeout! -- Slave %d\n", args->client_selected);
        }

        sendCode(args->frame);
        printf("OUTPUT_THREAD -- FRAME: %#x\n", args->frame);
    }

    printf("Exiting output thread.\n");

    pthread_exit(0);
}

void *tty_output_thread_fcn(void * arg)
{
    char outstr[32];

    printf("pthread tty_output started\n");

    output_thread_t* args;

    args = (output_thread_t*) arg;

    while (1)
    {
        if(get_us()>args->timeout_master_us){
            args->outtty.motor_l = 0; // timeout
            args->outtty.motor_r = 0; // timeout
            args->outtty.servo_pan = 0; // timeout
            args->outtty.servo_tilt = 0; // timeout
            printf("*** Master timeout!\n");
        }
        if(get_us()>args->timeout_slave_us){
            args->outtty.motor_l = 0; // timeout
            args->outtty.motor_r = 0; // timeout
            args->outtty.servo_pan = 0; // timeout
            args->outtty.servo_tilt = 0; // timeout
            printf("*** Slave timeout! -- Slave %d\n", args->client_selected);
        }

        usleep(30000);
        sprintf(outstr, "%5d,%5d,%5d,%5d;", args->outtty.motor_l, args->outtty.motor_r, args->outtty.servo_pan, args->outtty.servo_tilt);
        write(args->outfh,outstr,25);
        printf("TTY_OUTPUT_THREAD -- L: %6d R: %6d x: %6d y: %6d\n", args->outtty.motor_l, args->outtty.motor_r, args->outtty.servo_pan, args->outtty.servo_tilt);
    }

    printf("Exiting output thread.\n");
    close(args->outfh);

    pthread_exit(0);
}



int main(int argc, char**argv)
{
    int sockfd, n;
    struct sockaddr_in servaddr,cliaddr;
    socklen_t len;
    henglongservconf_t conf;
    uint8_t clisel = 0 , clinbr = 0, client_selected = 0;
    pthread_t outthread;
    output_thread_t output_thread_args;
    RCdatagram_t RCdata;
    struct termios ttyoptions;    // Terminal options


    if(2!=argc){
        printf("\nThis program is intented to be run on the raspberry pi in the heng long tank. \n\n USAGE: UDPserver server.config\n\n Copyright (C) 2014 Stefan Helmert <stefan.helmert@gmx.net>\n\n");
        return 0;
    }

    conf = getservconfig(argv[1]);



    //memset(recvline, 0, 64*sizeof(unsigned char));

    output_thread_args.client_selected = 0;



    if(TTY==conf.outtype){
        if(!(output_thread_args.outfh = open(conf.outdevfile, O_RDWR | O_NOCTTY))) {
            printf("failed to open %s\n", conf.outdevfile);
            return 0;
        }

        cfsetispeed(&ttyoptions, B115200);
        cfsetospeed(&ttyoptions, B115200);
        cfmakeraw(&ttyoptions);
        ttyoptions.c_cflag |= (CLOCAL | CREAD | CS8);   // Enable the receiver and set local mode
        ttyoptions.c_cflag &= ~CSTOPB;            // 1 stop bit
        ttyoptions.c_cflag &= ~CRTSCTS;           // Disable hardware flow control
        ttyoptions.c_cc[VMIN]  = 1;
        ttyoptions.c_cc[VTIME] = 2;
        tcsetattr(output_thread_args.outfh, TCSANOW, &ttyoptions);
        output_thread_args.outtty.motor_l = 0;
        output_thread_args.outtty.motor_r = 0;
        output_thread_args.outtty.servo_pan = 0;
        output_thread_args.outtty.servo_tilt = 0;

        if (pthread_create(&outthread, NULL, tty_output_thread_fcn , (void *) &output_thread_args)) printf("failed to create thread\n");
    }else{
        setGPIOnbr(conf.outdev);
        setup_io();
        if (pthread_create(&outthread, NULL, output_thread_fcn , (void *) &output_thread_args)) printf("failed to create thread\n");
    }



    sockfd=socket(AF_INET,SOCK_DGRAM,0);

    bzero(&servaddr,sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(conf.port);
    bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

    for (;;)
    {
        len = sizeof(cliaddr);
        n = recvfrom(sockfd,&RCdata,sizeof(RCdata),0,(struct sockaddr *)&cliaddr,&len);
        sendto(sockfd,&RCdata,sizeof(RCdata),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));





        printf("RECV FRAME from %s:%u -- FRM_NBR: %5d, CLK_ERR: %16" PRIi64 ", BYTES recv: %3d, REFL_FRM: %#x, CLINBR: %d, CLISEL: %d, SERVOFF: %d\n", inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port, RCdata.frame_nbr, get_us() - RCdata.time_us, n, RCdata.frame_recv, RCdata.clinbr, RCdata.clisel, RCdata.servoff);
        if(client_selected==RCdata.clinbr){
            output_thread_args.frame = RCdata.frame_recv;
            output_thread_args.outtty = RCdata.outtty;
            output_thread_args.timeout_slave_us = get_us() + (uint64_t)conf.timeout_ms*1000;
        }
        output_thread_args.client_selected = client_selected;
        if(0==clinbr){
            output_thread_args.timeout_master_us = get_us() + (uint64_t)conf.timeout_ms*1000;
            client_selected = clisel;
            if(RCdata.servoff){
                printf("Server stoped on client request.\n");
                return 0; // Server beenden
            }
        }

    }
}

