/*
    Ana Luisa da Rocha Alves Rainha Coelho - 2015231777
    Maria Paula de Alencar Viegas  -  2017125592

    ponteiros para char
*/
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "drone_movement.c"

#define MAX_BUFFER  1000

typedef struct produtos {
	char *produto;
	int qt;
}Prod;// Struct usado para guardar as informacoes de um produto de um armazem

typedef struct warehouse {
    char *nome;
    int idArmazem;
    int coordenadas[2];
    Prod produtos[3];
    //struct warehouse *next;
    pid_t pid;
}Warehouse;

typedef struct encomenda { // Struct usado para guardar as informacoes de uma encomenda
    char *nomeEncomenda;
	int id_drone;
    int nSque;//id encomenda
	char *tipo_produto;
	int qtd;
    double coordenadas[2];
	double coordernadasArmazem[2];
	int idArmazem ;
    int hora, min, seg; //
    int validade;//verifica se ha a quantidade de produtos necessarios no armazem
    struct encomenda *next;
}Encomenda;

typedef struct drones { //Struct usado para guardar as informacoes de um drone
    Encomenda *encomenda_drone; //Encomenda atribuida ao drone
	int estado;
    /*
    1 se esta em repouso, 2 se esta em deslocamento para carregamento
    3 se esta em carregamento, 4 se esta em deslocamento para entrega e 5 se esta a retornar a base
    1 -> 2 -> 3 -> 4 -> 5
    */
    int id;
    double posI[2];
	double posF[2];
    int bateria;
}Drones;

typedef struct estatisticas { //Struct usado para guardar as informaçoes gerais do armazem
    int encomendas_entregues;//encomendas entregues
    int encomendas_atribuidas; //encomendas atribuídas a drones
    int encomendas_descartadas;//encomendas descartadas
    int prod_carregados;//produtos carregados de armazéns
    int prod_entregues;//produtos entregues
    float tempo_medio_individual;
    float tempo_medio_total;
}Estats;

typedef struct dados{ //Struct usado para guardar as informaçoes gerais do armazem
    int max_x, max_y, n_drones, f_abast, qtd, unidadeT, numWh, bInit, bMax;
    char *tipos_produtos[MAX_BUFFER];
}Dados;

// Mutexes
typedef struct {    //Struct dos mutexes
    pthread_mutex_t write_file;
    pthread_mutex_t ctrlc;
    pthread_mutex_t write_stats;
    pthread_mutex_t write_armazens;
    pthread_mutex_t drones;
} mutex_struct;


//Struct das mensagens com atualizacao do estoque
typedef struct message{
    int mtype;
    int idArmazem;
    int prod_type;
    char prod_type_name[50];
    int qtd;
    int comentario;
    int idDrone;
}msg;

