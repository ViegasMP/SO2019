/*
    Ana Luisa da Rocha Alves Rainha Coelho - 2015231777
    Maria Paula de Alencar Viegas  -  2017125592
    gcc main.c -lpthread -D_REENTRANT -Wall -o prog estruturas.h  -lm
    echo "ORDER REQ_1 Prod:A, 5 to: 300, 100" >input_pipe

    sleep unidade de tempo MILISEGUNDO
    espera ativa do drone
    variavel de condicao


*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/types.h>
#include <math.h>
#include <sys/msg.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/time.h>
#include <semaphore.h>

#include "drone_movement.h"
#include "estruturas.h"

#define MAX_BUFFER  1000
#define PIPE_NAME "input_pipe"

//Lista de Encomendas
Encomenda *headListaE;
int id_encomenda = 0;

//informacoes ficheiro
Dados *dados;
Warehouse *armazens;
char mensagem[MAX_BUFFER];
FILE *fp_log;

//memoria partilhada
Estats *estatisticas;
Warehouse *armazensShm;
int shmid_estats, shmid_armazens, mutex_shmid;

//pipe
int fd_pipe;

//processos
pid_t idCentral;
pid_t idArmazem;
pid_t processo_central;
pid_t processo_gestor;
pid_t processo_armazem, pidWh;

//threads
pthread_t *my_thread, charger;
Drones *arrayDrones;

//Mutexes
mutex_struct *mutexes;
pthread_cond_t cond_nao_escolhido = PTHREAD_COND_INITIALIZER;

// Semáforos
sem_t *sem_write_stats;
sem_t *sem_write_armazens;
sem_t *sem_write_file;
sem_t *sem_ctrlC;

//variavel de condicao
pthread_mutex_t drones_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t drones_call_cv = PTHREAD_COND_INITIALIZER;

//exemplo encomenda
Encomenda *novoNode;

//sinal
int exit_flag=0;

//fila de mensagens
int mq_id;

//funcoes
void generateStock();
void escolheDrone();
void escolheArmazem();
void *controla_drone(void *id_ptr);
void write_log(char* mensagem);
void criaArmazens(int n);
void central();
void init_mutex();
void criaDrones(int numI, int qtd);
void initShm(Warehouse *arrayArmazens);
void *baseCharger();
void sinal_estatistica(int signum);
void init_sem();
void destruir_threads();
void close_sem();
void sinal_saida (int sig);
void destruirShM_ware();
void destruirShm_stats();
void cria_named_pipe();
void cria_MQ();
void escolheBase(double *buf,double x, double y);
void leitura_pipe();

int main() {
    dados = (Dados *) malloc(sizeof(Dados));
    int i = 0;
    long pos;
    char *token;
    char *linha = (char *) malloc(sizeof(char) * MAX_BUFFER);
    char *produto = (char *) malloc(sizeof(char) * 255);
    pid_t novo_processo;
    Warehouse *arrayArmazens=NULL;
    int armazemN = 0;
    time_t tempo = time(NULL);
    struct tm *t = localtime(&tempo);

    signal(SIGINT, sinal_saida);
    signal(SIGUSR1, sinal_estatistica);

    srand(time(NULL));

    //Encomenda teste para Meta 1
    novoNode = malloc(sizeof(Encomenda));
    //novoNode->nomeEncomenda = "Encomenda Teste";
    //novoNode->qtd = 5;
    //novoNode->tipo_produto = "Prod_A";
    //novoNode->coordenadas[0] = (double) 300;
    //novoNode->coordenadas[1] = (double) 100;
    //novoNode->nSque = id_encomenda;

    processo_gestor = getpid();
    sprintf(mensagem, "\n\n\n%d:%d:%d Inicio do programa [%d]\n",t->tm_hour,t->tm_min,t->tm_sec,getpid());
    printf("%s", mensagem);
    //abre ficheiro log
    fp_log = fopen("log.txt","a");
    write_log(mensagem);

    //cria fila de mensagens
    cria_MQ();

    //leitura do ficheiro
    printf("\n----------Informacoes do ficheiro--------\n");
    FILE *fp = fopen("config.txt", "r");
    if (fp != NULL) {
        //primeira linha do ficheiro
        fscanf(fp, "%d, %d\n", &dados->max_x, &dados->max_y);
        printf("%d   %d\n", dados->max_x, dados->max_y);
        pos = ftell(fp);
        fseek(fp, pos, SEEK_SET); //troca a linha

        //segunda linha do ficheiro
        fgets(linha, MAX_BUFFER, fp); //recebe a linha
        //troca o \n por \0
        linha[strlen(linha) - 1] = '\0';
        token = strtok(linha, ", ");
        while (token != NULL) {
            produto = token;
            //printf("%s\n", produto);
            token = strtok(NULL, ", ");
            dados->tipos_produtos[i] = produto;
            //printf("%s\n", dados->tipos_produtos[i]);
            i++;//numero de produtos diferentes
        }
        printf("%s    %s     %s     %s\n", dados->tipos_produtos[0], dados->tipos_produtos[1], dados->tipos_produtos[2], dados->tipos_produtos[3]);
        //printf("%s\n", dados->tipos_produtos[0]);

        //terceira linha do ficheiro
        fscanf(fp, "%d, %d, %d\n", &dados->n_drones, &dados->bInit, &dados->bMax);
        pos = ftell(fp);
        fseek(fp, pos, SEEK_SET); //troca a linha
        printf("%d, %d, %d\n", dados->n_drones, dados->bInit, dados->bMax);

        //quarta linha do ficheiro
        fscanf(fp, "%d, %d, %d\n", &dados->f_abast, &dados->qtd, &dados->unidadeT);
        pos = ftell(fp);
        fseek(fp, pos, SEEK_SET); //troca a linha
        printf("%d   %d     %d \n", dados->f_abast, dados->qtd, dados->unidadeT);

        //sexta linha do ficheiro
        fscanf(fp, "%d \n", &dados->numWh);
        pos = ftell(fp);
        fseek(fp, pos, SEEK_SET); //troca a linha
        printf("%d\n", dados->numWh);

        //leitura de cada armazem ate o fim do ficheiro
        i = 0;
        arrayArmazens = malloc(sizeof(Warehouse) * dados->numWh);
        while (i < dados->numWh) {
            char *info = (char *) malloc(sizeof(char) * MAX_BUFFER);
            char *nome_prod = (char *) malloc(sizeof(char) * MAX_BUFFER);
            char *qtdI = (char *) malloc(sizeof(char) * MAX_BUFFER);
            char *nome = (char *) malloc(sizeof(char) * MAX_BUFFER);
            int x, y;

            fscanf(fp, "%s xy: %d, %d prod: ", nome, &x, &y);
            printf("%s    %d  %d\n", nome, x, y);
            arrayArmazens[i].nome = nome;
            arrayArmazens[i].coordenadas[0] = x;
            arrayArmazens[i].coordenadas[1] = y;
            arrayArmazens[i].idArmazem = i;


            fgets(info, MAX_BUFFER, fp);

            info[strlen(info) - 1] = '\0';

            token = strtok(info, ", ");
            int aux = 0;
            while (token) {
                nome_prod = token;

                token = strtok(NULL, ", ");
                qtdI = token;
                token = strtok(NULL, ", ");

                //guarda o produto e suas informacoes na lista de structprodutos
                struct produtos *nProd = malloc(sizeof(struct produtos));
                nProd->produto = nome_prod;
                nProd->qt = atoi(qtdI);
                arrayArmazens[i].produtos[aux].produto = nProd->produto;
                arrayArmazens[i].produtos[aux].qt = nProd->qt;
                printf("%s    %d  \n", arrayArmazens[i].produtos[aux].produto, arrayArmazens[i].produtos[aux].qt);
                aux++;
            }
            //troca de linha do ficheiro
            pos = ftell(fp);
            fseek(fp, pos, SEEK_SET);
            i++;
        }
    }
    fclose(fp);
    printf("Ficheiro lido.\n");
    printf("-----------------------------------------\n\n");

    //ficheiro lido

    //inicializa os mutexes
    init_sem();

    //inicializa os mutexes
    init_mutex();

    //cria shared mem
    initShm(arrayArmazens);

    novo_processo = processo_gestor;
    novo_processo = fork();
    for (i = 0; i < (dados->numWh + 1); i++) {
        //Cria processo central
        if (i == 0) {
            if (novo_processo == 0) { //guardar na variavel apenas para o processo central
                //signal(SIGINT, central_exit);
                processo_central = getpid();
                central();
            }
        } else { //Cria processos armazens
            novo_processo = fork();
            if (novo_processo == 0) {
                criaArmazens(armazemN);
            }
            armazemN++;
        }
    }

    generateStock();

    while(wait(NULL)>0);   
}

//Gera reabastecimento de stock
void generateStock(){
    int contador = 0;
    while(1) {
                int k = rand()%3;
        //cria mensagem com atualizacao do stock
        msg atualizaStock;
        atualizaStock.idArmazem = contador;
        atualizaStock.prod_type= k;
        atualizaStock.qtd = dados->qtd;
        atualizaStock.mtype = (long) 100+contador;
        strcpy(atualizaStock.prod_type_name, armazensShm[atualizaStock.idArmazem].produtos[k].produto);

        //envia para a fila de mensagens
        msgsnd(mq_id, &atualizaStock, sizeof(atualizaStock) - sizeof(long), 0);

        printf("Mensagem de atualizacao de stock enviada:\n");
        printf("\tArmazem%d: %d produtos %s\n", atualizaStock.idArmazem, atualizaStock.qtd, atualizaStock.prod_type_name);
        
        
        contador++;
        if(contador >= dados->numWh) {
            contador = 0;
        }
        sleep(dados->unidadeT/5);
    }
}

// Inicializar semaforos
void init_sem(){
    time_t tempo = time(NULL);
    struct tm *t = localtime(&tempo);

    sem_unlink("sem_write_armazens");
    sem_unlink("sem_write_stats");
    sem_unlink("sem_write_file");
    sem_write_armazens = sem_open("sem_write_armazens", O_CREAT | O_EXCL, 0700, 1);
    sem_write_stats = sem_open("sem_write_stats", O_CREAT | O_EXCL, 0700, 1);
    sem_write_file = sem_open("sem_write_file", O_CREAT | O_EXCL, 0700, 1);

    sprintf(mensagem, "->%d:%d:%d Semaforos criados\n",t->tm_hour,t->tm_min,t->tm_sec);
    printf("%s", mensagem);
    sem_wait(sem_write_file);
    write_log(mensagem);
    sem_post(sem_write_file);
    mensagem[0]='\0';
}

// Inicializar mutexes
void init_mutex(){
    time_t tempo = time(NULL);
    struct tm *t = localtime(&tempo);
    if((mutex_shmid = shmget(IPC_PRIVATE, sizeof(mutex_struct*), IPC_CREAT|0700))<0){
        perror("Error - shmget() of mutexes struct");
    }

    if((mutexes = (mutex_struct*) shmat(mutex_shmid, NULL, 0)) == (mutex_struct*)-1){
        perror("Error - shmat() of mutexes struct");
    }

    // escrita log file
    if(pthread_mutex_init(&mutexes->write_file, NULL) != 0) {
        perror("Error - init() of mutexes->write_file");
    }

    if(pthread_mutex_init(&mutexes->ctrlc, NULL) != 0) {
        perror("Error - init() of mutexes->ctrlc");
    }

    if(pthread_mutex_init(&mutexes->write_stats, NULL) != 0) {
        perror("Error - init() of mutexes->write_stats");
    }


    if(pthread_mutex_init(&mutexes->write_armazens, NULL) != 0) {
        perror("Error - init() of mutexes->write_armazens");
    }

    if(pthread_mutex_init(&mutexes->drones, NULL) != 0) {
        perror("Error - init() of mutexes->drones");
    }

    sprintf(mensagem, "->%d:%d:%d Mutexes criados\n",t->tm_hour,t->tm_min,t->tm_sec);
    printf("%s", mensagem);
    sem_wait(sem_write_file);
    write_log(mensagem);
    sem_post(sem_write_file);
    mensagem[0]='\0';
}

//inicializa estruturas de memória partilhada
void initShm(Warehouse *arrayArmazens){
    //inicia memoria partilhada
    if ((shmid_estats = shmget(IPC_PRIVATE, sizeof(Estats), IPC_CREAT | 0766)) < 0) {
        printf("Erro no smget\n");
        exit(1);
    }
    if ((estatisticas = (Estats *) shmat(shmid_estats, NULL, 0)) < 0) {
        printf("error no shmat");
        exit(1);
    }
    estatisticas->encomendas_entregues = 0;
    estatisticas->encomendas_descartadas = 0;
    estatisticas->prod_carregados = 0;
    estatisticas->prod_entregues = 0;
    estatisticas->encomendas_atribuidas = 0;
    estatisticas->tempo_medio_individual = 0.0;
    estatisticas->tempo_medio_total = 0.0;
    printf("->Memoria partilhada de estatisticas.\n");

    //cria shared mem_armazens
    if((shmid_armazens = shmget(IPC_PRIVATE, sizeof(Warehouse) * dados->numWh, IPC_CREAT | 0766))<0) {
        printf("Erro no smget\n");
        exit(1);
    }
    if((armazensShm = (Warehouse *) shmat(shmid_armazens, NULL, 0))<0){
        printf("Erro no shmat");
        exit(1);
    }
    printf("->Memoria partilhada criada.\n");
    //memoria partilhada criada

    for (int k = 0; k < dados->numWh; k++) {
        armazensShm[k].nome = arrayArmazens[k].nome;
        armazensShm[k].coordenadas[0] = arrayArmazens[k].coordenadas[0];
        armazensShm[k].coordenadas[1] = arrayArmazens[k].coordenadas[1];
        armazensShm[k].produtos[0] = arrayArmazens[k].produtos[0];
        armazensShm[k].produtos[1] = arrayArmazens[k].produtos[1];
        armazensShm[k].produtos[2] = arrayArmazens[k].produtos[2];
        armazensShm[k].idArmazem = arrayArmazens[k].idArmazem;
    }
    printf("->Armazens na Shared Memory.\n");
}

//Cria fila de mensagens
void cria_MQ(){

    if((mq_id = msgget(IPC_PRIVATE, IPC_CREAT|0700)) < 0){
        perror("Problem creating message queue");
        exit(0);
    }
    printf("->MQ criada.");

}

//Atualiza armazens
void criaArmazens(int n) {
    //tempo
    time_t tempo = time(NULL);
    struct tm *t = localtime(&tempo);
    Warehouse aux;

    for (int i = 0; i < dados->numWh; i++) {
        if (armazensShm[i].idArmazem == n) {
            aux = armazensShm[i];
        }
    }
    
    pthread_mutex_lock(&mutexes->write_armazens);
    armazensShm[n].pid = getpid();
    pthread_mutex_unlock(&mutexes->write_armazens);
    
    printf("Armazem %s coordenadas x: %d y: %d, produto : %s qt:%d\n", aux.nome, aux.coordenadas[0],
           aux.coordenadas[1], aux.produtos[0].produto, aux.produtos[0].qt);
    
    sprintf(mensagem, "%d:%d:%d Warehouse%d criada (id = %ld)\n", t->tm_hour, t->tm_min, t->tm_sec, n, (long) getpid());
    sem_wait(sem_write_file);
    write_log(mensagem);
    sem_post(sem_write_file);
    mensagem[0] = '\0';
    fflush(stdout);

    while(1){
        msg mensagem;
        msgrcv(mq_id, &mensagem, sizeof(msg)-sizeof(long), (100+n), 0);
        printf("\nStock atualizado!\n");
        sem_wait(sem_write_armazens);
        armazensShm[mensagem.idArmazem].produtos[mensagem.prod_type].qt += mensagem.qtd;
        armazensShm[mensagem.idArmazem].comentario=0;
        printf("Armazem %d atualizado: %d produtos %s\n", armazensShm[mensagem.idArmazem].idArmazem, armazensShm[mensagem.idArmazem].produtos[mensagem.prod_type].qt, armazensShm[mensagem.idArmazem].produtos[mensagem.prod_type].produto);
        sem_post(sem_write_armazens);
    
        
        msgrcv(mq_id, &mensagem, sizeof(msg)-sizeof(long), mensagem.idDrone, 0);
        printf("[%d] Armazem%d foi notificado pelo Drone%d\n", getpid(), n, mensagem.idDrone);
        sleep(mensagem.qtd);
        printf("[%d] Armazem notifica o Drone%d\n", getpid(), mensagem.idDrone);
        sem_wait(sem_write_armazens);
        armazensShm[mensagem.idArmazem].produtos[mensagem.prod_type].qt -= armazensShm[mensagem.idArmazem].reservados[mensagem.prod_type].qt;
        armazensShm[mensagem.idArmazem].reservados[mensagem.prod_type].qt -= mensagem.qtd;
        sem_post(sem_write_armazens);
        msg msg_snd;
        msg_snd.mtype = mensagem.mtype;
        msg_snd.idArmazem = n;
        msg_snd.qtd = mensagem.qtd;
        msg_snd.prod_type = mensagem.prod_type;
        msg_snd.idDrone=mensagem.idDrone;
        msgsnd(mq_id, &msg_snd, sizeof(msg)-sizeof(long), 0);
        
        
    }

    exit(0);
}

//Movimentacao do drone ate armazem
void *controla_drone (void *id) {

    //idDrone vai ter o indice do array de drones para encontrar o id desse drone
    int idDrone = *(int*)id;
    //tempo
    time_t tempo = time(NULL);
    struct tm *t = localtime(&tempo);
    int hora, min, seg;
    float tTotal=0.0;

    printf("[%d] Sou um drone com id: %d\n", idDrone, arrayDrones[idDrone].id);

    while(1){
        pthread_mutex_lock(&drones_mutex);

        while(arrayDrones[idDrone].encomenda_drone == NULL){
            pthread_cond_wait(&drones_call_cv, &drones_mutex);
        }

        if(arrayDrones[idDrone].estado == 2){
            printf("[%d]Recebi uma encomenda %s\n", arrayDrones[idDrone].id, arrayDrones[idDrone].encomenda_drone->nomeEncomenda);
            printf("DX: %0.2f   DY: %0.2f    AX:  %0.2f      AY:   %0.2f \n", arrayDrones[idDrone].posI[0], arrayDrones[idDrone].posI[1],
                arrayDrones[idDrone].encomenda_drone->coordenadasArmazem[0], arrayDrones[idDrone].encomenda_drone->coordenadasArmazem[1]);
            
            //deslocamento para carregamento
            while (move_towards(&arrayDrones[idDrone].posI[0], &arrayDrones[idDrone].posI[1],
                                arrayDrones[idDrone].encomenda_drone->coordenadasArmazem[0],
                                arrayDrones[idDrone].encomenda_drone->coordenadasArmazem[1]) >= 0) {
                //printf("DRONE %d a deslocar se para Armazem (X: %0.2f   Y: %0.2f)\n ", arrayDrones[idDrone].id,  arrayDrones[idDrone].posI[0],  arrayDrones[idDrone].posI[1]);
                arrayDrones[idDrone].bateria-=1;  
            }
            sleep(dados->unidadeT/500);
            printf("Chegou no Armazem\n");
            arrayDrones[idDrone].estado = 3;
        }

        //carregamento
        if(arrayDrones[idDrone].estado == 3){
            printf("[%d] Notifica o armazem...\n", arrayDrones[idDrone].id);
            msg msg_wh;
            
            msg_wh.idArmazem = arrayDrones[idDrone].encomenda_drone->idArmazem;
            strcpy(msg_wh.prod_type_name, arrayDrones[idDrone].encomenda_drone->tipo_produto);
            msg_wh.qtd = arrayDrones[idDrone].encomenda_drone->qtd;
            msg_wh.idDrone = arrayDrones[idDrone].id;
            msg_wh.mtype = (long) arrayDrones[idDrone].encomenda_drone->idArmazem + 100;
            msgsnd(mq_id, &msg_wh, sizeof(msg_wh)-sizeof(long), 0);
            
            msg msg_rcv;
            while(1){
                if(msgrcv(mq_id, &msg_rcv, sizeof(msg)-sizeof(long), arrayDrones[idDrone].id, 0)){
                    printf("[%d] Carregamento concluido\n", idDrone);
                    sem_wait(sem_write_stats);
                    estatisticas->prod_carregados += arrayDrones[idDrone].encomenda_drone->qtd;
                    sem_post(sem_write_stats);
                    arrayDrones[idDrone].estado = 4;
                    break;
                }
            }
        }
            
        //deslocação para entrega
        if(arrayDrones[idDrone].estado == 4){
            printf("[%d]Drone a deslocar-se para entrega\n", arrayDrones[idDrone].id);
            while (move_towards(&arrayDrones[idDrone].posI[0], &arrayDrones[idDrone].posI[1],
                                arrayDrones[idDrone].encomenda_drone->coordenadas[0],
                                arrayDrones[idDrone].encomenda_drone->coordenadas[1]) >= 0) {
                //printf("DRONE %d a deslocar se para o Destino (X: %0.2f   Y: %0.2f)\n ",arrayDrones[idDrone].id, arrayDrones[idDrone].posI[0], arrayDrones[idDrone].posI[1]);
                arrayDrones[idDrone].bateria-=1;
            }
            sleep(dados->unidadeT/500);
            arrayDrones[idDrone].estado  = 5;

            //calcula tempo de duracao da entrega da encomenda
            hora = (t->tm_hour) - (arrayDrones[idDrone].encomenda_drone->hora);
            min = (t->tm_min) - (arrayDrones[idDrone].encomenda_drone->min);
            seg = (t->tm_sec) - (arrayDrones[idDrone].encomenda_drone->seg);
            tTotal = (hora*3600)+(min*60)+seg;

            //atualiza estatisticas
            sem_wait(sem_write_stats);
            estatisticas->tempo_medio_individual += tTotal;
            estatisticas->encomendas_entregues += 1;
            estatisticas->prod_entregues += arrayDrones[idDrone].encomenda_drone->qtd;
            sem_post(sem_write_stats);
            
            //escreve no log
            sprintf(mensagem, "%d:%d:%d Encomenda %s-%d entregue no destino pelo drone %d\n",  t->tm_hour, t->tm_min, t->tm_sec, arrayDrones[idDrone].encomenda_drone->nomeEncomenda, arrayDrones[idDrone].encomenda_drone->nSque, arrayDrones[idDrone].id);
            printf("%s", mensagem);
            sem_wait(sem_write_file);
            write_log(mensagem);
            sem_post(sem_write_file);
            mensagem[0] = '\0';

            arrayDrones[idDrone].encomenda_drone=NULL;

            escolheBase(arrayDrones[idDrone].posF,arrayDrones[idDrone].posI[0],arrayDrones[idDrone].posI[1]);

        }

        if(arrayDrones[idDrone].estado==5){
            printf("[%d]Regressar a base [%.2f, %.2f]\n", arrayDrones[idDrone].id, arrayDrones[idDrone].posF[0], arrayDrones[idDrone].posF[1]);
            while (move_towards(&arrayDrones[idDrone].posI[0], &arrayDrones[idDrone].posI[1],
                                arrayDrones[idDrone].posF[0],arrayDrones[idDrone].posF[1]) >= 0 
                                && arrayDrones[idDrone].estado==5) {
                //printf("[%d]Drone a deslocar se para Base (X: %0.2f   Y: %0.2f)\n",arrayDrones[idDrone].id, arrayDrones[idDrone].posI[0], arrayDrones[idDrone].posI[1]);
                arrayDrones[idDrone].bateria-=1;
            }
            sleep(dados->unidadeT/500);

            printf("Chegou a base\n");
            arrayDrones[idDrone].estado = 1;
        }
        
        sleep(dados->unidadeT/50);
        pthread_mutex_unlock(&drones_mutex);
    }
}

//Escolhe base mais proxima de um drone
void escolheBase(double buf[2],double x, double y){
    for(int i =0; i < 4; i++) {
        if( i % 4 == 1) {
            //[0,dados->max_y]
            if(distance(x,y,(double) 0,(double)dados->max_y) < distance(x,y,buf[0],buf[1])){
                buf[0] = 0;
                buf[1] = dados->max_y;
            }
        } else if (i%4 == 2) {
            //[dados->max_x,dados->max_y]
            if(distance(x,y,(double)dados->max_x,(double)dados->max_y) < distance(x,y,buf[0],buf[1])){
                buf[0] = dados->max_x;
                buf[1] = dados->max_y;
            }

        } else if (i%4 == 3){
            //[dados->max_x,0]
            if(distance(x,y,(double) dados->max_x,(double)0) < distance(x,y,buf[0],buf[1])){
                buf[0] = dados->max_x;
                buf[1] = 0;
            }
        } else if (i%4 == 0) {
            //[0,0]
            if(distance(x,y,(double) 0,(double)dados->max_y) < distance(x,y,buf[0],buf[1])){
                buf[0] = 0;
                buf[1] = 0;
            }
        }
    }
}

//escolhe o drone para uma encomenda
void escolheDrone(){
    printf("\nSELECIONAR O DRONE PARA A ENCOMENDA FEITA\n");
    /*
    printf("Encomenda: %s\n", novoNode->nomeEncomenda);
    printf("prod: %s\n", novoNode->tipo_produto);
    printf("qtd: %d\n", novoNode->qtd);
    printf("posI: %f %f\n", novoNode->coordenadas[0], novoNode->coordenadas[1]);
    printf("posF: %f %f\n", novoNode->coordenadasArmazem[0], novoNode->coordenadasArmazem[1]);
    printf("\n");
    */
    int distMin=2*(dados->max_x+dados->max_y);
    int idEscolhido=-1;
    int distanciaTotal=0, distanciaArmazem=0, distanciaEntrega=0;

    for(int i=0;i<dados->n_drones; i++){
        if(arrayDrones[i].estado == 1 || arrayDrones[i].estado == 5){ //se o drone tiver desocupado
            //calcular distancia
            distanciaArmazem = distance(arrayDrones[i].posI[0],arrayDrones[i].posI[1],novoNode->coordenadasArmazem[0],novoNode->coordenadasArmazem[1]);
            distanciaEntrega = distance(novoNode->coordenadasArmazem[0],novoNode->coordenadasArmazem[1], novoNode->coordenadas[0], novoNode->coordenadas[1]);
            distanciaTotal = distanciaArmazem+distanciaEntrega;
            //printf("distancia = %d\n", distanciaTotal);
            if(distanciaTotal < arrayDrones[i].bateria){       
                if(distanciaTotal < distMin){
                //se a distancia for menor que a distancia minima atual, a nossa nova distancia minima e essa
                //id do drone escolhido e atualizado
                    distMin=distanciaTotal;
                    idEscolhido=i;
                }
            }
        }
    }

    printf("idEscolhido = %d\n", idEscolhido);
    if(idEscolhido!=-1){
        arrayDrones[idEscolhido].estado=2;   //o drone ja nao esta mais em repouso 
        printf("Drone%d mudou para %d\n",arrayDrones[idEscolhido].id,arrayDrones[idEscolhido].estado);
        novoNode->id_drone = arrayDrones[idEscolhido].id;    //guarda em encomenda o id do drone responsavel por ela
        arrayDrones[idEscolhido].encomenda_drone = malloc(sizeof(Encomenda));
        //guarda as informacoes da encomenda no drone
        arrayDrones[idEscolhido].encomenda_drone = novoNode;
        arrayDrones[idEscolhido].encomenda_drone->hora = novoNode ->hora;
        arrayDrones[idEscolhido].encomenda_drone->min = novoNode ->min;
        arrayDrones[idEscolhido].encomenda_drone->seg = novoNode ->seg;        
        //atualiza estatisticas
        pthread_mutex_lock(&mutexes->write_stats);
        estatisticas->encomendas_atribuidas += 1;
        pthread_mutex_unlock(&mutexes->write_stats);

    }
}

//escolhe o armazem para uma encomenda
void escolheArmazem(){
    int flag = 1;
    printf("\nSELECIONAR O ARMAZEM PARA A ENCOMENDA FEITA\n");
    for(int k = 0;k < dados->numWh;k++) {
        for (int i = 0; i < 3; i++) {
            if (strcmp(armazensShm[k].produtos[i].produto,novoNode->tipo_produto) == 0) {
                if(armazensShm[k].produtos[i].qt - armazensShm[k].reservados[i].qt >= novoNode->qtd){
                    novoNode->coordenadasArmazem[0] = armazensShm[k].coordenadas[0];
                    novoNode->coordenadasArmazem[1] = armazensShm[k].coordenadas[1];
                    novoNode->idArmazem = k;
                    printf("armazem escolhido = %d\n", novoNode->idArmazem);
                    novoNode->validade = i;
                    flag = 0;
                    break;
                }
            }
        }
        if(flag == 0)
            break;
    }
}

void printDrones(){
    for(int i=0; i<dados->n_drones; i++){
        printf("Drone: %d\n", arrayDrones[i].id);
        printf("estado: %d\n", arrayDrones[i].estado);
        printf("posX: %f %f\n", arrayDrones[i].posI[0], arrayDrones[i].posI[1]);
        printf("posY: %f %f\n", arrayDrones[i].posF[0], arrayDrones[i].posF[1]);
        printf("\n");
    }
}

//cria qtd drones
void criaDrones(int numI, int qtd){
    int i=0;

    //cria threads
    my_thread = malloc(dados->qtd * sizeof(pthread_t));
    arrayDrones = (Drones*)malloc(sizeof(Drones)*qtd);
    
    for(i=numI; i < qtd; i++) {
        if( i % 4 == 1) {
            arrayDrones[i].posI[0] = (double) 0;
            arrayDrones[i].posI[1] = (double)dados->max_y;
        } else if (i%4 == 2) {
            arrayDrones[i].posI[0] = (double)dados->max_x;
            arrayDrones[i].posI[1] = (double)dados->max_y;
        } else if (i%4 == 3) {
            arrayDrones[i].posI[0] = (double) dados->max_x;
            arrayDrones[i].posI[1] = (double) 0;
        } else if (i%4 == 0) {
            arrayDrones[i].posI[0] = (double) 0;
            arrayDrones[i].posI[1] = (double) 0;
        }
        arrayDrones[i].id = i;
        arrayDrones[i].estado = 1;
        arrayDrones[i].bateria = dados->bInit;
        arrayDrones[i].encomenda_drone = NULL;
    }

    printDrones();

    //long id[qtd+1];
    for(i=numI; i < qtd; i++) {
        //id[i] = i;
        if((pthread_create(&my_thread[i], NULL, controla_drone, &arrayDrones[i].id))!=0){
            perror("Error creating thread\n");
            exit(1);
        }
        printf("\t\t->Thread Drone%d criada no estado %d com bateria %d", arrayDrones[i].id, arrayDrones[i].estado, arrayDrones[i].bateria);
        printf("\tBase x: %0.2f Base Y: %0.2f\n",arrayDrones[i].posI[0],arrayDrones[i].posI[1]);
    }
    sleep(5);
    printf("->Threads Criadas\n");
}

//Carregar bateria dos drones na base
void *baseCharger(){
    while(1){
        for(int i=0; i<dados->n_drones; i++){
            if(arrayDrones[i].estado == 1 && arrayDrones[i].bateria < dados->bMax){
                //se o drone estiver na base e sua bateria for inferior ao maximo
                arrayDrones[i].bateria+=5; //aumenta cinco unidades
                printf("[%d] com bateria %d\n", arrayDrones[i].id, arrayDrones[i].bateria);
            }
        }
        sleep(dados->unidadeT/50); //a cada unidade de tempo
    }
}

void leitura_pipe(){
    while(1){
        int fd_pipe;
        int n_char;
        char buf[128], linha[128];

        pthread_mutex_lock(&drones_mutex);

        if((fd_pipe = open(PIPE_NAME, O_RDONLY)) < 0){
            perror("Erro ao abrir o pipe");
            exit(0);
        }

        n_char = read(fd_pipe, buf, 128);
        buf[n_char-1] = '\0';
        printf("%s", buf);
        fflush(stdout);

        strcpy(linha, buf);
        char *token = strtok(buf, " ");

        if(strcmp(token, "ORDER")==0){
            //Encomenda *novoNode = malloc(sizeof(Encomenda));
            time_t tempo = time(NULL);
            struct tm *t = localtime(&tempo);
            char *str = "Prod_";
            char nome_ordem[20];
            char nome_produto;
            int quantidade, posX, posY;

            sscanf(linha, "ORDER %s Prod: %c, %d to: %d, %d", nome_ordem, &nome_produto, &quantidade, &posX, &posY);
        
            size_t len = strlen(str);
            char *produto = malloc(len + 1 + 1);
            strcpy(produto, str);
            produto[len] = nome_produto;
            produto[len + 1] = '\0';
            int posProd=0;
            while (strcmp(dados->tipos_produtos[posProd], produto) != 0||posProd>9) {
                posProd++;
            }
            printf("\nOpcao: ORDER\n");

            if(strcmp(dados->tipos_produtos[posProd], produto) == 0) {
                //tipo de produto valido
                if(posX <= dados->max_x && posX >= 0 && posY <= dados->max_y && posY >= 0) {
                    //coordenadas tambem validas
                    //guarda as informacoes do pipe no no
                    strcpy(novoNode->nomeEncomenda,nome_ordem);
                    novoNode->qtd = quantidade;
                    novoNode->tipo_produto = produto;
                    novoNode->coordenadas[0] = (double) posX;
                    novoNode->coordenadas[1] = (double) posY;
                    novoNode->nSque = id_encomenda;
                    novoNode->validade = -1;
                    novoNode->id_drone = -1;
                    novoNode->idArmazem = -1;
                    id_encomenda++;

                    //guarda o horario que a encomenda foi criada
                    novoNode->hora = t->tm_hour;
                    novoNode->min = t->tm_min;
                    novoNode->seg = t->tm_sec;

                    sprintf(mensagem, "%d:%d:%d Encomenda %s-%d recebida pela Central\n", novoNode->hora, novoNode->min, novoNode->seg, novoNode->nomeEncomenda, novoNode->nSque);
                    printf("%s", mensagem);

                    sem_wait(sem_write_file);
                    write_log(mensagem);
                    sem_post(sem_write_file);

                    escolheArmazem();

                    if (novoNode->idArmazem != -1) { //ha stock no armazem
                        escolheDrone();
                        if (novoNode->id_drone != -1){//ha drones disponiveis
                            sprintf(mensagem, "%d:%d:%d Encomenda %s-%d enviada ao drone %d\n", t->tm_hour, t->tm_min, t->tm_sec, novoNode->nomeEncomenda, novoNode->nSque, novoNode->id_drone);
                            printf("%s", mensagem);
                            sem_wait(sem_write_file);
                            write_log(mensagem);
                            sem_post(sem_write_file);

                            sem_wait(sem_write_armazens);
                            armazensShm[novoNode->idArmazem].reservados[novoNode->validade].qt = novoNode->qtd;
                            sem_post(sem_write_armazens);

                            pthread_cond_signal(&drones_call_cv);
                        } else { //todos os drones ocupados
                            sprintf(mensagem, "%d:%d:%d Encomenda %s-%d suspensa por falta de drones\n", t->tm_hour, t->tm_min, t->tm_sec, novoNode->nomeEncomenda, novoNode->nSque);
                            
                            printf("%s", mensagem);
                            sem_wait(sem_write_file);
                            write_log(mensagem);
                            sem_post(sem_write_file);

                            sem_wait(sem_write_stats);
                            estatisticas->encomendas_descartadas += 1;
                            sem_post(sem_write_stats);

                        }
                    } else { //nao ha stock em nenhum armazem
                        sprintf(mensagem, "%d:%d:%d Encomenda %s-%d suspensa por falta de stock\n", t->tm_hour, t->tm_min, t->tm_sec, novoNode->nomeEncomenda, novoNode->nSque);

                        printf("%s", mensagem);
                        sem_wait(sem_write_file);
                        write_log(mensagem);
                        sem_post(sem_write_file);

                        sem_wait(sem_write_stats);
                        estatisticas->encomendas_descartadas += 1;
                        sem_post(sem_write_stats);

                    }
                } else { //coordenada invalida
                    sprintf(mensagem, "%d:%d:%d Coordenada invalida: %s \n", t->tm_hour, t->tm_min, t->tm_sec, linha);
                    sem_wait(sem_write_file);
                    write_log(mensagem);
                    sem_post(sem_write_file);
                }
            }else{ //produto invalido
                sprintf(mensagem, "%d:%d:%d Produto invalido: %s \n", t->tm_hour, t->tm_min, t->tm_sec, linha);
                sem_wait(sem_write_file);
                write_log(mensagem);
                sem_post(sem_write_file);
            }

        } else if(strcmp(token, "DRONE")==0){
            printf("\nOpcao: DRONE\n");
            int num;
            sscanf(linha, "DRONE SET %d", &num);
            printf("%d\n", num);

            if(num < dados->n_drones) {
                for(int k= num ; k < dados->n_drones; k++){

                    if (arrayDrones[k].estado ==1 || arrayDrones[k].estado==5){

                        pthread_cancel(my_thread[k]);
                        pthread_join(my_thread[k], NULL);

                        arrayDrones[k].estado = 0;
                        arrayDrones[k].id = -1;
                        arrayDrones[k].posI[0] = 0;
                        arrayDrones[k].posI[1] = 0;
                        arrayDrones[k].posF[0] = 0;
                        arrayDrones[k].posF[1] = 0;

                    } else {

                        pthread_mutex_lock(&mutexes->drones);
                        pthread_cancel(my_thread[k]);
                        pthread_join(my_thread[k], NULL);
                        pthread_mutex_unlock(&mutexes->drones);

                        arrayDrones[k].estado = 0;
                        arrayDrones[k].id = -1;
                        arrayDrones[k].posI[0] = 0;
                        arrayDrones[k].posI[1] = 0;
                        arrayDrones[k].posF[0] = 0;
                        arrayDrones[k].posF[1] = 0;
                    }

                    printf("\nForam destruidas %d threads\n", dados->n_drones - num);
                    dados->n_drones = num;
                }

            } else if (num > dados->n_drones){
                    criaDrones(dados->n_drones,num);
                    dados->n_drones = num;
            } else {
                printf("\nJa estao derteminadas %d threads drones\n", num);
            }
        }else{
            printf("Comando invalido\nTente:\n\tORDER <order name> prod: <product name>, <quantity> to: <x>, <y>\n\tDRONE SET <num>\n");
        }
        pthread_mutex_unlock(&drones_mutex);
    }
}

//Cria named pipe
void cria_named_pipe(){

    if((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0600)<0) && (errno!= EEXIST)){
        perror("Error creating named pipe: ");
        exit(0);
    }

    printf("->Pipe criado\n");

}

//gestao do pipe e dos drones
void central(){
    charger = (pthread_t)malloc(sizeof(pthread_t));
    headListaE = (Encomenda *) malloc(sizeof(Encomenda));
    headListaE->next = NULL;

    //cria as threads
    criaDrones(0, dados->n_drones);

    if((pthread_create(&charger, NULL, baseCharger, NULL))!=0){
        perror("Error creating thread\n");
        exit(1);
    }

    //cria o pipe
    cria_named_pipe();

    leitura_pipe();

    //meta1
    //escolheArmazem();
    //escolheDrone();

    for (int i = 0; i < dados->n_drones; i++) {
        if(pthread_join(my_thread[i], NULL)==0){
            printf("thread [%d] morreu\n", i);
        }
    }
}

//adiciona no ficheiro log a mensagem fornecida
void write_log(char* mensagem) { 
    if(fp_log != NULL){
        fseek(fp_log, 0, SEEK_END);
        fprintf(fp_log,"%s", mensagem);
    }
    mensagem[0]='\0';
}

void destruirShM_stats(){
    if(shmdt(estatisticas)==-1){
        printf("erro shmdt\n");
    }
    if(shmctl(shmid_estats,IPC_RMID, NULL)==-1){
        printf("erro shmctl\n");
    }
    printf("memoria partilhada estats destruida\n");        
}

void destruirShM_ware(){
    if(shmdt(armazensShm)==-1){
        printf("erro shmdt\n");
    }
    if(shmctl(shmid_armazens,IPC_RMID, NULL)==-1){
        printf("erro shmctl\n");
    }
    printf("memoria partilhada armazens destruida\n");      
}

void destruir_threads(){
    for (int i = 0; i < dados->n_drones; i++) {
        if (pthread_join(my_thread[i], NULL) != 0) {
            perror("Error joining thread");
            exit(1);
        }
        free(my_thread);
    }

    if (pthread_join(charger, NULL) != 0) {
        perror("Error joining thread");
        exit(1);
    }
}
//Destruir semaforos
void close_sem(){
    sem_unlink("sem_write_armazens");
    sem_unlink("sem_write_stats");
    sem_unlink("sem_write_file");
    sem_close(sem_write_armazens);
    sem_close(sem_write_stats);
    sem_close(sem_write_file);
}

void sinal_estatistica(int signum){

    printf("\n--------Informação estatistica--------\n");
    printf("Numero total de encomendas entregues = %d\n", estatisticas->encomendas_entregues);
    printf("Numero total de encomendas atribuidas = %d\n", estatisticas->encomendas_atribuidas);
    printf("Numero total de produtos carregados = %d\n", estatisticas->prod_carregados);
    printf("Numero total de produtos entregues = %d\n", estatisticas->prod_entregues);
    printf("Tempo medio = %0.2f\n", estatisticas->tempo_medio_total);
    printf("\n--------------------------------------\n");    
}

void sinal_saida (int sig){
    //CTRL + C
    //tempo
    time_t tempo = time(NULL);
    struct tm*t =localtime(&tempo);

    sem_wait(sem_ctrlC);

    if(getpid() == processo_central){
        /*if(unlink(PIPE_NAME)==0){
            printf("\tpipe fechado\n");
        }*/
        destruir_threads();

        printf("\tcentral terminada\n" );

        //matar processos
        kill(processo_central, SIGKILL);

    } else if(getpid()==processo_gestor) {
        destruirShM_stats();
        destruirShM_ware();

        // Message Queue
        msgctl(mq_id, IPC_RMID, NULL);
        printf("\tmessage queue destruida\n");

        close_sem();
        printf("\tsemaforos destruidos.\n");

        //liberar mallocs
        free(dados);
        printf("\tmallocs libertados.\n");


        sprintf(mensagem, "%d:%d:%d Fim do programa\n",t->tm_hour,t->tm_min,t->tm_sec);
        printf("%s", mensagem);
        sem_wait(sem_write_file);
        write_log(mensagem);
        sem_post(sem_write_file);
        fclose(fp_log);

        //cond
        pthread_cond_destroy(&cond_nao_escolhido);

        printf("\tmutexes destruidos.\n");

        //matar processos
        kill(processo_gestor, SIGKILL);

    } else {    //processo armazem

        for(int i=0; i<dados->numWh; i++){
            sprintf(mensagem, "%d:%d:%d Fim do processo armazem %d\n",t->tm_hour,t->tm_min,t->tm_sec,getpid());
            printf("%s", mensagem);
            sem_wait(sem_write_file);
            write_log(mensagem);
            sem_post(sem_write_file);
            kill(armazensShm[i].pid, SIGKILL);
        }
        sprintf(mensagem, "%d:%d:%d Fim do programa\n",t->tm_hour,t->tm_min,t->tm_sec);
        printf("%s", mensagem);
        sem_wait(sem_write_file);
        write_log(mensagem);
        sem_post(sem_write_file);
    }
}
