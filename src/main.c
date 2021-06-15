// gcc main.c -pthread -o prog (to compile)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>

#define MAXSIZE 10
#define MEMORY_SHM 3300
#define KEY 5678
#define QTD_PROCESS 7
#define T1_P4 1
#define T2_P4 2
#define P7_THREADS 3
#define END_PROG 10000
#define P5_TURN 0
#define P6_TURN 1
#define P7_TURN 2

//Estrutura das Filas
struct queue{
  int front,pos;
  int size;
  int rand_numbers[MAXSIZE];
};
typedef struct queue FIFO;

//Funções Fila
void queue_create(FIFO *fi);
int queue_push(FIFO *fi, int value);
int queue_pop(FIFO *fi);
int queue_getfirst(FIFO *fi);
int queue_isfull(FIFO *fi);
int queue_isempty(FIFO *fi);
int queue_size(FIFO *fi);


//Estrutura dos dados compartilhados
struct shared_memo{
  FIFO f1, f2;
  int *pipe1, *pipe2;
  int writeAcess, readAcess;
  sem_t mutex_producers, mutex_threadsp4;
  int pid4, turn;
  int p5_processed , p6_processed ;
};
struct shared_memo *shm;//Ponteiro para shared memory

int last_Turn = P5_TURN;

//Estrutura dos dados para Relatório
struct all_records{
  int count, maior, menor, moda;
  int numbers[END_PROG];
  clock_t timer;
};

//Tratador(Handle) of SIGUSR1
void handler_signal(int sig);

//Process Functions
void *consumeF2(void *args);
void produce_to_F1();
void task_P4();
void *get_F1_toPipes(void *args);
void get_pipe1_toF2();
void get_pipe2_toF2();
void get_F2_toPrint(struct all_records all_records);
void create_report(struct all_records all_records);


int main(int argc, char* argv[]) {
  int pid[7], status[7], id;
  struct all_records all_records;

//////////////// Criando shared memory do programa //////////////// 
  int shmid;
  key_t key = KEY;

  void *shared_memory = (void *)0;
  all_records.timer = clock();

  shmid = shmget(key, MEMORY_SHM, 0666 | IPC_CREAT);
  if (shmid == -1)
    exit(-1);

  shared_memory = shmat(shmid, (void *)0, 0);
  if (shared_memory == (void *)-1)
    exit(-1);

  shm = (struct shared_memo *)shared_memory;


//////////////// Instanciando os Pipes //////////////////////

  int pipe1[2];
  if (pipe(pipe1) == -1) return -1;

  int pipe2[2];
  if (pipe(pipe2) == -1) return -1;


/////// Inicializando semaphore e Dados Shared Memory ///////

  if (sem_init((sem_t *)&shm->mutex_producers, 1, 0) != 0)
    exit(-1);
  //instancia pipes
  shm->pipe1 = pipe1;
  shm->pipe2 = pipe2;
  shm->writeAcess = 1;//há acesso para escrita
  shm->readAcess = 0;//não há acesso para leitura(fila vazia)
  shm->turn = -2;
  //contagens dados passados p5 e p6
  shm->p5_processed  = 0;
  shm->p6_processed  = 0;
  //verificador de qtd de dados processados
  all_records.count = 0;
  //cria filas fifo da Shm
  queue_create(&shm->f1);
  queue_create(&shm->f2);

/////////////////// Processos //////////////////////////////
  for (int i = 0; i < QTD_PROCESS; i++) {
    id = fork();
    if (id > 0) { //Father
      pid[i] = id;
    }
    else {
      if (i == 3) { //Processo 4
        signal(SIGUSR1, handler_signal);//ao receber o SIGUSR1 usa o handler como tratamento
        task_P4();
        while (1){}
      } else if (i < 3 && i >= 0){//Processos produtores p1,p2 e p3
         produce_to_F1();
      }else {
         if (i == 4) { // Processo 5
           get_pipe1_toF2();
        }if (i == 5) { // Processo 6
           get_pipe2_toF2();
        } if (i == 6) { // Processo 7
           get_F2_toPrint(all_records);
        }
      }
      break;
    }
 }
 
  if (id > 0) { //Father
    int status;

    //permite que os processos produtores em espera sejam desbloqueados
    sem_post(&shm->mutex_producers);
    sleep(1);

    //inicializa P5
    shm->turn = P5_TURN;
    //espera contexto de P6 chegue ao pai e finaliza até p6
    waitpid(pid[6], &status, WUNTRACED);
    for (int i = 0; i < 6; i++){
      kill(pid[i], SIGKILL);
    }
    close(shm->pipe1[1]);
    close(shm->pipe2[1]);

    return 0;
  }
}


// >>>>>>>>>>>>>>>>>>>  THREADS  AND TREATS  <<<<<<<<<<<<<<<<<<<<<<<<

void handler_signal(int sig) {//altera o acesso para dados disponíveis para retirada
  shm->readAcess = 1;
}

void produce_to_F1() {
  while (1) {
    sem_wait((sem_t *)&shm->mutex_producers);
    if (shm->writeAcess) {//verifica se não há nenhum processo em escrita
      if (!queue_isfull(&shm->f1)) {
        /*
        unsigned int myseed = time(NULL)^getpid()^pthread_self();
        int rand = rand_r(&myseed)%1000+1;
        enqueue(&shm->f1, n_rand);
        */
        int n_rand = rand() % 1000 + 1;
        queue_push(&shm->f1, n_rand);
       
        if (queue_isfull(&shm->f1)) {
          shm->writeAcess = 0;
          kill(shm->pid4, SIGUSR1);//Se fila estiver cheia, envia sinal p/P4
        }
      }
    }
    sem_post((sem_t *)&shm->mutex_producers);
  }
}

void *get_F1_toPipes(void *args) {
  int *atual = (int *) args;

  while (1) {
    sem_wait((sem_t *)&shm->mutex_threadsp4);
    if (shm->readAcess) {//verifica se os dados estão disponíveis para leitura(1)
      if (queue_isempty(&shm->f1)) {
        shm->readAcess = 0;
        shm->writeAcess = 1;
        sem_post((sem_t *)&shm->mutex_threadsp4);
      }else{
        int num_fi = queue_getfirst(&shm->f1);
        queue_pop(&shm->f1);

        if (*atual == T1_P4){//verifica qual processo é o atual e coloca no pipe respectivo
          write(shm->pipe1[1], &num_fi, sizeof(int));
        }else{
          write(shm->pipe2[1], &num_fi, sizeof(int));
        }
      }
    }
    sem_post((sem_t *)&shm->mutex_threadsp4);
  }
}

void task_P4() {
  shm->pid4 = getpid();
  pthread_t p4_thread02;
  int p4T1 = T1_P4;
  int p4T2 = T2_P4;

  if (sem_init((sem_t *)&shm->mutex_threadsp4, 0, 1) != 0)
    exit(-1);

  pthread_create(&p4_thread02, NULL, get_F1_toPipes, &p4T1); 
  get_F1_toPipes(&p4T2);
}

void get_pipe1_toF2() {
  while (1) {
    if(shm->turn == P5_TURN){
      if (queue_isfull(&shm->f2)){
        shm->turn = P7_TURN;
      }else{
        int value = 0;

        if(read(shm->pipe1[0], &value, sizeof(int)) > -1){
            queue_push(&shm->f2, value);
            shm->p5_processed ++;
            shm->turn = P7_TURN;
        }else{
            shm->turn = P6_TURN;
        }
      }
    }
  }
}

void get_pipe2_toF2() {
    while (1) {
      if(shm->turn == P6_TURN){
        if(queue_isfull(&shm->f2)) {
          shm->turn = P7_TURN;
        }else{
          int value = 0;
          
          if(read(shm->pipe2[0], &value, sizeof(int)) > -1){
              queue_push(&shm->f2, value);
              shm->p6_processed ++;

              shm->turn = P7_TURN;
          }else{
              shm->turn = P5_TURN;
          }
        }
    }
  }
}

void *consumeF2(void *args) {
  struct all_records *all_records = (struct all_records *) args;

  while (1) {
   if (shm->turn == P7_TURN){
        if (all_records->count == END_PROG){
          break;
        }
      
        if(!queue_isempty(&shm->f2)) {
          int value = queue_getfirst(&shm->f2);
          
          queue_pop(&shm->f2);

            if(value >= 1){
              if (all_records->count == 0) {
                  all_records->menor = value;
                  all_records->maior = value;
              }else{
                  if (all_records->menor > value){
                      all_records->menor = value;
                  } if (value > all_records->maior){
                      all_records->maior = value;
                  }
              }

            all_records->numbers[all_records->count] = value;
            all_records->count++;
            printf("from P7-->[%d]\n", value);
          }
        }
        if (last_Turn == P5_TURN) shm->turn = P6_TURN;
        else shm->turn = P5_TURN;
        last_Turn = shm->turn;
    }
  }
}

void get_F2_toPrint(struct all_records all_records) {
  pthread_t t[P7_THREADS];

  for (int i = 0; i < P7_THREADS -1; i++) {
    if (pthread_create(&t[i], NULL,
          consumeF2, &all_records))
      exit(-1);
  }
  consumeF2(&all_records);

  //Após consumo de F2 pelas Threads gera o relatório
  create_report(all_records);

  exit(0);//Finaliza P7
}

void create_report(struct all_records all_records){
  
  int c = 1, tempCount;
  int temp = 0,i = 0,j = 0;
  int popular = all_records.numbers[0];
  
  for (i = 0; i < (all_records.count - 1); i++)
    {
        temp = all_records.numbers[i];
        tempCount = 0;
        for (j = 1; j < all_records.count; j++)
        {
            if (temp == all_records.numbers[j])
                tempCount++;
        }
        if (tempCount > c)
        {
            popular = temp;
            c = tempCount;
        }
    }

  all_records.timer = clock() - all_records.timer; 

  printf("\n>>>>>>>>>>>>>>>>>>> Relatório <<<<<<<<<<<<<<<<<<<\n\n");
  printf("> Total de tempo de execução: %.3lf segundos\n", ((double)all_records.timer) / ((CLOCKS_PER_SEC)));
  printf("> Maior valor ------%d\n", all_records.maior);
  printf("> Menor valor ------%d\n", all_records.menor);
  printf("> Moda -------------%d\n",popular);
  printf("> Processados em P5-%d e P6-%d\n", shm->p5_processed , shm->p6_processed );
  printf("*************************************************\n\n");
}

// >>>>>>>>>>>>>>>>>>> QUEUE FIFO <<<<<<<<<<<<<<<<<<<<<<<<<


void queue_create(FIFO *fi) {
    fi->front = 0;
    fi->pos = MAXSIZE - 1;
    fi->size = 0;
}

int queue_push(FIFO *fi, int value) {//enqueue
    if (queue_isfull(fi))
        return 1;//está cheia

    fi->pos = (fi->pos + 1) % MAXSIZE;
    fi->rand_numbers[fi->pos] = value;
    fi->size++;

    return 2;//está vazia 
}

int queue_getfirst(FIFO *fi) {
    if (queue_isempty(fi))
        return -2;

    int val = fi->rand_numbers[fi->front];
    return val;
}

int queue_pop(FIFO *fi) {
    if (queue_isempty(fi))
        return -2;

    fi->front = (fi->front + 1) % MAXSIZE;
    fi->size--;
}

int queue_isfull(FIFO *fi) {
    return fi->size == MAXSIZE;
}

int queue_isempty(FIFO *fi) {
    return fi->size == 0;
}

int queue_size(FIFO *fi){
    return fi->size;
}