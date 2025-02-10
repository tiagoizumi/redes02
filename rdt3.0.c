#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

#define MAX_MSG_LEN 1000
#define ERROR -1
#define SUCCESS 0
#define TRUE 1
#define FALSE 0

#define INIT_CWND 1
#define MAX_CWND 16
#define SSTHRESH 8
#define ALPHA 0.125
#define BETA 0.25

#define PKT_ACK 0
#define PKT_DATA 1

typedef uint16_t hsize_t;
typedef uint16_t hcsum_t;
typedef uint16_t hseq_t;
typedef uint8_t htype_t;

struct hdr {
    hseq_t pkt_seq;
    hsize_t pkt_size;
    htype_t pkt_type;
    hcsum_t csum;
};

typedef struct hdr hdr;

struct pkt {
    hdr h;
    unsigned char msg[MAX_MSG_LEN];
    unsigned int msg_len;
};

typedef struct pkt pkt;

// Variáveis de controle da janela
int cwnd = INIT_CWND;
int ssthresh = SSTHRESH;
double estRTT = 1.0, devRTT = 0.5, timeoutInterval = 1.0;
hseq_t snd_base = 1, next_seq = 1;
int actual_file_size = 0;

int get_file_size(FILE *file) {
  fseek(file, 0, SEEK_END);
  int size = ftell(file);
  rewind(file);
  return size;
}

unsigned short checksum(unsigned short *buf, int nbytes) {
    register long sum = 0;
    while (nbytes > 1) {
        sum += *(buf++);
        nbytes -= 2;
    }
    if (nbytes == 1)
        sum += *(unsigned short *)buf;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)~sum;
}

int make_pkt(pkt *p, htype_t type, hseq_t seqnum, char *msg, int msg_len) {
    if (msg_len > MAX_MSG_LEN) return ERROR;
    p->h.pkt_size = sizeof(hdr) + msg_len;
    p->h.pkt_type = type;
    p->h.pkt_seq = seqnum;
    memset(p->msg, 0, MAX_MSG_LEN);
    memcpy(p->msg, msg, msg_len);
    p->msg_len = msg_len;
    p->h.csum = 0;
    p->h.csum = checksum((unsigned short *)p, p->h.pkt_size);
    return SUCCESS;
}

int rdt_send(int sockfd, FILE *file, struct sockaddr_in *dst) {
    pkt p;
    struct sockaddr_in dst_ack;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    struct timeval start, end;
    int bytes_read;
		char msg[MAX_MSG_LEN];
		memset(p.msg, 0, MAX_MSG_LEN);
		memset(msg, 0, MAX_MSG_LEN);

    while ((bytes_read = fread(msg, 1, MAX_MSG_LEN, file)) > 0) {
        make_pkt(&p, PKT_DATA, next_seq, msg, bytes_read);

        gettimeofday(&start, NULL);
        sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, addrlen);
        next_seq++;

		    memset(msg, 0, MAX_MSG_LEN);
		    memset(p.msg, 0, MAX_MSG_LEN);
 
        pkt ack;
        if (recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, &addrlen) > 0) {
            gettimeofday(&end, NULL);
            double sampleRTT = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
            estRTT = (1 - ALPHA) * estRTT + ALPHA * sampleRTT;
            devRTT = (1 - BETA) * devRTT + BETA * fabs(sampleRTT - estRTT);
            timeoutInterval = estRTT + 4 * devRTT;
            
            if (ack.h.pkt_type == PKT_ACK && ack.h.pkt_seq >= snd_base) {
                snd_base = ack.h.pkt_seq + 1;
                if (cwnd < ssthresh) {
                    cwnd++; // Crescimento exponencial
                } else {
                    cwnd += 1 / cwnd; // Crescimento linear
                }
            } else {
							printf("ACK inesperado recebido: %d\n", ack.h.pkt_seq);
						}
        } else {
            ssthresh = cwnd / 2;
            cwnd = INIT_CWND;
            printf("Timeout. Reduzindo cwnd para %d e ssthresh para %d\n", cwnd, ssthresh);
        }
    }
    return SUCCESS;
}

int rdt_recv(int sockfd, char* namefile, struct sockaddr_in *src) {
    pkt p, ack;
    socklen_t addrlen = sizeof(struct sockaddr_in);

		FILE* file = fopen(namefile, "a");
		if(file < 0)
			return ERROR;

    int result, write_mode = 1;
    while (1) {
        int nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr *)src, &addrlen);
        if (nr < 0 || p.h.pkt_type != PKT_DATA) break;
        
        result = (int)p.h.pkt_size - (int)sizeof(hdr);
        if (result < MAX_MSG_LEN) write_mode = 0;
        fwrite(p.msg, 1, result, file);

        bzero(p.msg, MAX_MSG_LEN);
        make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
        sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr *)src, addrlen);
        printf("ACK enviado para %d\n", p.h.pkt_seq);

        if (write_mode == 0) break;
    }

    return SUCCESS;
}

