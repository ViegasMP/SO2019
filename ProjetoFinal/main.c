/*
    Ana Luisa da Rocha Alves Rainha Coelho - 2015231777
    Maria Paula de Alencar Viegas  -  2017125592
    gcc main.c -lpthread -D_REENTRANT -Wall -o prog estruturas.h  -lm
    echo "ORDER REQ_1 Prod:A, 5 to: 300, 100" >input_pipe

    bateria-distancia
    encomendas descartadas

    usleep?
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

int shm_mutex;
pthread_cond_t cond_nao_escolhido = PTHREAD_COND_INITIALIZER;

// Mutexes
pthread_mutex_t mutex_drones;

// Semáforos
sem_t *sem_write_stats;
sem_t *sem_write_armazens;
sem_t *sem_write_file;


//exemplo encomenda
Encomenda *novoNode;

//funcoes
void generateStock();
void escolheDrone();
void escolheArmazem();
void sinal_estatistica();
void *controla_drone(void *id_ptr);
void write_log(char* mensagem);
void criaArmazens(int n);
void central();
void init_mutex();
void init_sem();
void criaDrones(int numI, int qtd);
void escolhe_armazem(Encomenda *novoNode);
void initShm(Warehouse *arrayArmazens);
void *baseCharger();
void sinal_estatistica();

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

    srand(time(NULL));

    //Encomenda teste para Meta 1
    novoNode = malloc(sizeof(Encomenda));
    novoNode->nomeEncomenda = "Encomenda Teste";
    novoNode->qtd = 5;
    novoNode->tipo_produto = "Prod_A";
    novoNode->coordenadas[0] = (double) 300;
    novoNode->coordenadas[1] = (double) 100;
    novoNode->nSque = id_encomenda;

    //limpa o conteudo existente em log.txt e guarda info do inicio do programa
    processo_gestor = getpid();
    sprintf(mensagem, "%d:%d:%d Inicio do programa [%d]\n",t->tm_hour,t->tm_min,t->tm_sec,getpid());
    printf("%s", mensagem);
    FILE *fpLog = fopen("log.txt","w");
    if(fpLog != NULL){
        fseek(fpLog, 0, SEEK_END);
        fprintf(fpLog,"%s", mensagem);
        fclose(fpLog);
    }
    mensagem[0] = '\0';

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

    //inicializa mutexes e semaforos
    init_mutex();
    init_sem();

    //cria shared mem
    initShm(arrayArmazens);

    novo_processo = processo_gestor;
    novo_processo = fork();
    for (i = 0; i < (dados->numWh + 1); i++) {
        //Cria processo central
        if (i == 0) {
            if (novo_processo == 0) { //guardar na variavel apenas para o processo central
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
        armazensShm[contador].produtos[k].qt+=dados->qtd;
        printf("Armazem %d atualizado: %d produto %s\n", armazensShm[contador].idArmazem, armazensShm[contador].produtos[k].qt, armazensShm[contador].produtos[k].produto);
        contador++;
        if(contador >= dados->numWh) {
            contador = 0;
        }
        sleep(dados->f_abast);
    }
}

// Inicializar mutexes
void init_mutex(){
    time_t tempo = time(NULL);
    struct tm *t = localtime(&tempo);

    if(pthread_mutex_init(&mutex_drones, NULL) != 0) {
        perror("Error - init() of mutexes_drones");
    }

    sprintf(mensagem, "->%d:%d:%d Mutexes criados\n",t->tm_hour,t->tm_min,t->tm_sec);
    printf("%s", mensagem);
    sem_wait(sem_write_file);
    write_log(mensagem);
    sem_post(sem_write_file);
    mensagem[0]='\0';
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
    
    sem_wait(sem_write_armazens);
    armazensShm[n].pid = getpid();
    sem_post(sem_write_armazens);
    
    printf("Armazem %s coordernadas x: %d y: %d, produto : %s qt:%d\n", aux.nome, aux.coordenadas[0],
           aux.coordenadas[1], aux.produtos[0].produto, aux.produtos[0].qt);
    
    sprintf(mensagem, "%d:%d:%d Warehouse%d criada (id = %ld)\n", t->tm_hour, t->tm_min, t->tm_sec, n, (long) getpid());
    sem_wait(sem_write_file);
    write_log(mensagem);
    sem_post(sem_write_file);
    mensagem[0] = '\0';

    fflush(stdout);
    exit(0);
}

//Movimentacao do drone ate armazem
void *controla_drone (void *id) {

    //idDrone vai ter o indice do array de drones para encontrar o id desse drone
    int idDrone = *(int*)id;
    printf("[%d] Sou um drone com id: %d\n", idDrone, arrayDrones[idDrone].id);
    while(1){
        if(arrayDrones[idDrone].encomenda_drone == NULL){
            printf("[%d] Nao tenho nenhuma encomenda :(\n", idDrone);
            sleep(dados->unidadeT);
        } else {
            printf("[%d]Recebi uma encomenda %s\n", arrayDrones[idDrone].id, arrayDrones[idDrone].encomenda_drone->nomeEncomenda);
            printf("DX: %0.2f   DY: %0.2f    AX:  %0.2f      AY:   %0.2f \n", arrayDrones[idDrone].posI[0], arrayDrones[idDrone].posI[1],
                   arrayDrones[idDrone].encomenda_drone->coordernadasArmazem[0], arrayDrones[idDrone].encomenda_drone->coordernadasArmazem[1]);
            while (move_towards(&arrayDrones[idDrone].posI[0], &arrayDrones[idDrone].posI[1],arrayDrones[idDrone].encomenda_drone->coordernadasArmazem[0], arrayDrones[idDrone].encomenda_drone->coordernadasArmazem[1]) >= 0) {
                printf("DRONE %d a deslocar se para Armazem (X: %0.2f   Y: %0.2f)\n ", arrayDrones[idDrone].id,  arrayDrones[idDrone].posI[0],  arrayDrones[idDrone].posI[1]);
                 arrayDrones[idDrone].bateria-=1;
            }
            printf("Chegou no Armazem\n");
            arrayDrones[idDrone].estado = 3;
            arrayDrones[idDrone].encomenda_drone = NULL; //fase teste

        }
        
        sleep(dados->unidadeT);

    }
}

//escolhe o drone para uma encomenda
void escolheDrone(){
    printf("\nSELECIONAR O DRONE PARA A ENCOMENDA FEITA\n");


    printf("Encomenda: %s\n", novoNode->nomeEncomenda);
    printf("prod: %s\n", novoNode->tipo_produto);
    printf("qtd: %d\n", novoNode->qtd);
    printf("posI: %f %f\n", novoNode->coordenadas[0], novoNode->coordenadas[1]);
    printf("posF: %f %f\n", novoNode->coordernadasArmazem[0], novoNode->coordernadasArmazem[1]);

    printf("\n");
    
    int distMin=dados->max_x+dados->max_y;
    int idEscolhido=-1;
    int distancia=0;

    for(int i=0;i<dados->n_drones; i++){
        if(arrayDrones[i].estado == 1 || arrayDrones[i].estado == 5){ //se o drone tiver desocupado
            //printf("drone desocupado\n");
            //calcular distancia
            //printf("distancia = %d\n", distancia);
            distancia = distance(arrayDrones[i].posI[0],arrayDrones[i].posI[1],novoNode->coordernadasArmazem[0],novoNode->coordernadasArmazem[1]);
            //printf("distancia = %d\n", distancia);
            if(distancia < arrayDrones[i].bateria){       
                if(distancia < distMin){
                //se a distancia for menor que a distancia minima atual, a nossa nova distancia minima e essa
                //id do drone escolhido e atualizado
                    distMin=distancia;
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
        sem_wait(sem_write_stats);
        estatisticas->encomendas_atribuidas += 1;
        sem_post(sem_write_stats);

        //apaga encomenda
        novoNode=NULL;
        sinal_estatistica();

    }
}

//escolhe o armazem para uma encomenda
void escolheArmazem(){
    int flag = 1;
    for(int k = 0;k < dados->numWh;k++) {
        for (int i = 0; i < 3; i++) {
            if (strcmp(armazensShm[k].produtos[i].produto,novoNode->tipo_produto) == 0) {
                if(armazensShm[k].produtos[i].qt >= novoNode->qtd){
                    novoNode->coordernadasArmazem[0] = armazensShm[k].coordenadas[0];
                    novoNode->coordernadasArmazem[1] = armazensShm[k].coordenadas[1];
                    novoNode->idArmazem = k;
                    sem_wait(sem_write_armazens);
                    armazensShm[k].produtos[i].qt =  armazensShm[k].produtos[i].qt - novoNode->qtd;
                    sem_post(sem_write_armazens);
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

    //printDrones();

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
    sleep(dados->unidadeT);
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
        sleep(dados->unidadeT); //a cada unidade de tempo
    }
}

//gestao do pipe e dos drones
void central(){
    charger = (pthread_t)malloc(sizeof(pthread_t));
    headListaE = (Encomenda *) malloc(sizeof(Encomenda));
    headListaE->next = NULL;

    criaDrones(0, dados->n_drones);

    if((pthread_create(&charger, NULL, baseCharger, NULL))!=0){
        perror("Error creating thread\n");
        exit(1);
    }
    escolheArmazem();
    escolheDrone();

    for (int i = 0; i < dados->n_drones; i++) {
        if(pthread_join(my_thread[i], NULL)==0){
            printf("thread [%d] morreu\n", i);
        }
    }
}

void destruirShM_estats(){
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

//adiciona no ficheiro log a mensagem fornecida
void write_log(char* mensagem) { 
    FILE *fp = fopen("log.txt","a");
    if(fp != NULL){
        fseek(fp, 0, SEEK_END);
        fprintf(fp,"%s", mensagem);
        fclose(fp);
    }
    mensagem[0]='\0';
}

void sinal_estatistica(){

    printf("\n--------Informação estatistica--------\n");
    printf("Numero total de encomendas entregues = %d\n", estatisticas->encomendas_entregues);
    printf("Numero total de encomendas atribuidas = %d\n", estatisticas->encomendas_atribuidas);
    printf("Numero total de produtos carregados = %d\n", estatisticas->prod_carregados);
    printf("Numero total de produtos entregues = %d\n", estatisticas->prod_entregues);
    printf("Tempo medio = %0.2f\n", estatisticas->tempo_medio_total);
    printf("\n--------------------------------------\n");    
}
