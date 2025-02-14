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

#define MAX_MSG_LEN 16
#define ERROR -1
#define TRUE 1
#define FALSE 0
#define SUCCESS 0
#define WINDOW_SIZE 4 
#define PART_SENT  1
#define FILE_DONE  0
#define SEND_ERROR -1
#define MIN_DELAY 10000    // 10ms
#define MAX_DELAY 900000  // 900ms

#define BUFFER_SIZE 32

#define MIN 100000
#define MAX 9000000

int biterror_inject = FALSE;

typedef uint16_t hsize_t;
typedef uint16_t hcsum_t;
typedef uint16_t hseq_t;
typedef uint8_t  htype_t;

#define PKT_ACK 0
#define PKT_DATA 1

hseq_t _snd_seqnum = 1;
hseq_t _rcv_seqnum = 1;

double EstRTT = 0.1;  
double DevRTT = 0.05;  
double alpha = 0.125; 
double beta = 0.25;   

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
		sum += *(unsigned char *) buf;
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

int rdt_send(int sockfd, char *buffer, size_t buffer_size, struct sockaddr_in *dst) {
    pkt sent_packets[2 * WINDOW_SIZE];        
    int acked[2 * WINDOW_SIZE] = {0};           
    int packets_in_flight = 0, base = _snd_seqnum, bytes_sent = 0;
    double TimeoutInterval = EstRTT + 4 * DevRTT;
    struct timeval base_sent_time, current_time, timeout;
    fd_set readfds;

    while (bytes_sent < buffer_size || packets_in_flight > 0) {
        // **Envia novos pacotes enquanto houver espaço na janela**
        while (packets_in_flight < WINDOW_SIZE && bytes_sent < buffer_size) {
            int index = _snd_seqnum % (2 * WINDOW_SIZE);
            int pkt_size = (buffer_size - bytes_sent > MAX_MSG_LEN) ? MAX_MSG_LEN : buffer_size - bytes_sent;

            make_pkt(&sent_packets[index], PKT_DATA, _snd_seqnum, buffer + bytes_sent, pkt_size);
            if (packets_in_flight == 0)  
                gettimeofday(&base_sent_time, NULL);

            sendto(sockfd, &sent_packets[index], sent_packets[index].h.pkt_size, 0, 
                   (struct sockaddr *)dst, sizeof(struct sockaddr_in));

            printf("Enviando pacote %d (%d bytes)\n", _snd_seqnum, pkt_size);

            acked[index] = 0;  
            _snd_seqnum++;
            packets_in_flight++;
            bytes_sent += pkt_size;
        }

        // **Aguardar eventos no socket**
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 50;  // Timeout curto para verificar pacotes rapidamente
        usleep((rand() % (MAX_DELAY * 2 - MIN_DELAY + 1)) + MAX_DELAY); 

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) {  // **Timeout atingido**
            gettimeofday(&current_time, NULL);
            double elapsed_time = (current_time.tv_sec - base_sent_time.tv_sec) + 
                                  (current_time.tv_usec - base_sent_time.tv_usec) / 1e6;

            if (elapsed_time > TimeoutInterval) {
                printf("Timeout no pacote base %d. Retransmitindo...\n", base);
                int base_index = base % (2 * WINDOW_SIZE);
                if (!acked[base_index]) {  
                    sendto(sockfd, &sent_packets[base_index], sent_packets[base_index].h.pkt_size, 0, 
                           (struct sockaddr *)dst, sizeof(struct sockaddr_in));
                    gettimeofday(&base_sent_time, NULL);
                    printf("Retransmitindo pacote base %d\n", base);
                }
            }
            continue;
        }

        // **Receber ACKs**
        struct sockaddr_in dst_ack;
        socklen_t addrlen = sizeof(dst_ack);
        pkt ack;
        int nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, &addrlen);

        if (nr > 0 && !iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
            int ack_index = ack.h.pkt_seq % (2 * WINDOW_SIZE);
            if (!acked[ack_index]) { 
                acked[ack_index] = 1;  

                // **Atualiza TimeoutInterval**
                gettimeofday(&current_time, NULL);
                double SampleRTT = (current_time.tv_sec - base_sent_time.tv_sec) + 
                                   (current_time.tv_usec - base_sent_time.tv_usec) / 1e6;
                EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
                DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);
                TimeoutInterval = EstRTT + 4 * DevRTT;

                printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);
                
                // **Avança a base da janela**
                while (packets_in_flight > 0 && acked[base % (2 * WINDOW_SIZE)]) {
                    acked[base % (2 * WINDOW_SIZE)] = 0;
                    packets_in_flight--;
                    base++;
                }
            }
        }

        // **Finaliza envio quando todos os pacotes foram enviados e confirmados**
        if (bytes_sent >= buffer_size && packets_in_flight == 0) {
            return FILE_DONE;
        }
    }

    return PART_SENT;
}


int rdt_recv(int sockfd, FILE *file, struct sockaddr_in *src) {
    pkt p, ack;
    socklen_t addrlen;
    
    // Buffer para pacotes fora de ordem
    pkt recv_buffer[WINDOW_SIZE];
    int received[WINDOW_SIZE] = {0};

    struct timeval timeout;
    fd_set readfds;

    printf("Recebendo arquivo...\n");

    while (1) {
        // **Aguardar pacotes usando select()**
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 50;  // Mesmo timeout do sender

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) {
            continue;
        }

        addrlen = sizeof(struct sockaddr_in);

        // **Receber pacote**
        int ns = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, &addrlen);
        if (ns < 0) {
            perror("recvfrom()");
            return SEND_ERROR;
        }

        int msg_size = p.h.pkt_size - sizeof(hdr);

        // **Caso seja um pacote de fim de arquivo**
        if (msg_size == 0) {
            printf("Fim da transmissão recebido (seq %d). Encerrando.\n", p.h.pkt_seq);
            make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            break;
        }

        // **Se o pacote estiver corrompido, reenvia o último ACK válido**
        if (iscorrupted(&p)) {
            printf("Pacote corrompido (seq %d). Reenviando último ACK (%d).\n", p.h.pkt_seq, _rcv_seqnum - 1);
            make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0);
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            continue;
        }

        // **Caso o pacote seja o esperado**
        if (p.h.pkt_seq == _rcv_seqnum) {
            printf("Pacote esperado recebido %d (%d bytes).\n", p.h.pkt_seq, msg_size);
            fwrite(p.msg, 1, msg_size, file);
            fflush(file);
            _rcv_seqnum++;

            // **Verifica se há pacotes armazenados no buffer para entrega em ordem**
            while (received[_rcv_seqnum % WINDOW_SIZE]) {
                pkt *next_pkt = &recv_buffer[_rcv_seqnum % WINDOW_SIZE];
                fwrite(next_pkt->msg, 1, next_pkt->h.pkt_size - sizeof(hdr), file);
                fflush(file);
                received[_rcv_seqnum % WINDOW_SIZE] = 0;
                _rcv_seqnum++;
            }
        }
        // **Se o pacote estiver dentro da janela, mas fora de ordem, armazena no buffer**
        else if (p.h.pkt_seq > _rcv_seqnum && p.h.pkt_seq < _rcv_seqnum + WINDOW_SIZE) {
            int index = p.h.pkt_seq % WINDOW_SIZE;
            printf("Pacote fora de ordem %d armazenado para entrega futura.\n", p.h.pkt_seq);
            recv_buffer[index] = p;
            received[index] = 1;
        }

        // **Enviar ACK para o pacote recebido**
        make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
        sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
        printf("ACK enviado para o pacote %d\n", p.h.pkt_seq);
    }

    printf("Arquivo recebido com sucesso!\n");
    return FILE_DONE;
}







