/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h> 
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>


#define INTERVAL 5           /* period of Data collection data 10 sec   */

#define BUFSIZE 64
#define PORT    3000
#define TIMEOUT 1000         /* I/O timeout 1 milli sec */ 
#define MAX_IP  253
#define OK      0
#define ERROR   1
#define IOERROR -2

#define SELF_IP 255
#define CRASH_MASTER_ITER_COUNT 5

enum msg_type {
    echo = 1,           // echo server
    get_info,           // get info from sensor
    average_info,       // send average info 
    set_leader          // bradcast leader 
};

typedef struct {
    uint8_t type;
    char    data[BUFSIZE];
} message;


typedef struct {
    uint32_t  temperature;
    uint32_t  illumination;
} sensor_data;

typedef struct {
    float  temperature;
    float  illumination;
} average_data;


/**
* Global variables
**/
uint8_t scan[MAX_IP];


/** @var  the leader node flag */
int is_leader = 0;


/** @var  the current node ip */
uint32_t my_ip = 0;


/** @var  the minimal ip into subnet */
uint32_t ip_start = 0;


/** @var  iterate time counter crash master  */
uint32_t uptime = 0;

//*****************************************

static int
client( unsigned long ip, message* msg, int lenght ); 



static void emulate_sensor( sensor_data* data)
{
    data->temperature   = 30 + rand() % 10;         // T limit 0 - 100C 
    data->illumination  = 50000 +rand() % 10000;    // L limit 0 - 100 000 Lx
    printf("%s T %d\t\tL %d\n", __FUNCTION__,(int) data->temperature,(int) data->illumination );
}


/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}


static uint32_t
id_to_ip(int id)
{    
    return ip_start + id * 0x01000000;
}


/**
* get  information from sensor
*/
static void
get_sensor_info()
{
    int i;

    message msg;
    msg.type = get_info;

    int sum_temperature     = 0;
    int count_temperature   = 0;

    int sum_illummination   = 0;
    int count_illummination = 0;


    for (i = 0; i < MAX_IP; ++i)
    {
        if (scan[i] == 1) {

            struct in_addr sin;
            sin.s_addr =  id_to_ip(i);
            
            int len = client( sin.s_addr, &msg, 1);

            sensor_data* pdata = (sensor_data*) &(msg.data);
            printf("requset to client %d -> %s\treceive: T %d L %d [%d bytes]\n",
                    i, inet_ntoa(sin), pdata->temperature, pdata->illumination, len );

            if (pdata->temperature) {
                sum_temperature += pdata->temperature;
                count_temperature ++;
            }
            
            if (pdata->illumination) {
                sum_illummination += pdata->illumination;
                count_illummination++;
            }

            // printf("%x %x  %x %x    %x %x  %x %x\n", 
            //     msg.data[0], msg.data[1],msg.data[2],msg.data[3],msg.data[4],msg.data[5],msg.data[6],msg.data[7]);    
        }

        if (scan[i] == SELF_IP) {
            // get local data
            sensor_data data;
            printf("**** local data:\n");
            emulate_sensor( &data);

            sum_temperature += data.temperature;
            count_temperature ++;
        
            sum_illummination += data.illumination;
            count_illummination++;
        }

    }

    float avg_temperature = sum_temperature / count_temperature;
    float avg_illumination = sum_illummination/ count_illummination;
    printf("*** average: T=%6.2f L=%6.2f \n", avg_temperature, avg_illumination);
    printf("\n");

    printf("brodcast\n");
    msg.type = average_info;
    average_data* p_average = (average_data*)&msg.data;
    p_average->temperature = avg_temperature;
    p_average->illumination = avg_illumination;

    for (i = 0; i < MAX_IP; ++i)
    {
        if (scan[i] == 1) {

            struct in_addr sin;            
            sin.s_addr =  id_to_ip(i);        
            
            int len = client( sin.s_addr, &msg, sizeof(average_data) + 1 );

            // printf("%x %x  %x %x    %x %x  %x %x\n", 
            //     msg.data[0], msg.data[1],msg.data[2],msg.data[3],msg.data[4],msg.data[5],msg.data[6],msg.data[7]);    
        }
    }



}


/**
* get local IP v.4
*/
static uint32_t
get_ip()
{
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth1", IFNAMSIZ-1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);

    return ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
}


/**
*  udp client
*
* @param unsigned long ip   - IP address will sent message
* @param message* msg       - the message
* @param int message_lenght - lenght of message
*
* @return int lenght of message or error code
*/
static int
client( unsigned long ip, message* msg, int message_lenght ) 
{

    socklen_t serverlen;
    struct sockaddr_in serveraddr;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr =  ip;
    serveraddr.sin_port = htons(PORT);

    serverlen = sizeof(serveraddr);

    // printf("send to %s  %d bytes\n", inet_ntoa((struct in_addr)serveraddr.sin_addr) , (int)(1 + strlen(msg->data)));    
    ssize_t n = sendto(sockfd, msg, message_lenght , 0, (const struct sockaddr *)&serveraddr, serverlen);
    if (n < 0) 
      error("ERROR in sendto");

    /* set timeout */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("set sockopt Error");
    }

    char buf[BUFSIZE];
    bzero(buf, BUFSIZE);
    /* print the server's reply */
 
    n = recvfrom(sockfd, buf, BUFSIZE, 0,(struct sockaddr *) &serveraddr, &serverlen);
    close(sockfd);

    // printf("type=%d %s receive %d bytes\n",
    //      msg->type,inet_ntoa((struct in_addr)serveraddr.sin_addr) ,(int)n);
    // printf("%x %x  %x %x    %x %x   %x %x\n", 
    //     (uint8_t)buf[0], (uint8_t)buf[1],(uint8_t)buf[2],(uint8_t)buf[3],(uint8_t)buf[4],
    //     (uint8_t)buf[5],(uint8_t)buf[6],(uint8_t)buf[7]);

    if (n == 0 ) {
        return 1;
    }

    if (n > 0 ) {

         if ( msg->type == 1) {
            return 1;
         }
         if ( msg->type == 2) {
            printf("copy %d bytes\n",(int) n);
            memcpy(msg->data, buf, n );
            return n;
         }
    }


    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
    }
    
    error("IO error");
    return IOERROR;
}


/**
*   scanning local network mask 255.255.255.0
*   from low IP 
*
*  @return int 1 if is leader the node
*/
static int
scan_net()
{
    const uint32_t ip_current = my_ip;
    
    if (! ip_start) {
        ip_start = (ip_current & 0x00FFFFFF) + 0x01000000;
        printf("start IP: %d\n", ip_start );
    }


    uint32_t ip = ip_start;
    uint32_t is_leader = 0, has_node = 0;

    int i;
    for (i=0; i < MAX_IP; i++) {

        struct in_addr sin;
        sin.s_addr =  ip;

        if (ip_current == ip) {
            scan[i] = SELF_IP;
            ip += 0x01000000;
            // printf( "%s\t\t%d\n", inet_ntoa( (struct in_addr)sin ), -1 ); //
            is_leader = ! has_node;
            continue;
        }

        message msg;
        msg.type = echo;

        int res = client(ip, &msg, 1);
        scan[i] = res;

        if (res == 1) has_node = 1;
    
        ip += 0x01000000;
    }


    return is_leader;
}


/**
*
*
*/
static void*
server(void* arg)
{
    int portno = PORT;  
    int sockfd; /* socket */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */

    char buf[BUFSIZE]; /* message buf */
    message msg;

    const int optval = 1; 
    int n; /* message byte size */


    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
    error("ERROR opening socket");

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
        (const void *)&optval , sizeof(int));

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    clientlen = sizeof(clientaddr);

    printf("server: %s\n", inet_ntoa(serveraddr.sin_addr));


    while (1) {

        int len;
        bzero(&msg, BUFSIZE);
        // bzero((char *) &serveraddr, sizeof(serveraddr));

        n = recvfrom(sockfd, &msg, BUFSIZE, 0,
                (struct sockaddr *) &clientaddr, &clientlen);

        if (n < 0)
          error("ERROR in server recvfrom");

        bzero(buf, BUFSIZE); 
        switch(msg.type) {
            case echo : {
                printf("client:  %s  type=1\n", inet_ntoa(clientaddr.sin_addr));
                buf[0] = '*';
                len = 1;
                break;
            }
            
            case get_info : {

                // clean uptime counter
                uptime = 0;

                sensor_data* p = (sensor_data*) &buf;
                emulate_sensor(p);
                len = sizeof(sensor_data);
                // printf("client:  %s sent [%d bytes] type=%d  T=%d L=%d\n", 
                    // inet_ntoa(clientaddr.sin_addr), len,
                    // msg.type, p->temperature, p->illumination);
                break;
            }
            
            case average_info: {
                average_data* p = (average_data*) &(msg.data);

        // printf("%x %x  %x %x    %x %x   %x %x\n", 
        //     (uint8_t)buf[0], (uint8_t)buf[1],(uint8_t)buf[2],(uint8_t)buf[3],(uint8_t)buf[4],
        //     (uint8_t)buf[5],(uint8_t)buf[6],(uint8_t)buf[7]);

                printf("client [%d]:  %s  average_data: T %6.2f\tL  %6.2f\n",
                    n, inet_ntoa(clientaddr.sin_addr), p->temperature, p->illumination );

                buf[0] = '*';
                len = 1;
                break;
            }

            case set_leader: {
                printf("====== client: leader info  %s\n", inet_ntoa(clientaddr.sin_addr));
                
                if (is_leader) {
                    is_leader = scan_net();
                    printf("the leader %s\n",  is_leader ? "YES" : "No" ); 
                }

                buf[0] = '*';
                len = 1;
                break;
            }

            default:
                printf("client:  %s  type=%d\n", inet_ntoa(clientaddr.sin_addr),
                    msg.type);
        }

 
        // printf("%x %x  %x %x    %x %x   %x %x\n", 
        //     (uint8_t)buf[0], (uint8_t)buf[1],(uint8_t)buf[2],(uint8_t)buf[3],(uint8_t)buf[4],
        //     (uint8_t)buf[5],(uint8_t)buf[6],(uint8_t)buf[7]);

        n = sendto(sockfd, buf, len, 0, 
             (struct sockaddr *) &clientaddr, clientlen);
        if (n < 0) 
          error("ERROR in sendto");
        else
            printf("server sent responce %d bytes\n", n);
    }

}


static void
search_leader()
{
    int i;

    for (i=0; i < MAX_IP; i++) {
       if (scan[i]) break;
    }

    const uint32_t ip_current = my_ip;
    const uint32_t ip = (ip_current & 0x00FFFFFF) + 0x01000000 * (i +1);
    struct in_addr sin;
    sin.s_addr =  ip;

    printf("leader is %s (%d)\n",inet_ntoa( (struct in_addr)sin ) ,i);
}


static void 
alarm_wakeup (int i)
{
    struct itimerval tout_val;

    tout_val.it_interval.tv_sec = 0;
    tout_val.it_interval.tv_usec = 0;
    tout_val.it_value.tv_sec = INTERVAL;
    tout_val.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &tout_val,0);

    signal(SIGALRM,alarm_wakeup);
    
    if (is_leader) {
        get_sensor_info();
    } else {
        uptime++;
        if ( uptime > CRASH_MASTER_ITER_COUNT) {
            is_leader = scan_net();

            if (is_leader) {
                printf("*** The leader\n");
                search_leader();
            }            
        }
    }
    // printf("%d sec up partner, Wakeup!!!\n",INTERVAL);
}

int 
main(int argc, char **argv) 
{
    srand(time(NULL));

    pthread_t thread_server;
    int port = PORT;
    if(pthread_create(&thread_server, NULL, server, &port)) {
        perror("Creating thread error:");
        return 1;
    }

    // litle delay for create thread
    usleep(10000);

    // set global variables
    my_ip = get_ip();
    printf("current IP: %d\n", my_ip);

    is_leader = scan_net();

    if (is_leader) {
        int i;
        printf("*** The leader\n");
        message msg;
        msg.type = set_leader;
        
        // brodcast info about leader
        for (i=0; i < MAX_IP; i++) {
           if (scan[i] == OK){
                struct in_addr sin;            
                sin.s_addr =  id_to_ip(i);        

                client(sin.s_addr, &msg, 1);
           } 
        }

    }

    int i;
    if (! is_leader) {
        search_leader();
    }

      // start timer
      struct itimerval tout_val;
      
      tout_val.it_interval.tv_sec = 0;
      tout_val.it_interval.tv_usec = 0;
      tout_val.it_value.tv_sec = INTERVAL;
      tout_val.it_value.tv_usec = 0;
      setitimer(ITIMER_REAL, &tout_val,0);

      signal(SIGALRM, alarm_wakeup); /* set the Alarm signal capture */
  

    if(pthread_join(thread_server, NULL)) {

        perror("Error joining thread:");
        return ERROR;
    }

    return OK;
}
