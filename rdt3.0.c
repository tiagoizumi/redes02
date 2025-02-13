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

#define MAX_MSG_LEN 16
#define ERROR -1
#define TRUE 1
#define FALSE 0
#define SUCCESS 0
#define WINDOW_SIZE 4 

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

int rdt_send(int sockfd, FILE *file, struct sockaddr_in *dst) {
    pkt p, ack;
    struct sockaddr_in dst_ack;
    int nr, addrlen;
    struct timeval start, end;
    double SampleRTT, TimeoutInterval;
    int packets_in_flight = 0;
    hseq_t base = _snd_seqnum;
    char buffer[MAX_MSG_LEN];  // Buffer de leitura do arquivo
    int bytes_read;

    TimeoutInterval = EstRTT + 4 * DevRTT;

    // **Ler e enviar o arquivo em pacotes**
    while ((bytes_read = fread(buffer, 1, MAX_MSG_LEN, file)) > 0 || packets_in_flight > 0) {
        // Enviar pacotes enquanto houver espaço na janela estática
        while (packets_in_flight < WINDOW_SIZE && bytes_read > 0) {
            // **Garantir que o buffer do pacote esteja limpo**
            memset(p.msg, 0, MAX_MSG_LEN);

            if (make_pkt(&p, PKT_DATA, _snd_seqnum, buffer, bytes_read) < 0) {
                return ERROR;
            }

            // **Imprime o conteúdo correto do pacote antes de enviar 🔹**
            printf("Enviando pacote %d (%d bytes): [", _snd_seqnum, bytes_read);
            for (int i = 0; i < bytes_read; i++) {
                printf("%c", buffer[i]);  // Exibir corretamente os caracteres
            }
            printf("]\n");

            gettimeofday(&start, NULL);
            sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in));
            _snd_seqnum++;
            packets_in_flight++;

            // ** Limpar buffer antes de ler mais caracteres**
            memset(buffer, 0, MAX_MSG_LEN);
            bytes_read = fread(buffer, 1, MAX_MSG_LEN, file);
        }

        // Configurar timeout dinâmico
        struct timeval timeout;
        timeout.tv_sec = (int)TimeoutInterval;
        timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        addrlen = sizeof(struct sockaddr_in);
        nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, (socklen_t *)&addrlen);

        if (nr > 0) {
            gettimeofday(&end, NULL);
            SampleRTT = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
            EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
            DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);
            TimeoutInterval = EstRTT + 4 * DevRTT;

            if (!iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK && ack.h.pkt_seq >= base) {
                printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);
                packets_in_flight -= (ack.h.pkt_seq - base + 1);
                base = ack.h.pkt_seq + 1;
            }
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("Timeout. Retransmitindo pacotes...\n");
            for (hseq_t i = base; i < _snd_seqnum; i++) {
                if (make_pkt(&p, PKT_DATA, i, buffer, bytes_read) < 0) {
                    return ERROR;
                }
                sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in));
                printf("Retransmitido pacote %d\n", i);
            }
        }
    }

    return SUCCESS;
}



int has_dataseqnum(pkt *p, hseq_t seqnum) {
	if (p->h.pkt_seq != seqnum || p->h.pkt_type != PKT_DATA)
		return FALSE;
	return TRUE;
}


int rdt_recv(int sockfd, struct sockaddr_in *src) {
    pkt p, ack;
    int nr, ns;
    int addrlen;
    FILE *fp;
    
    memset(&p, 0, sizeof(pkt));

    // **Criar um pacote ACK com o último número de sequência correto**
    if (make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0) < 0) 
        return ERROR;

    // **Abrir o arquivo de saída**
    fp = fopen("output.txt", "w");
    if (fp == NULL) {
        perror("Erro ao abrir output.txt");
        return ERROR;
    }

    printf("Recebendo arquivo e salvando em output.txt...\n");

    while (1) { // **Loop infinito até o arquivo ser completamente recebido**
        addrlen = sizeof(struct sockaddr_in);
        nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, (socklen_t *)&addrlen);

        if (nr < 0) {
            perror("recvfrom():");
            fclose(fp);
            return ERROR;
        }

        // **Verifica se é um pacote de fim de arquivo**
        if (p.h.pkt_size == sizeof(hdr) && p.h.pkt_type == PKT_DATA) {
            printf("Fim da transmissão recebido. Encerrando.\n");
            break;
        }

        // **Verifica se o pacote já foi processado**
        if (p.h.pkt_seq < _rcv_seqnum) {
            printf("Pacote duplicado %d recebido. Enviando ACK novamente.\n", p.h.pkt_seq);
            
            // **Reenviar ACK**
            if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) {
                fclose(fp);
                return ERROR;
            }

            ns = sendto(sockfd, &ack, ack.h.pkt_size, 0, 
                        (struct sockaddr*)src, (socklen_t)sizeof(struct sockaddr_in));

            if (ns < 0) {
                perror("rdt_recv: sendto(PKT_ACK)");
                fclose(fp);
                return ERROR;
            }

            continue;
        }

        // **Verifica se o pacote está corrompido**
        if (iscorrupted(&p)) {
            printf("Pacote corrompido (seq %d). Enviando último ACK (%d).\n", p.h.pkt_seq, _rcv_seqnum - 1);
            ns = sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, (socklen_t)sizeof(struct sockaddr_in));
            if (ns < 0) {
                perror("rdt_recv: sendto(PKT_ACK - 1)");
                fclose(fp);
                return ERROR;
            }
            continue;
        }

        // **Processar e imprimir os dados do pacote**
        int msg_size = p.h.pkt_size - sizeof(hdr);
        printf("Pacote recebido %d (%d bytes): ", p.h.pkt_seq, msg_size);
        for (int i = 0; i < msg_size; i++) {
            printf("%c", p.msg[i]);
        }
        printf("\n");

        // **Salvar no arquivo**
        fwrite(p.msg, 1, msg_size, fp);
        fflush(fp);

        // **Enviar ACK**
        if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) {
            fclose(fp);
            return ERROR;
        }

        ns = sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, (socklen_t)sizeof(struct sockaddr_in));
        if (ns < 0) {
            perror("rdt_recv: sendto(PKT_ACK)");
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


