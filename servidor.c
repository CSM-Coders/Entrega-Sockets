#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_CLIENTES 50

// --- VARIABLES GLOBALES PARA EL MULTIJUGADOR ---
int clientes_conectados[MAX_CLIENTES];
int num_clientes = 0;
pthread_mutex_t clientes_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Estructura para pasar datos al hilo
typedef struct {
    int socket_cliente;
    struct sockaddr_in direccion_cliente;
    char archivo_logs[256];
} DatosCliente;

// --- FUNCIONES DE RED Y LOGS ---

void registrar_log(const char *archivo, const char *ip, int puerto, const char *mensaje) {
    pthread_mutex_lock(&log_mutex);
    printf("[CLIENTE %s:%d] %s\n", ip, puerto, mensaje);
    FILE *f = fopen(archivo, "a");
    if (f != NULL) {
        fprintf(f, "[CLIENTE %s:%d] %s\n", ip, puerto, mensaje);
        fclose(f);
    }
    pthread_mutex_unlock(&log_mutex);
}

// Agrega un nuevo socket a la lista global
void agregar_cliente(int socket) {
    pthread_mutex_lock(&clientes_mutex);
    if (num_clientes < MAX_CLIENTES) {
        clientes_conectados[num_clientes] = socket;
        num_clientes++;
    }
    pthread_mutex_unlock(&clientes_mutex);
}

// Quita un socket de la lista cuando se desconecta
void remover_cliente(int socket) {
    pthread_mutex_lock(&clientes_mutex);
    for (int i = 0; i < num_clientes; i++) {
        if (clientes_conectados[i] == socket) {
            // Mover el último elemento al espacio vacío para mantener el arreglo sin huecos
            clientes_conectados[i] = clientes_conectados[num_clientes - 1];
            num_clientes--;
            break;
        }
    }
    pthread_mutex_unlock(&clientes_mutex);
}

// Envía un mensaje a TODOS los jugadores excepto al que lo originó
void enviar_broadcast(const char *mensaje, int socket_remitente) {
    pthread_mutex_lock(&clientes_mutex);
    for (int i = 0; i < num_clientes; i++) {
        if (clientes_conectados[i] != socket_remitente) {
            send(clientes_conectados[i], mensaje, strlen(mensaje), 0);
        }
    }
    pthread_mutex_unlock(&clientes_mutex);
}

// --- LÓGICA DE CADA JUGADOR ---

void *manejar_cliente(void *arg) {
    DatosCliente *datos = (DatosCliente *)arg;
    int sock = datos->socket_cliente;
    char ip_cliente[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(datos->direccion_cliente.sin_addr), ip_cliente, INET_ADDRSTRLEN);
    int puerto_cliente = ntohs(datos->direccion_cliente.sin_port);
    char *archivo_logs = datos->archivo_logs;

    // Registrar cliente en la lista global
    agregar_cliente(sock);
    
    // Variables para recordar quién es este jugador (una vez se autentique)
    char nombre_jugador[50] = "Anonimo";

    registrar_log(archivo_logs, ip_cliente, puerto_cliente, "Conexión entrante establecida.");

    char buffer[1024];
    int bytes_leidos;

    while ((bytes_leidos = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_leidos] = '\0'; 
        buffer[strcspn(buffer, "\r\n")] = 0;
        
        if (strlen(buffer) == 0) continue;

        char log_msg[1100];
        snprintf(log_msg, sizeof(log_msg), "Petición recibida: %s", buffer);
        registrar_log(archivo_logs, ip_cliente, puerto_cliente, log_msg);

        char respuesta[512] = "";
        char buffer_copia[1024];
        strcpy(buffer_copia, buffer); 
        
        char *comando = strtok(buffer_copia, " ");
        
        if (comando != NULL) {
            if (strcmp(comando, "AUTH") == 0) {
                char *usuario = strtok(NULL, " ");
                char *password = strtok(NULL, " ");
                if (usuario && password) {
                    strncpy(nombre_jugador, usuario, sizeof(nombre_jugador)-1);
                    snprintf(respuesta, sizeof(respuesta), "OK_AUTH Bienvenido %s\n", usuario);
                } else {
                    snprintf(respuesta, sizeof(respuesta), "ERR_AUTH Faltan credenciales\n");
                }
                send(sock, respuesta, strlen(respuesta), 0);
            } 
            else if (strcmp(comando, "JOIN") == 0) {
                char *sala = strtok(NULL, " ");
                if (sala) {
                    snprintf(respuesta, sizeof(respuesta), "OK_JOIN Te has unido a %s\n", sala);
                } else {
                    snprintf(respuesta, sizeof(respuesta), "ERR_JOIN Falta ID\n");
                }
                send(sock, respuesta, strlen(respuesta), 0);
            }
            else if (strcmp(comando, "MOVE") == 0) {
                char *x_str = strtok(NULL, " ");
                char *y_str = strtok(NULL, " ");
                if (x_str && y_str) {
                    // 1. Le confirmamos al propio jugador su movimiento
                    snprintf(respuesta, sizeof(respuesta), "POS %s %s %s\n", nombre_jugador, x_str, y_str);
                    send(sock, respuesta, strlen(respuesta), 0);
                    
                    // 2. LA MAGIA: Avisamos a TODOS LOS DEMÁS jugadores
                    enviar_broadcast(respuesta, sock);
                } else {
                    snprintf(respuesta, sizeof(respuesta), "ERR_MOVE Faltan coordenadas\n");
                    send(sock, respuesta, strlen(respuesta), 0);
                }
            }
        }
    }

    // Si sale del while, es porque se desconectó
    registrar_log(archivo_logs, ip_cliente, puerto_cliente, "Cliente desconectado.");
    remover_cliente(sock);
    close(sock);
    free(datos);
    return NULL;
}

// ... EL MAIN SIGUE SIENDO EXACTAMENTE IGUAL ...
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int puerto = atoi(argv[1]);
    char *archivo_logs = argv[2];
    int socket_servidor, socket_cliente;
    struct sockaddr_in direccion_servidor, direccion_cliente;
    socklen_t tamano_direccion = sizeof(direccion_cliente);

    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_addr.s_addr = INADDR_ANY;
    direccion_servidor.sin_port = htons(puerto);

    bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor));
    listen(socket_servidor, 10);

    printf("Servidor iniciado en el puerto %d. Guardando logs en %s\n", puerto, archivo_logs);

    while (1) {
        socket_cliente = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &tamano_direccion);
        DatosCliente *datos = malloc(sizeof(DatosCliente));
        datos->socket_cliente = socket_cliente;
        datos->direccion_cliente = direccion_cliente;
        strncpy(datos->archivo_logs, archivo_logs, sizeof(datos->archivo_logs) - 1);

        pthread_t hilo;
        pthread_create(&hilo, NULL, manejar_cliente, (void *)datos);
        pthread_detach(hilo);
    }
    return 0;
}