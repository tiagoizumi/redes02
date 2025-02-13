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

#define MIN 10000000
#define MAX 900000000

int biterror_inject = FALSE;

typedef uint16_t hsize_t;
typedef uint16_t hcsum_t;
typedef uint16_t hseq_t;
typedef uint8_t  htype_t;

#define PKT_ACK 0
#define PKT_DATA 1

hseq_t _snd_seqnum = 1;
hseq_t _rcv_seqnum = 1;

double EstRTT = 0.0001;  
double DevRTT = 0.0001;  
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
    pkt sent_packets[WINDOW_SIZE];        // Buffer para os pacotes enviados
    struct timeval sent_time[WINDOW_SIZE];  // Tempo de envio de cada pacote
    int acked[WINDOW_SIZE] = {0};           // 0 = não ACK, 1 = ACK recebido

    int packets_in_flight = 0; // Quantidade de pacotes atualmente "no ar"
    int base = _snd_seqnum;    // O pacote mais antigo ainda não confirmado
    char buffer[MAX_MSG_LEN];
    int bytes_read;
    
    // TimeoutInterval baseado em EstRTT e DevRTT
    double TimeoutInterval = EstRTT + 4 * DevRTT;
    
    // Contador para limitar a duração do loop (para evitar loops infinitos)
    int loop_counter = 0;
    const int MAX_LOOP_COUNT = 1000;

    while (loop_counter < MAX_LOOP_COUNT) {
        loop_counter++;

        // 1. Envia pacotes enquanto houver espaço na janela e dados para ler do arquivo
        while (packets_in_flight < WINDOW_SIZE && (bytes_read = fread(buffer, 1, MAX_MSG_LEN, file)) > 0) {
            int index = (_snd_seqnum % WINDOW_SIZE);
            
            if (make_pkt(&sent_packets[index], PKT_DATA, _snd_seqnum, buffer, bytes_read) < 0)
                return SEND_ERROR;
            
            gettimeofday(&sent_time[index], NULL);
            if (sendto(sockfd, &sent_packets[index], sent_packets[index].h.pkt_size, 0, 
                       (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0) {
                perror("sendto");
                return SEND_ERROR;
            }
            
            printf("Enviando pacote %d (%d bytes)\n", _snd_seqnum, bytes_read);
            
            // Delay aleatório para simular variações na rede (não interfere no timer real)
            // int delay = (rand() % (900000 - 10000 + 1)) + 10000;
            // printf("Delay: %d microsegundos\n", delay);
            // usleep(delay);
            
            acked[index] = 0;  // Marca como não confirmado
            _snd_seqnum++;
            packets_in_flight++;
        }
        
        // 2. Verifica se o pacote mais antigo (base) excedeu o TimeoutInterval
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        if (packets_in_flight > 0) {
            double elapsed_time = (current_time.tv_sec - sent_time[base % WINDOW_SIZE].tv_sec) +
                                    (current_time.tv_usec - sent_time[base % WINDOW_SIZE].tv_usec) / 1e6;
            if (elapsed_time > TimeoutInterval) {
                printf("Timeout no pacote base %d (elapsed: %.3f s > %.3f s). Retransmitindo janela...\n", 
                       base, elapsed_time, TimeoutInterval);
                // Retransmite todos os pacotes pendentes a partir da base
                for (int i = 0; i < packets_in_flight; i++) {
                    int seqnum = base + i;
                    int index = seqnum % WINDOW_SIZE;
                    if (!acked[index]) {
                        if (sendto(sockfd, &sent_packets[index], sent_packets[index].h.pkt_size, 0, 
                                   (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0) {
                            perror("sendto (retransmit)");
                            return SEND_ERROR;
                        }
                        gettimeofday(&sent_time[index], NULL); // Reinicia o timer para este pacote
                        printf("Retransmitindo pacote %d\n", seqnum);
                    }
                }
            }
        }
        
        // 3. Tenta receber ACKs (operação não bloqueante)
        struct sockaddr_in dst_ack;
        socklen_t addrlen = sizeof(dst_ack);
        pkt ack;
        int nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, &addrlen);
        
        if (nr > 0) {
            gettimeofday(&current_time, NULL);
            if (!iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
                int ack_index = ack.h.pkt_seq % WINDOW_SIZE;
                
                if (!acked[ack_index]) { // Se ainda não confirmado
                    acked[ack_index] = 1; // Marca o ACK como recebido
                    
                    // Calcula o SampleRTT e atualiza EstRTT, DevRTT e TimeoutInterval
                    double SampleRTT = (current_time.tv_sec - sent_time[ack_index].tv_sec) +
                                         (current_time.tv_usec - sent_time[ack_index].tv_usec) / 1e6;
                    EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
                    DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);
                    TimeoutInterval = EstRTT + 4 * DevRTT;
                    
                    printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);
                    
                    // Avança a base da janela se o ACK for para o pacote base
                    while (packets_in_flight > 0 && acked[base % WINDOW_SIZE]) {
                        acked[base % WINDOW_SIZE] = 0;
                        packets_in_flight--;
                        base++;
                    }
                }
            }
        } else {
            // Se recvfrom falhar por um motivo diferente de EWOULDBLOCK/EAGAIN, trata o erro
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                perror("recvfrom");
                return SEND_ERROR;
            }
        }
        
        // 4. Critério de saída: se o arquivo terminou e não há pacotes pendentes, a transmissão está completa
        if (feof(file) && packets_in_flight == 0) {
            printf("Transmissão concluída nesta chamada de rdt_send!\n");
            return FILE_DONE;
        }
    }
    
    // Se atingiu o número máximo de iterações mas ainda há pacotes pendentes, retorna PART_SENT
    return PART_SENT;
}






int rdt_recv(int sockfd, struct sockaddr_in *src) {
    pkt p, ack;
    int ns;
    socklen_t addrlen;
    FILE *fp;
    
    // Buffer para armazenar pacotes fora de ordem
    pkt recv_buffer[WINDOW_SIZE];
    int received[WINDOW_SIZE] = {0};


    // Abrir arquivo de saída
    fp = fopen("output.txt", "w");
    if (fp == NULL) {
        perror("Erro ao abrir output.txt");
        return ERROR;
    }

    printf("Recebendo arquivo e salvando em output.txt...\n");

    while (1) {
        addrlen = sizeof(struct sockaddr_in);
        
        // Receber pacote
        ns = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, &addrlen);
        if (ns < 0) {
            perror("recvfrom()");
            fclose(fp);
            return ERROR;
        }

        // Verificar se é um pacote de fim de arquivo
        int msg_size = p.h.pkt_size - sizeof(hdr);
        if (msg_size == 0) {
            printf("Fim da transmissão recebido (seq %d). Encerrando.\n", p.h.pkt_seq);
            
            // Enviar ACK final
            if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) {
                fclose(fp);
                return ERROR;
            }
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            break;
        }

        // Se o pacote estiver corrompido, reenvia o último ACK
        if (iscorrupted(&p)) {
            printf("Pacote corrompido (seq %d). Enviando último ACK (%d).\n", p.h.pkt_seq, _rcv_seqnum - 1);

            if (make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0) < 0) {
                fclose(fp);
                return ERROR;
            }
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            continue;
        }

        // Se o pacote for esperado, processa e entrega
        if (p.h.pkt_seq == _rcv_seqnum) {
            printf("Pacote esperado recebido %d (%d bytes): ", p.h.pkt_seq, msg_size);
            fwrite(p.msg, 1, msg_size, fp);
            fflush(fp);
            
            _rcv_seqnum++;

            // Verifica se há pacotes armazenados no buffer que podem ser entregues
            while (received[_rcv_seqnum % WINDOW_SIZE]) {
                pkt *next_pkt = &recv_buffer[_rcv_seqnum % WINDOW_SIZE];
                int next_size = next_pkt->h.pkt_size - sizeof(hdr);
                
                fwrite(next_pkt->msg, 1, next_size, fp);
                fflush(fp);
                
                received[_rcv_seqnum % WINDOW_SIZE] = 0;
                _rcv_seqnum++;
            }
        } 
        // Se o pacote estiver dentro da janela, armazena no buffer
        else if (p.h.pkt_seq > _rcv_seqnum && p.h.pkt_seq < _rcv_seqnum + WINDOW_SIZE) {
            int index = p.h.pkt_seq % WINDOW_SIZE;
            printf("Pacote fora de ordem %d armazenado para entrega futura.\n", p.h.pkt_seq);
            recv_buffer[index] = p;
            received[index] = 1;
        }

        // Atualizar TimeoutInterval após cada ACK recebido


        // Enviar ACK para o pacote recebido
        if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) {
            fclose(fp);
            return ERROR;
        }
        sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));

        printf("ACK enviado para o pacote %d\n", p.h.pkt_seq);
    }

    fclose(fp);
    printf("Arquivo salvo como output.txt\n");
    return SUCCESS;
}






