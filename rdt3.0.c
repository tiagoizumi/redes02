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

#define AIMD_DECREASE_FACTOR 0.5
#define AIMD_INCREMENT 1
#define MAX_WINDOW_SIZE 16

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
	//if (p->h.pkt_seq != seqnum || p->h.pkt_type != PKT_DATA)
	if (p->h.pkt_seq != seqnum || p->h.pkt_type != PKT_DATA)
		return FALSE;
	return TRUE;
}

int has_ackseq(pkt *p, hseq_t seqnum) {
    //printf("%d %d %d\n", p->h.pkt_type, p->h.pkt_seq, seqnum);
    //return (p->h.pkt_type == PKT_ACK && p->h.pkt_seq == seqnum);
    return (p->h.pkt_type == PKT_ACK && p->h.pkt_seq == seqnum);
}

int make_pkt(pkt *p, htype_t type, hseq_t seqnum, char *msg, int msg_len) {
    if (msg_len > MAX_MSG_LEN) return ERROR;
    p->h.pkt_size = sizeof(hdr) + msg_len;
    p->h.pkt_type = type;
    p->h.pkt_seq = seqnum;
    memset(p->msg, 0, MAX_MSG_LEN);
    memcpy(p->msg, msg, msg_len);
    p->msg_len = msg_len;
    p->h.csum = checksum((unsigned short *)p, p->h.pkt_size);
    return SUCCESS;
}

int rdt_send(int sockfd, char* msg, struct sockaddr_in *dst, int buf_len, int bytes_read, int _snd_seqnum) {
    pkt p, ack;
    struct sockaddr_in dst_ack;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    struct timeval start, end;
    int ns, nr;

    if (make_pkt(&p, PKT_DATA, _snd_seqnum, msg, bytes_read) < 0) 
      return ERROR;
    
resend:
    gettimeofday(&start, NULL);
    ns = sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, addrlen);

    if (ns < 0) {
      perror("rdt_send: sendto(PKT_DATA):");
      return ERROR;
    }
    nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack,
      (socklen_t *)&addrlen);
    printf("%d %d\n", ack.h.pkt_seq, _snd_seqnum);
    if (nr < 0) {
      perror("rdt_send: recvfrom(PKT_ACK)");
      return ERROR;
    }
    if (iscorrupted(&ack) || has_ackseq(&ack, _snd_seqnum) == FALSE)
      goto resend;

    if (nr > 0) {
        gettimeofday(&end, NULL);
        double sampleRTT = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
        estRTT = (1 - ALPHA) * estRTT + ALPHA * sampleRTT;
        devRTT = (1 - BETA) * devRTT + BETA * fabs(sampleRTT - estRTT);
        timeoutInterval = estRTT + 4 * devRTT;
        
        if (!iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
            printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);
            cwnd += AIMD_INCREMENT;
            if (cwnd > MAX_WINDOW_SIZE) cwnd = MAX_WINDOW_SIZE;
        }
    } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
      cwnd = (int)(cwnd * AIMD_DECREASE_FACTOR);
      if (cwnd < 1) cwnd = 1;

      // Retransmitir pacotes não confirmados
      if (make_pkt(&p, PKT_DATA, _snd_seqnum, msg, bytes_read) < 0) return ERROR;
      sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in));
    }
    return buf_len;
}

int rdt_recv(int sockfd, FILE* file, struct sockaddr_in *src, int buf_len, int _rcv_seqnum) {
    pkt p, ack;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    int nr, ns;

    if (make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0) < 0)
      return ERROR;

rerecv:
    nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr *)src, &addrlen);

    if (nr < 0) return ERROR;

    //printf("%d %d\n", p.h.pkt_seq, _rcv_seqnum);
    if (iscorrupted(&p) || !has_dataseqnum(&p, _rcv_seqnum)) {
      ns = sendto(sockfd, &ack, ack.h.pkt_size, 0,
			(struct sockaddr*)src, (socklen_t)sizeof(struct sockaddr_in));
      if (ns < 0) return ERROR;
      goto rerecv;
    }

    int msg_size = (int)p.h.pkt_size - (int)sizeof(hdr);
    if (msg_size > buf_len) return ERROR;

    fwrite(p.msg, 1, msg_size, file);
    bzero(p.msg, MAX_MSG_LEN);

    if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0)
      return ERROR;

    printf("%d %d\n", ack.h.pkt_seq, _rcv_seqnum);
    ns = sendto(sockfd, &ack, ack.h.pkt_size, 0,
    (struct sockaddr*)src, (socklen_t)sizeof(struct sockaddr_in));

    printf("ACK Enviado\n");
    if (msg_size > buf_len) return ERROR;
    return p.h.pkt_size - sizeof(hdr);
}
