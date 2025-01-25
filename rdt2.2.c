#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <math.h>

#define MAX_MSG_LEN 1000
#define ERROR -1
#define TRUE 1
#define FALSE 0
#define SUCCESS 0

int biterror_inject = FALSE;

typedef uint16_t hsize_t;
typedef uint16_t hcsum_t;
typedef uint16_t hseq_t;
typedef uint8_t htype_t;

#define PKT_ACK 0
#define PKT_DATA 1

hseq_t _snd_seqnum = 1;
hseq_t _rcv_seqnum = 1;

// Variáveis globais para RTT
double EstRTT = 1.0;  // Valor inicial do RTT estimado (em segundos)
double DevRTT = 0.5;  // Valor inicial do desvio
double alpha = 0.125; // Fator de suavização para EstRTT
double beta = 0.25;   // Fator de suavização para DevRTT

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
};
typedef struct pkt pkt;

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

int iscorrupted(pkt *pr) {
    pkt pl = *pr;
    pl.h.csum = 0;
    unsigned short csuml;
    csuml = checksum((void *)&pl, pl.h.pkt_size);
    return csuml != pr->h.csum;
}

int make_pkt(pkt *p, htype_t type, hseq_t seqnum, void *msg, int msg_len) {
    if (msg_len > MAX_MSG_LEN) {
        printf("make_pkt: tamanho da msg (%d) maior que limite (%d).\n", msg_len, MAX_MSG_LEN);
        return ERROR;
    }
    p->h.pkt_size = sizeof(hdr);
    p->h.csum = 0;
    p->h.pkt_type = type;
    p->h.pkt_seq = seqnum;
    if (msg_len > 0) {
        p->h.pkt_size += msg_len;
        memset(p->msg, 0, MAX_MSG_LEN);
        memcpy(p->msg, msg, msg_len);
    }
    p->h.csum = checksum((unsigned short *)p, p->h.pkt_size);
    return SUCCESS;
}

int has_ackseq(pkt *p, hseq_t seqnum) {
    return (p->h.pkt_type == PKT_ACK && p->h.pkt_seq == seqnum);
}

#define INITIAL_WINDOW_SIZE 1   // Tamanho inicial da janela
#define MAX_WINDOW_SIZE 64      // Tamanho máximo da janela
#define AIMD_INCREMENT 1        // Incremento aditivo
#define AIMD_DECREASE_FACTOR 0.5 // Fator multiplicativo para redução

int rdt_send(int sockfd, void *buf, int buf_len, struct sockaddr_in *dst) {
    pkt p, ack;
    struct sockaddr_in dst_ack;
    int nr, addrlen;
    struct timeval start, end;
    double SampleRTT, TimeoutInterval;
    int packets_in_flight = 0;   // Pacotes não confirmados
    hseq_t base = _snd_seqnum;   // Base da janela
    int cwnd = INITIAL_WINDOW_SIZE; // Janela de congestionamento (dinâmica)

    // Calcular Timeout Interval inicial
    TimeoutInterval = EstRTT + 4 * DevRTT;

    while (1) {
        // Enviar pacotes enquanto houver espaço na janela dinâmica
        while (packets_in_flight < cwnd && packets_in_flight < MAX_WINDOW_SIZE) {
            if (make_pkt(&p, PKT_DATA, _snd_seqnum, buf, buf_len) < 0) {
                return ERROR;
            }
            gettimeofday(&start, NULL); // Registrar o tempo de envio
            sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in));
            printf("Enviado pacote %d\n", _snd_seqnum);
            _snd_seqnum++;
            packets_in_flight++;
        }

        // Configurar timeout dinâmico
        struct timeval timeout;
        timeout.tv_sec = (int)TimeoutInterval;
        timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("rdt_send: setsockopt");
            return ERROR;
        }

        // Aguardar ACKs
        addrlen = sizeof(struct sockaddr_in);
        nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, (socklen_t *)&addrlen);

        if (nr > 0) {
            gettimeofday(&end, NULL); // Registrar o tempo de chegada do ACK
            SampleRTT = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;

            // Atualizar EstRTT e DevRTT
            EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
            DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);

            // Atualizar TimeoutInterval
            TimeoutInterval = EstRTT + 4 * DevRTT;

            printf("Timeout atualizado: %f segundos\n", TimeoutInterval);

            if (!iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK && ack.h.pkt_seq >= base) {
                printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);
                packets_in_flight -= (ack.h.pkt_seq - base + 1);
                base = ack.h.pkt_seq + 1;

                // **AIMD: Incremento aditivo**
                cwnd += AIMD_INCREMENT;
                if (cwnd > MAX_WINDOW_SIZE) cwnd = MAX_WINDOW_SIZE;
                printf("AIMD: Nova janela de congestionamento (cwnd) = %d\n", cwnd);
            }
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("Timeout. Reduzindo a janela...\n");

            // **AIMD: Redução multiplicativa**
            cwnd = (int)(cwnd * AIMD_DECREASE_FACTOR);
            if (cwnd < 1) cwnd = 1;
            printf("AIMD: Janela reduzida após timeout (cwnd) = %d\n", cwnd);

            // Retransmitir pacotes não confirmados
            for (hseq_t i = base; i < _snd_seqnum; i++) {
                if (make_pkt(&p, PKT_DATA, i, buf, buf_len) < 0) {
                    return ERROR;
                }
                sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in));
                printf("Retransmitido pacote %d\n", i);
            }
        }

        if (packets_in_flight == 0) break; // Sair do loop se todos os pacotes forem confirmados
    }

    return buf_len;
}

int has_dataseqnum(pkt *p, hseq_t seqnum) {
    return (p->h.pkt_seq == seqnum && p->h.pkt_type == PKT_DATA);
}

int rdt_recv(int sockfd, void *buf, int buf_len, struct sockaddr_in *src) {
    pkt p, ack;
    int nr, ns;
    int addrlen = sizeof(struct sockaddr_in);

    if (make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0) < 0)
        return ERROR;

rerecv:
    nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr *)src, (socklen_t *)&addrlen);
    if (nr < 0) {
        perror("recvfrom():");
        return ERROR;
    }

    if (iscorrupted(&p) || !has_dataseqnum(&p, _rcv_seqnum)) {
        printf("rdt_recv: Pacote corrompido ou fora de sequência\n");
        ns = sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr *)src, sizeof(struct sockaddr_in));
        if (ns < 0) {
            perror("rdt_recv: sendto (ACK - 1)");
            return ERROR;
        }
        goto rerecv;
    }

    int msg_size = p.h.pkt_size - sizeof(hdr);
    if (msg_size > buf_len) {
        printf("rdt_recv: Buffer insuficiente (%d < %d)\n", buf_len, msg_size);
        return ERROR;
    }

    memcpy(buf, p.msg, msg_size);

    if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0)
        return ERROR;

    ns = sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr *)src, sizeof(struct sockaddr_in));
    if (ns < 0) {
        perror("rdt_recv: sendto (ACK)");
        return ERROR;
    }

    _rcv_seqnum++;
    return msg_size;
}
