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
#include<unistd.h>

#define MAX_MSG_LEN 16
#define ERROR -1
#define TRUE 1
#define FALSE 0
#define SUCCESS 0
#define WINDOW_SIZE 4 
#define PART_SENT  1
#define FILE_DONE  0
#define SEND_ERROR -1

#define BUFFER_SIZE 32

int biterror_inject = FALSE;

typedef uint16_t hsize_t;
typedef uint16_t hcsum_t;
typedef uint16_t hseq_t;
typedef uint8_t  htype_t;

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
	hseq_t  pkt_seq;
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

typedef struct queue_node {
	pkt data;
	struct queue_node *next;
} queue_node;

typedef struct {
	struct queue_node *start;
	struct queue_node *finish;
} queue;

void init_queue(queue *q) {
	q->start = NULL;
  q->finish = NULL;
}

int is_empty(queue *q) {
	return q->start == NULL;
}

void enqueue(queue *q, pkt value) {
	queue_node *new_node = (queue_node *)malloc(sizeof(queue_node));
	if (!new_node) return;
	new_node->data = value;
	new_node->next = NULL;
	if (q->finish) q->finish->next = new_node;
	q->finish = new_node;
	if (q->start == NULL) q->start = new_node;
}

pkt dequeue(queue *q) {
	pkt value = q->start->data;
	queue_node *temp = q->start;
	q->start = q->start->next;
	if (q->start == NULL) q->finish = NULL;
	free(temp);
	return value;
}

unsigned short checksum(unsigned short *buf, int nbytes){
	register long sum;
	sum = 0;
	while (nbytes > 1) {
		sum += *(buf++);
		nbytes -= 2;
	}
	if (nbytes == 1)
		sum += *(unsigned short *) buf;
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (unsigned short) ~sum;
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

int make_pkt(pkt *p, htype_t type, hseq_t seqnum, void *msg, int msg_len) {
	if (msg_len > MAX_MSG_LEN) {
		printf("make_pkt: tamanho da msg (%d) maior que limite (%d).\n",
		msg_len, MAX_MSG_LEN);
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
	if (p->h.pkt_type != PKT_ACK || p->h.pkt_seq != seqnum)
		return FALSE;
	return TRUE;
}


int has_dataseqnum(pkt *p, hseq_t seqnum) {
	if (p->h.pkt_seq != seqnum || p->h.pkt_type != PKT_DATA)
		return FALSE;
	return TRUE;
}

int rdt_send(int sockfd, FILE *file, struct sockaddr_in *dst) {
    // static variables to hold state between calls
    static int state_initialized = 0;
    static int packets_in_flight = 0;
    static int file_end = 0;   // becomes 1 when we hit EOF
    static hseq_t base = 0;    // sequence number of the base packet
    static pkt sent_packets[WINDOW_SIZE];    // saved packets for possible retransmission
    static int sent_sizes[WINDOW_SIZE];        // actual data lengths
    static struct timeval sent_time[WINDOW_SIZE]; // send time for each packet

    // On first call, initialize our state
    if (!state_initialized) {
        base = _snd_seqnum;        // assume _snd_seqnum is a global variable
        packets_in_flight = 0;
        file_end = 0;
        state_initialized = 1;
    }

    /* --- Fill the window with new packets (if there is space and file data remains) --- */
    while (packets_in_flight < WINDOW_SIZE && !file_end) {
        char data_buffer[MAX_MSG_LEN];
        int bytes_read = fread(data_buffer, 1, MAX_MSG_LEN, file);
        if (bytes_read <= 0) { 
            // Reached end-of-file
            file_end = 1;
            break;
        }

        pkt p;
        memset(&p, 0, sizeof(pkt));
        if (make_pkt(&p, PKT_DATA, _snd_seqnum, data_buffer, bytes_read) < 0)
            return SEND_ERROR;

        int index = _snd_seqnum % WINDOW_SIZE;
        sent_packets[index] = p;
        sent_sizes[index] = bytes_read;
        gettimeofday(&sent_time[index], NULL);

        printf("Enviando pacote %d (%d bytes): [", _snd_seqnum, bytes_read);
        for (int i = 0; i < bytes_read; i++) {
            printf("%c", data_buffer[i]);
        }
        printf("]\n");

        if (sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0) {
            perror("sendto");
            return SEND_ERROR;
        }
        _snd_seqnum++;
        packets_in_flight++;
    }

    /* --- Wait for an ACK (with timeout) --- */
    double TimeoutInterval = EstRTT + 4 * DevRTT;
    struct timeval timeout;
    timeout.tv_sec = (int)TimeoutInterval;
    timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        return SEND_ERROR;
    }

    struct sockaddr_in dst_ack;
    int addrlen = sizeof(dst_ack);
    pkt ack;
    int nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, (socklen_t *)&addrlen);

    if (nr > 0) {
        // ACK received
        if (!iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
            printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);
            int ack_index = ack.h.pkt_seq % WINDOW_SIZE;
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            double SampleRTT = (current_time.tv_sec - sent_time[ack_index].tv_sec) +
                                 (current_time.tv_usec - sent_time[ack_index].tv_usec) / 1e6;
            EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
            DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);
            TimeoutInterval = EstRTT + 4 * DevRTT;

            if (ack.h.pkt_seq >= base) {
                int num_acked = (ack.h.pkt_seq - base) + 1;
                packets_in_flight -= num_acked;
                base = ack.h.pkt_seq + 1;
            }
        }
    } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
        // Timeout: retransmit all unacknowledged packets
        printf("Timeout. Retransmitindo pacotes...\n");
        for (hseq_t i = base; i < _snd_seqnum; i++) {
            int index = i % WINDOW_SIZE;
            if (sendto(sockfd, &sent_packets[index], sent_packets[index].h.pkt_size, 0,
                       (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0) {
                perror("sendto");
                return SEND_ERROR;
            }
            printf("Retransmitido pacote %d\n", i);
            gettimeofday(&sent_time[index], NULL);
        }
    }

    /* --- Check if everything has been sent and acknowledged --- */
    if (file_end && packets_in_flight == 0) {
        // All data has been sent, so send a termination packet.
        pkt term_pkt;
        memset(&term_pkt, 0, sizeof(pkt));
        if (make_pkt(&term_pkt, PKT_DATA, _snd_seqnum, NULL, 0) < 0)
            return SEND_ERROR;
        printf("Enviando pacote de término (seq %d).\n", _snd_seqnum);
        for (int i = 0; i < 5; i++) {
            if (sendto(sockfd, &term_pkt, term_pkt.h.pkt_size, 0,
                       (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0) {
                perror("sendto");
                return SEND_ERROR;
            }
            usleep(100000); // 100 ms delay between termination packets
        }
        // Reset state for future transfers (if needed)
        state_initialized = 0;
        return FILE_DONE;
    }

    // Otherwise, indicate that more data remains to be sent.
    return PART_SENT;
}







int rdt_recv(int sockfd, struct sockaddr_in *src) {
    pkt p, ack;
    int nr, ns;
    socklen_t addrlen;
    FILE *fp;
    
    // Abrir o arquivo de saída.
    fp = fopen("output.txt", "w");
    if (fp == NULL) {
        perror("Erro ao abrir output.txt");
        return ERROR;
    }

    printf("Recebendo arquivo e salvando em output.txt...\n");

    while (1) {
        addrlen = sizeof(struct sockaddr_in);
        nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, &addrlen);
        if (nr < 0) {
            perror("recvfrom()");
            fclose(fp);
            return ERROR;
        }

        // Calcular o tamanho da mensagem real (payload).
        int msg_size = p.h.pkt_size - sizeof(hdr);

        // Se a mensagem for vazia, trata-se do pacote de término.
        if (msg_size == 0) {
            printf("Fim da transmissão recebido (seq %d). Encerrando.\n", p.h.pkt_seq);
            // Opcional: enviar um ACK final para o pacote de término.
            if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) {
                fclose(fp);
                return ERROR;
            }
            ns = sendto(sockfd, &ack, ack.h.pkt_size, 0,
                        (struct sockaddr*)src, sizeof(struct sockaddr_in));
            if (ns < 0)
                perror("rdt_recv: sendto(PKT_ACK) term");
            break;
        }

        // Se o pacote já foi processado, reenviar o ACK correspondente.
        if (p.h.pkt_seq < _rcv_seqnum) {
            printf("Pacote duplicado %d recebido. Enviando ACK novamente.\n", p.h.pkt_seq);
            if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) {
                fclose(fp);
                return ERROR;
            }
            ns = sendto(sockfd, &ack, ack.h.pkt_size, 0,
                        (struct sockaddr*)src, sizeof(struct sockaddr_in));
            if (ns < 0) {
                perror("rdt_recv: sendto(PKT_ACK) duplicate");
                fclose(fp);
                return ERROR;
            }
            continue;
        }

        // Se o pacote estiver corrompido, reenviar o último ACK.
        if (iscorrupted(&p)) {
            printf("Pacote corrompido (seq %d). Enviando último ACK (%d).\n", p.h.pkt_seq, _rcv_seqnum - 1);
            if (make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0) < 0) {
                fclose(fp);
                return ERROR;
            }
            ns = sendto(sockfd, &ack, ack.h.pkt_size, 0,
                        (struct sockaddr*)src, sizeof(struct sockaddr_in));
            if (ns < 0) {
                perror("rdt_recv: sendto(PKT_ACK) corrupted");
                fclose(fp);
                return ERROR;
            }
            continue;
        }

        // Pacote válido: processar e salvar os dados.
        printf("Pacote recebido %d (%d bytes): ", p.h.pkt_seq, msg_size);
        for (int i = 0; i < msg_size; i++) {
            printf("%c", p.msg[i]);
        }
        printf("\n");
        fwrite(p.msg, 1, msg_size, fp);
        fflush(fp);

        // Enviar ACK para o pacote recebido.
        if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) {
            fclose(fp);
            return ERROR;
        }
        ns = sendto(sockfd, &ack, ack.h.pkt_size, 0,
                    (struct sockaddr*)src, sizeof(struct sockaddr_in));
        if (ns < 0) {
            perror("rdt_recv: sendto(PKT_ACK) valid");
            fclose(fp);
            return ERROR;
        }
        printf("ACK enviado para o pacote %d\n", p.h.pkt_seq);
        _rcv_seqnum++;
    }

    fclose(fp);
    printf("Arquivo salvo como output.txt\n");
    return SUCCESS;
}



