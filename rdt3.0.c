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

double EstRTT = 1.0;  
double DevRTT = 0.5;  
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
    static int packets_in_flight = 0; 
    static int file_end = 0; 
    static hseq_t base = 0; 
    static pkt sent_packets[WINDOW_SIZE]; 
    static int sent_sizes[WINDOW_SIZE]; 
    static struct timeval sent_time[WINDOW_SIZE]; 

    // Envia pacotes até preencher a janela ou chegar ao final do arquivo
    while (packets_in_flight < WINDOW_SIZE && !file_end) {
        char data_buffer[MAX_MSG_LEN]; 
        int bytes_read = fread(data_buffer, 1, MAX_MSG_LEN, file);
        
        if (bytes_read <= 0) { 
            // Se o arquivo chegou ao fim, indica que não há mais pacotes para enviar
            file_end = 1;
            break;
        }

        // Cria um pacote com os dados lidos
        pkt p;
        memset(&p, 0, sizeof(pkt)); // Garante que o pacote esteja zerado
        if (make_pkt(&p, PKT_DATA, _snd_seqnum, data_buffer, bytes_read) < 0)
            return SEND_ERROR;

        // Armazena o pacote e suas informações para possível retransmissão
        int index = _snd_seqnum % WINDOW_SIZE;
        sent_packets[index] = p;
        sent_sizes[index] = bytes_read;
        gettimeofday(&sent_time[index], NULL);

        // Envia o pacote para o destino via UDP
        if (sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0) {
            perror("sendto");
            return SEND_ERROR;
        }

        _snd_seqnum++;  // Incrementa o número de sequência
        packets_in_flight++; // Aumenta o número de pacotes em trânsito
    }



    struct timeval timeout;

    double TimeoutInterval = EstRTT + 4 * DevRTT;
    timeout.tv_sec = (int)TimeoutInterval;
    timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;


	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		perror("setsockopt");
		return SEND_ERROR;
	}



    // Aguarda a recepção de um ACK do destinatário
    struct sockaddr_in dst_ack;
    int addrlen = sizeof(dst_ack);
    pkt ack;
    int nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, (socklen_t *)&addrlen);

    if (nr > 0) {
        if (!iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
            printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);

            // Atualiza o tempo estimado de RTT
            int ack_index = ack.h.pkt_seq % WINDOW_SIZE;
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            double SampleRTT = (current_time.tv_sec - sent_time[ack_index].tv_sec) +
                               (current_time.tv_usec - sent_time[ack_index].tv_usec) / 1e6;
            EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
            DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);
            TimeoutInterval = EstRTT + 4 * DevRTT;

            //  ACK para um pacote dentro da janela, atualiza a base
            if (ack.h.pkt_seq >= base) {
                int num_acked = (ack.h.pkt_seq - base) + 1;
                packets_in_flight -= num_acked;
                base = ack.h.pkt_seq + 1;
            }
        }
    } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
        printf("timeout retransmitindo pacotes\n");
        for (hseq_t i = base; i < _snd_seqnum; i++) {
            int index = i % WINDOW_SIZE;
            if (sendto(sockfd, &sent_packets[index], sent_packets[index].h.pkt_size, 0,
                       (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0) {
                perror("sendto");
                return SEND_ERROR;
            }
            printf("retransmitido pacote %d\n", i);
            gettimeofday(&sent_time[index], NULL);
        }
    }

	if (file_end && packets_in_flight == 0) {
		printf("fechando conexao\n");
		close(sockfd);  // Cliente fecha o socket
		return FILE_DONE;
	}

    return PART_SENT; 
}



int rdt_recv(int sockfd, struct sockaddr_in *src) {
    pkt p, ack;
    int nr, ns;
    socklen_t addrlen;
    FILE *fp;
    
    // Abre o arquivo para salvar os dados recebidos
    fp = fopen("output.txt", "w");

    while (1) {
        addrlen = sizeof(struct sockaddr_in);

        // Aguarda o recebimento de um pacote
        nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, &addrlen);
        if (nr < 0) {
            perror("recvfrom()");
            fclose(fp);
            return ERROR;
        }

        // Calcula o tamanho real da mensagem, excluindo o cabeçalho
        int msg_size = p.h.pkt_size - sizeof(hdr);

        // Se a mensagem for vazia, considera que o pacote de término foi recebido
        if (msg_size == 0) {
            printf("Fim da transmissão recebido (seq %d). Encerrando.\n", p.h.pkt_seq);

            // Envia um ACK final para confirmar o recebimento do pacote de término
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

        if (iscorrupted(&p)) {
            printf("Pacote corrompido (seq %d). Enviando último ACK (%d).\n", p.h.pkt_seq, _rcv_seqnum - 1);

            // Se o pacote estiver corrompido, reenvia um ACK para o último pacote correto recebido
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
            // Ignora o pacote corrompdo 
            continue;
        }
        // Se o pacote está fora de ordem
        if (p.h.pkt_seq != _rcv_seqnum) {
            printf("Pacote fora de ordem (esperado %d, recebido %d). Reenviando último ACK (%d).\n", 
                   _rcv_seqnum, p.h.pkt_seq, _rcv_seqnum - 1);

            // Reenvia o ACK para o último pacote corretamente recebido
            if (make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0) < 0) {
                fclose(fp);
                return ERROR;
            }

            ns = sendto(sockfd, &ack, ack.h.pkt_size, 0, 
                        (struct sockaddr*)src, sizeof(struct sockaddr_in));

            if (ns < 0) {
                perror("rdt_recv: sendto(PKT_ACK) out-of-order");
                fclose(fp);
                return ERROR;
            }

            // ignora o pacote 
            continue;
        }

        // Pacote válido: salva os dados no arquivo
        printf("Pacote recebido %d (%d bytes): ", p.h.pkt_seq, msg_size);
        for (int i = 0; i < msg_size; i++) {
            printf("%c", p.msg[i]);
        }
        printf("\n");

        fwrite(p.msg, 1, msg_size, fp);
        fflush(fp);

        // Envia um ACK confirmando o recebimento do pacote correto
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

        // Atualiza o número de sequência esperado para o próximo pacote
        _rcv_seqnum++;
    }

    // Fecha o arquivo após a recepção completa dos dados
    fclose(fp);

    return SUCCESS;
}




