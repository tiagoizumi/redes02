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

#define MAX_MSG_LEN 1500
#define ERROR -1
#define SUCCESS 0
#define TRUE 1
#define FALSE 0
#define MAX_SEQ_NUM 16

#define INIT_CWND 1
#define MAX_CWND 16
#define SSTHRESH 8
#define ALPHA 0.125
#define BETA 0.25

#define PKT_ACK 0
#define PKT_DATA 1

#define STATIC_WINDOW 1
#define DYNAMIC_WINDOW 2
#define DYNAMIC_TIMER 1
#define STATIC_TIMER 2

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
    unsigned int status;
};

typedef struct pkt pkt;

pkt rcv_buffer[MAX_SEQ_NUM];
pkt snd_buffer[MAX_SEQ_NUM];

// Variáveis de controle da janela
int cwnd = INIT_CWND;
int ssthresh = SSTHRESH;
double estRTT = 1.0, devRTT = 0.5, timeoutInterval = 1.0;
hseq_t snd_base = 0, next_seq = 0;
int actual_file_size = 0;

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

int iscorrupted(pkt *pr){
	pkt pl = *pr;
	pl.h.csum = 0;
	unsigned short csuml;
	csuml = checksum((void *)&pl, pl.h.pkt_size);
	if (csuml != pr->h.csum){
		return TRUE;
	}
	return FALSE;
}

int has_dataseqnum(pkt *p, hseq_t seqnum) {
  //printf("%d %d %d\n", p->h.pkt_type, p->h.pkt_seq, seqnum);
	if (p->h.pkt_seq != seqnum || p->h.pkt_type != PKT_DATA)
		return FALSE;
	return TRUE;
}

int has_ackseq(pkt *p, hseq_t seqnum) {
    //printf("%d %d %d\n", p->h.pkt_type, p->h.pkt_seq, seqnum);
    return (p->h.pkt_type == PKT_ACK && p->h.pkt_seq == seqnum);
}

int rdt_send(int sockfd, FILE *file, struct sockaddr_in *dst) {
    pkt p;
    struct sockaddr_in dst_ack;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    struct timeval start, end;
    int bytes_read, nr;
		char msg[MAX_MSG_LEN];
		memset(p.msg, 0, MAX_MSG_LEN);
		memset(msg, 0, MAX_MSG_LEN);

    while (1) {
        bytes_read = fread(msg, 1, MAX_MSG_LEN, file);
        if (bytes_read > 0) {
          make_pkt(&p, PKT_DATA, next_seq, msg, bytes_read);
          snd_buffer[next_seq] = p;
          gettimeofday(&start, NULL);
          sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, addrlen);
          next_seq++;
          memset(msg, 0, MAX_MSG_LEN);
          memset(p.msg, 0, MAX_MSG_LEN);

          gettimeofday(&start, NULL);
        }
        if (bytes_read > 0 && next_seq < MAX_SEQ_NUM - 1) continue;

        memset(snd_buffer, 0, sizeof(snd_buffer));
        next_seq = 0;
 
        struct timeval timeout;
        timeout.tv_sec = (int)timeoutInterval;
        timeout.tv_usec = (timeoutInterval - timeout.tv_sec) * 1e6;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("rdt_send: setsockopt");
            return ERROR;
        }

        pkt ack;
				nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, &addrlen);
        printf("%d\n", cwnd);

        if (nr > 0) {
            gettimeofday(&end, NULL);
            double sampleRTT = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
            estRTT = (1 - ALPHA) * estRTT + ALPHA * sampleRTT;
            devRTT = (1 - BETA) * devRTT + BETA * fabs(sampleRTT - estRTT);
            timeoutInterval = estRTT + 4 * devRTT;
            
            if (!iscorrupted(&ack) && has_ackseq(&ack, snd_base)) {
              if (ack.h.pkt_type == PKT_ACK && ack.h.pkt_seq >= snd_base) {
                  snd_base = ack.h.pkt_seq + 1;
                  if (cwnd < ssthresh) cwnd++; // Crescimento exponencial
                  else cwnd += 1 / cwnd; // Crescimento linear
              } else {
                printf("ACK inesperado recebido: %d\n", ack.h.pkt_seq);
              }
            }
        } else {
            ssthresh = cwnd / 2;
            cwnd = INIT_CWND;
            printf("Timeout. Reduzindo cwnd para %d e ssthresh para %d\n", cwnd, ssthresh);
        }
    }
    return SUCCESS;
}

int rdt_recv(int sockfd, char* filename, struct sockaddr_in *src) {
    pkt p, ack;
    socklen_t addrlen = sizeof(struct sockaddr_in);
		int ns;

		FILE* file = fopen(filename, "a");
		if(file < 0)
			return ERROR;

    int result, write_mode = 1;

    while (1) {
        int nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr *)src, &addrlen);
        if (nr < 0 || !has_dataseqnum(&p, p.h.pkt_seq)) {
		      make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
					ns = sendto(sockfd, &ack, ack.h.pkt_size, 0,
					(struct sockaddr*)src, (socklen_t)sizeof(struct sockaddr_in));

					if (ns < 0) return ERROR;
					continue;
				}
        
        result = (int)p.h.pkt_size - (int)sizeof(hdr);
        if (result < MAX_MSG_LEN) write_mode = 0;
        fwrite(p.msg, 1, result, file);

        bzero(p.msg, MAX_MSG_LEN);
        make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
        sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr *)src, addrlen);
        printf("ACK enviado para %d\n", p.h.pkt_seq);

        if (write_mode == 0) break;
    }
    fclose(file);
    return SUCCESS;
}
