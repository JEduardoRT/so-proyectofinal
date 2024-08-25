#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>

//CONSTANTES
#define NUM_RUEDAS 4
#define TASA_DESCARGA 0.1
#define TASA_CARGA 0.05
#define TIEMPO_REGENERACION 4

//Rueda
typedef struct {
    int id;
    float velocidad_actual;
    char estado[20];
    int activo;
    int accion;
    pthread_t thread_id; 
} Rueda;

//Batería
typedef struct {
    float nivel_carga;
    char estado[20];
} Bateria;

sem_t bat_sem;
sem_t ruedas_sem;
Rueda *ruedas;
Bateria *bateria;
float velocidad_crucero;
float aceleracion;
char *estado;
char *accion;
int *accion_auto;
pthread_mutex_t console_lock;
int run_update = 1; // Bandera para detener la actualización

int shm_id_ruedas;
int shm_id_bateria;
int shm_id_estado;
int shm_id_accion;
int shm_id_accion_auto;

//-------------------------------------SECCION DE AYUDA------------------------------------------------
//Mostrar ayuda del programa: -h
void mostrar_ayuda() {
    printf("Uso: ./suyay -v <valor_decimal> -a <valor_dcimal>\n");
    printf("Parámetros:\n");
    printf("  -v <x.x>  Valor de velocidad crucero en km/h\n");
    printf("  -a <x.x>  Valor de tasa de aceleracion en km/h^2'\n");
    printf("  -h                 Mostrar esta ayuda\n");
}

void limpiar_pantalla() {
    printf("\033[H\033[J");
}
//-----------------------------------------------------------------------------------------------------

//---------------------------------FUNCIONES AUTO----------------------------------------------------
float obtener_velocidad(Rueda *ruedas){
    float velocidad_final=0.f;
    int resta = 0;
    for (int i = 0; i < NUM_RUEDAS; i++) {
        if(!strcmp(ruedas[i].estado,"SIN EFECTO")){
            velocidad_final += ruedas[i].velocidad_actual;
        }else{
            resta++;
        }
    }
    if(resta>=NUM_RUEDAS){
        return 0.f;
    }
    return velocidad_final/(NUM_RUEDAS-resta);
}

void *acelerar_rueda(void *arg) {

    Rueda *rueda = (Rueda *)arg;
    if (rueda->activo) {
        if(rueda->velocidad_actual<velocidad_crucero){
            rueda->velocidad_actual+=aceleracion;
            if(rueda->velocidad_actual>velocidad_crucero){
                rueda->velocidad_actual = velocidad_crucero;
            }
        }
    }
    return NULL;
}

void *frenar_rueda(void *arg, int *tiempo_frenado) {
    Rueda *rueda = (Rueda *)arg;
    if (rueda->activo) {
        float frenado = aceleracion * 2.f;
        if (rueda->velocidad_actual > 0) {
            rueda->velocidad_actual -= frenado;
            if (rueda->velocidad_actual < 0) {
                rueda->velocidad_actual = 0.f;
            }
            
            // Regenerar la batería durante los primeros 4 segundos
            if (*tiempo_frenado < TIEMPO_REGENERACION) {
                sem_wait(&bat_sem);
                bateria->nivel_carga += TASA_CARGA * (rueda->velocidad_actual / velocidad_crucero);
                if(bateria->nivel_carga>100){
                    bateria->nivel_carga=100;
                }
                sem_post(&bat_sem);
                (*tiempo_frenado)++;
            }
        }
    }
    return NULL;
}

void *inicializar_rueda(void *arg) {

    Rueda *rueda = (Rueda *)arg;
    int tiempo_frenado = 0;
    while (rueda->activo) {
        if(rueda->accion==1){
            acelerar_rueda(rueda);
        }else if(rueda->accion==2){
            frenar_rueda(rueda, &tiempo_frenado);
        }
        if(rueda->accion!=2){
            tiempo_frenado=0;
        }    
        if(tiempo_frenado>= TIEMPO_REGENERACION || rueda->accion!=2){
            sem_wait(&bat_sem);
                bateria->nivel_carga -= TASA_DESCARGA*(rueda->velocidad_actual/velocidad_crucero);
                if(bateria->nivel_carga<0){
                    bateria->nivel_carga=0;
                }
            sem_post(&bat_sem);  
        }
        sleep(1); // Simular el tiempo de aceleración
    }
    return NULL;
}

int encender_vehiculo() {
    if(strcmp(estado,"ENCENDIDO")!=0){
        bateria->nivel_carga = 100.0;
        strcpy(bateria->estado, "ESTABLE");
        for (int i = 0; i < NUM_RUEDAS; i++) {
            ruedas[i].id = i;
            ruedas[i].velocidad_actual = 0.f;
            strcpy(ruedas[i].estado, "SIN EFECTO");
            ruedas[i].activo = 1;
            pthread_create(&ruedas[i].thread_id, NULL, inicializar_rueda, (void *)&ruedas[i]);
        }         
            
    }
    return 1;
}

// Mueve el cursor a una posición específica en la consola
void mover_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

// Actualiza los valores en la consola sin refrescar toda la pantalla
void *actualizar_pantalla(void *arg) {
    while(run_update){
        mover_cursor(1, 1);  // Mueve el cursor a la posición donde están los valores
        printf("Velocidad: %.2f Km/h,  Estado: %s, Batería: %.2f%%, Acción vehículo: %s\n", obtener_velocidad(ruedas), estado, bateria->nivel_carga, accion);    
        mover_cursor(8, 4);
        fflush(stdout); // Asegura que la salida se muestre inmediatamente
        sleep(0.1);
    }
}

// Función para mostrar el menú
void mostrar_menu(float velocidad, char *estado, float bateria, char *accion) {
    limpiar_pantalla();
    printf("Velocidad: %.2f Km/h,  Estado: %s, Batería: %.2f%%, Acción vehículo: %s\nEscriba una acción:\na: acelerar\nf: frenar\ne: encender\nx: apagar\ns: salir\n>> ", velocidad, estado, bateria, accion);
}

int apagar_vehiculo() {
    for (int i = 0; i < NUM_RUEDAS; i++) {
        sem_wait(&ruedas_sem);
        ruedas[i].activo = 0;
        sem_post(&ruedas_sem);
    }
    return 1; 
}

int gestion_auto(){
    int ruedas_activas = 4;
    int status;
    pid_t rueda_pid;
    while (ruedas_activas > 0) {
        if(*accion_auto==0){
            if(bateria->nivel_carga<=0){
                if(apagar_vehiculo()){
                    strcpy(estado, "APAGADO");
                    strcpy(accion, "SIN EFECTO");
                }
            }
            for (int i = 0; i < NUM_RUEDAS; i++) {
                if (ruedas[i].activo) {
                    int res = pthread_kill(ruedas[i].thread_id, 0);
                    if (res != 0) { // Si el hilo ha terminado o ha sido terminado
                        sem_wait(&ruedas_sem); 
                        ruedas[i].activo = 0;
                        ruedas[i].accion = 0;
                        strcpy(ruedas[i].estado,"SIN EFECTO");
                        ruedas[i].thread_id = 0; 
                        sem_post(&ruedas_sem);
                        ruedas_activas--;
                    }
                }else if(ruedas[i].thread_id>0){
                    pthread_join(ruedas[i].thread_id,NULL);
                    sem_wait(&ruedas_sem);               
                    ruedas[i].accion = 0;
                    ruedas[i].thread_id = 0;
                    strcpy(ruedas[i].estado,"SIN EFECTO");  
                    sem_post(&ruedas_sem);                           
                    ruedas_activas--;         
                }
            }
        }
        else if(*accion_auto==1){
            if(encender_vehiculo()){
                strcpy(estado, "ENCENDIDO");
                *accion_auto=0;
            }else{
                *accion_auto=-1;
                ruedas_activas=0;
            }
        }
        sleep(0.2);
    }
    return 0;
}


//---------------------------------------------------------------------------------------------------------------------

//---------------------------------------------SECCION DE MENU------------------------------------------------------------


int main(int argc, char *argv[]) {
    int opt;
    float valor_v = -1;
    float valor_a = -1;
    pthread_t gestion_id;
    sem_init(&bat_sem,1,1);
    sem_init(&ruedas_sem,1,1);

    // Procesar los argumentos de la línea de comandos
    while ((opt = getopt(argc, argv, "v:a:h")) != -1) {
        switch (opt) {
            case 'v':
                valor_v = atof(optarg);
                break;
            case 'a':
                valor_a = atof(optarg);
                break;
            case 'h':
                mostrar_ayuda();
                return 0;
            default:
                mostrar_ayuda();
                return 1;
        }
    }

    // Verificar que los parámetros obligatorios han sido proporcionados
    if (valor_v == -1 || valor_a == -1) {
        mostrar_ayuda();
        return 1;
    }

    velocidad_crucero = valor_v;
    aceleracion = valor_a;
    char opcion;

    shm_id_ruedas = shmget(IPC_PRIVATE, NUM_RUEDAS*sizeof(Rueda), IPC_CREAT | 0666);
    if (shm_id_ruedas == -1) {
        perror("shmget");
        exit(1);
    }
    // Asociar el segmento de memoria compartida
    ruedas = (Rueda *)shmat(shm_id_ruedas, NULL, 0);
    if (ruedas == (void *)-1) {
        perror("shmat");
        exit(1);
    }
    shm_id_bateria = shmget(IPC_PRIVATE, sizeof(Bateria), IPC_CREAT | 0666);
    if (shm_id_bateria == -1) {
        perror("shmget");
        exit(1);
    }
    // Asociar el segmento de memoria compartida
    bateria = (Bateria *)shmat(shm_id_bateria, NULL, 0);
    if (bateria == (void *)-1) {
        perror("shmat");
        exit(1);
    }
    shm_id_estado = shmget(IPC_PRIVATE, 20*sizeof(char), IPC_CREAT | 0666);
    if (shm_id_estado == -1) {
        perror("shmget");
        exit(1);
    }
    // Asociar el segmento de memoria compartida
    estado = (char *)shmat(shm_id_estado, NULL, 0);
    if (estado == (void *)-1) {
        perror("shmat");
        exit(1);
    }
    shm_id_accion = shmget(IPC_PRIVATE, 20*sizeof(char), IPC_CREAT | 0666);
    if (shm_id_accion == -1) {
        perror("shmget");
        exit(1);
    }
    // Asociar el segmento de memoria compartida
    accion = (char *)shmat(shm_id_accion, NULL, 0);
    if (accion == (void *)-1) {
        perror("shmat");
        exit(1);
    }
    shm_id_accion_auto = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    if (shm_id_accion_auto == -1) {
        perror("shmget");
        exit(1);
    }
    // Asociar el segmento de memoria compartida
    accion_auto = (int *)shmat(shm_id_accion_auto, NULL, 0);
    if (accion_auto == (void *)-1) {
        perror("shmat");
        exit(1);
    }
    
    strcpy(estado,"APAGADO");
    strcpy(accion,"SIN EFECTO");
    // Mostrar el menú inicial
    mostrar_menu(0, estado, bateria->nivel_carga, accion);
    
    pthread_t update_thread;

    pthread_mutex_init(&console_lock, NULL);
    pthread_create(&update_thread, NULL, actualizar_pantalla, NULL);

    // Loop para manejar las acciones del usuario
    do {
        pthread_mutex_lock(&console_lock);
        opcion = getchar();
        getchar(); // Capturar el newline
        pthread_mutex_unlock(&console_lock);

        switch (opcion) {
            case 'a':
                if(bateria->nivel_carga>0){
                    for (int i = 0; i < NUM_RUEDAS; i++) {
                        if(ruedas[i].activo){
                            ruedas[i].accion = 1;
                        }
                    }
                    strcpy(accion, "ACELERANDO");
                }
                break;
            case 'f':
                if(bateria->nivel_carga>0){
                    for (int i = 0; i < NUM_RUEDAS; i++) {
                        if(ruedas[i].activo){
                            ruedas[i].accion = 2;
                        }
                    }
                    strcpy(accion, "FRENANDO");
                }
                break;
            case 'e':
                if(strcmp(estado,"ENCENDIDO")!=0){
                    *accion_auto = 1;
                    pthread_create(&gestion_id, NULL, (void *)gestion_auto, NULL);
                    sleep(1);
                    pthread_detach(gestion_id);
                }
                break;
            case 'x':
                if(apagar_vehiculo()){
                    strcpy(estado, "APAGADO");
                    strcpy(accion, "SIN EFECTO");
                    run_update = 0;
                    mostrar_menu(obtener_velocidad(ruedas), estado, bateria->nivel_carga, accion);
                }
                break;
            case 's':
                printf("Saliendo...\n");
                run_update = 0;
                return 0;
            default:
                printf("Opción no válida\n");
                break;
        }

    } while (strcmp(estado,"APAGADO")!=0);

    
    sem_destroy(&bat_sem);
    sem_destroy(&ruedas_sem);
    pthread_join(update_thread, NULL);
    pthread_mutex_destroy(&console_lock);
    return 0;
}

//---------------------------------------------------------------------------------------------------------------------------
