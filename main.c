#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>

//CONSTANTES
#define NUM_RUEDAS 4
#define TASA_DESCARGA 0.1
#define TASA_CARGA 0.05
#define TIEMPO_REGENERACION 4

//Rueda
typedef struct {
    int id;
    double velocidad_actual;
    char estado[20];
    int activo;
    int accion;
    pid_t pid; 
} Rueda;

//Batería
typedef struct {
    double nivel_carga;
    char estado[20];
} Bateria;

sem_t bat_sem;
sem_t ruedas_sem; // Declaración del semáforo
Rueda ruedas[NUM_RUEDAS];
Bateria bateria;
double velocidad_crucero;
double aceleracion;
char estado[20] = "APAGADO";
char accion[20] = "SIN EFECTO";
int accion_auto;

//-------------------------------------SECCION DE AYUDA------------------------------------------------
//Mostrar ayuda del programa: -h
void mostrar_ayuda() {
    printf("Uso: ./suyay -v <valor_decimal> -a <valor_dcimal>\n");
    printf("Parámetros:\n");
    printf("  -v <x.x>  Valor de velocidad crucero en km/h\n");
    printf("  -a <x.x>  Valor de tasa de aceleracion en km/h2'\n");
    printf("  -h                 Mostrar esta ayuda\n");
}

void limpiar_pantalla() {
    printf("\033[H\033[J");
}
//-----------------------------------------------------------------------------------------------------

//---------------------------------FUNCIONES AUTO----------------------------------------------------
void *acelerar_rueda(void *arg) {

    Rueda *rueda = (Rueda *)arg;
    if (rueda->activo) {
        if(rueda->velocidad_actual<velocidad_crucero){
            rueda->velocidad_actual+=aceleracion;
            if(rueda->velocidad_actual>velocidad_crucero){
                rueda->velocidad_actual = velocidad_crucero;
            }
        }
        //bateria.nivel_carga DEACARGAR BATERIA
    }
    return NULL;
}

void *frenar_rueda(void *arg, int *tiempo_frenado) {
    Rueda *rueda = (Rueda *)arg;
    if (rueda->activo) {
        double frenado = aceleracion * 2;
        int tiempo_frenado = 0;
        if (rueda->velocidad_actual > 0) {
            rueda->velocidad_actual -= frenado;
            if (rueda->velocidad_actual < 0) {
                rueda->velocidad_actual = 0;
            }
            
            // Regenerar la batería durante los primeros 4 segundos
            if (tiempo_frenado < TIEMPO_REGENERACION) {
                sem_wait(&bat_sem);
                bateria.nivel_carga += TASA_CARGA * (rueda->velocidad_actual / velocidad_crucero);
                sem_post(&bat_sem);
                tiempo_frenado++;
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
            acelerar_rueda(&rueda);
        }else if(rueda->accion==2){
            frenar_rueda(&rueda, &tiempo_frenado);
        }
        if(rueda->accion!=2){
            tiempo_frenado=0;
        }    
        if(tiempo_frenado>= TIEMPO_REGENERACION || rueda->accion!=2){
            sem_wait(&bat_sem);
                bateria.nivel_carga -= TASA_DESCARGA*(rueda->velocidad_actual/velocidad_crucero);
            sem_post(&bat_sem);  
        }
        sleep(1); // Simular el tiempo de aceleración
    }
    return NULL;
}

int encender_vehiculo() {
    if(strcmp(estado,"ENCENDIDO")!=0){
        bateria.nivel_carga = 100.0;
        strcpy(bateria.estado, "ESTABLE");
        pid_t cpid;
        for (int i = 0; i < NUM_RUEDAS; i++) {
            ruedas[i].id = i;
            ruedas[i].velocidad_actual = 0.0;
            strcpy(ruedas[i].estado, "SIN EFECTO");
            ruedas[i].activo = 1;
            cpid = fork();
            if(cpid==0){
                inicializar_rueda((void *)&ruedas[i]);
                exit(0);
            }
            else if(cpid>0){
                ruedas[i].pid = cpid;
            }
        }         
            
    }
    return 1;
}

int gestion_auto(){
    int ruedas_activas = 4;
    int status;
    pid_t rueda_pid;
    while (ruedas_activas > 0) {
        if(accion_auto==0){
            for (int i = 0; i < NUM_RUEDAS; i++) {
                if(ruedas[i].activo){
                    rueda_pid = waitpid(ruedas[i].pid, &status, WNOHANG);
                    if (rueda_pid == -1) {
                        perror("waitpid");
                        exit(1);
                    } else if (rueda_pid > 0) {
                        if (WIFEXITED(status)) {
                            sem_wait(&ruedas_sem); 
                            ruedas[i].activo = 0;
                            ruedas[i].accion = 0;
                            ruedas[i].pid = 0;
                            strcpy(ruedas[i].estado,"SIN EFECTO"); 
                            sem_post(&ruedas_sem);                        
                            ruedas_activas--;
                        }
                    }
                }else if(ruedas[i].pid>0){
                    kill(ruedas[i].pid,SIGTERM); 
                    sem_post(&ruedas_sem);               
                    ruedas[i].accion = 0;
                    ruedas[i].pid = 0;
                    strcpy(ruedas[i].estado,"SIN EFECTO");  
                    sem_post(&ruedas_sem);                           
                    ruedas_activas--;         
                }
            }
        }
        else if(accion_auto==1){
            if(encender_vehiculo()){
                strcpy(estado, "ENCENDIDO");
                accion_auto=0;
            }else{
                accion_auto=-1;
                ruedas_activas=0;
            }
        }
    }
    return 0;
}

int apagar_vehiculo() {
    for (int i = 0; i < NUM_RUEDAS; i++) {
        sem_wait(&ruedas_sem);
        ruedas[i].activo = 0;
        sem_post(&ruedas_sem);
    }
    return 1; 
}

//---------------------------------------------------------------------------------------------------------------------

//---------------------------------------------SECCION DE MENU------------------------------------------------------------
// Función para mostrar el menú
void mostrar_menu(int velocidad, char *estado, int bateria, char *accion) {
    limpiar_pantalla();
    printf("Velocidad: %d Km/h,  Estado: %s, Batería: %d%%, Acción vehículo: %s\n", velocidad, estado, bateria, accion);
    printf("Escriba una acción:\n");
    printf("a: acelerar\n");
    printf("f: frenar\n");
    printf("e: encender\n");
    printf("x: apagar\n");
    printf("s: salir\n");
    printf(">> ");
}

int main(int argc, char *argv[]) {
    int opt;
    int valor_v = -1;
    int valor_a = -1;
    pid_t pid;
    sem_init(&bat_sem,0,1);
    sem_init(&ruedas_sem,0,1);

    // Procesar los argumentos de la línea de comandos
    while ((opt = getopt(argc, argv, "v:a:h")) != -1) {
        switch (opt) {
            case 'v':
                valor_v = atoi(optarg);
                break;
            case 'a':
                valor_a = atoi(optarg);
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

    // Mostrar el menú inicial
    mostrar_menu(0, estado, bateria.nivel_carga, accion);

    // Loop para manejar las acciones del usuario
    do {
        opcion = getchar();
        getchar(); // Para capturar el newline

        switch (opcion) {
            case 'a':
                if(bateria.nivel_carga>0){
                    for (int i = 0; i < NUM_RUEDAS; i++) {
                        if(ruedas[i].activo){
                            ruedas[i].accion = 1;
                        }
                    }
                    strcpy(accion, "ACELERANDO");
                }
                break;
            case 'f':
                if(bateria.nivel_carga>0){
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
                    accion_auto = 1;
                    pid = fork();
                    if(pid==0){
                        exit(gestion_auto());
                    }else if(pid>0){
                        sleep(1.5);
                    }
                }
                break;
            case 'x':
                if(apagar_vehiculo()){
                    strcpy(estado, "APAGADO");
                    strcpy(accion, "SIN EFECTO");
                }
                break;
            case 's':
                printf("Saliendo...\n");
                return 0;
            default:
                printf("Opción no válida\n");
                break;
        }

        // Volver a mostrar el menú después de cada acción
        mostrar_menu(0, estado, bateria.nivel_carga, accion);
    } while (strcmp(estado,"APAGADO")!=0);

    
    sem_destroy(&bat_sem);
    sem_destroy(&ruedas_sem);
    return 0;
}

//---------------------------------------------------------------------------------------------------------------------------
