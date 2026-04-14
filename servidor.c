/*
 * servidor.c — Servidor maestro del juego de ciberseguridad
 * Protocolo CSGS (CyberSecurity Game Server Protocol)
 * Compilar: gcc -Wall -pthread -o servidor servidor.c
 * Uso:      ./servidor <puerto> <archivoDeLogs>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>

/* ─── Constantes del juego ─────────────────────────────────────── */
#define MAX_CLIENTES     50
#define MAX_SALAS        10
#define MAX_J_SALA       10
#define ANCHO_MAPA       400
#define ALTO_MAPA        300
#define RADIO_DETECCION  40   /* px para detectar recurso crítico   */
#define NUM_RECURSOS     2
#define LIMITE_MITIGACION_SEG 10
#define IDENTITY_HOST_DEFAULT    "localhost"
#define IDENTITY_HOST_FALLBACK   "host.docker.internal"
#define IDENTITY_PORT_DEFAULT    "9090"
#define IDENTITY_CONNECT_TIMEOUT_MS 400
#define IDENTITY_IO_TIMEOUT_MS      700

/* ─── Estructuras ───────────────────────────────────────────────── */

typedef struct {
    int  id;
    int  x, y;
    int  bajo_ataque;   /* 0 = normal | 1 = atacado */
    int  mitigado;      /* 0 = no     | 1 = mitigado */
    time_t ataque_ts;   /* instante de inicio del ataque */
    char atacante[64];
    char nombre[32];
} Recurso;

typedef struct {
    int  socket;
    char nombre[64];
    char rol[20];           /* "ATACANTE" | "DEFENSOR" */
    char ip[INET_ADDRSTRLEN];
    int  puerto;
    int  pos_x, pos_y;
    int  sala_id;           /* -1 = sin sala */
    int  activo;
} Jugador;

typedef struct {
    int      id;
    int      activa;
    int      iniciada;
    int      estado;       /* 0=WAITING | 1=IN_GAME | 2=FINISHED */
    Jugador *jugadores[MAX_J_SALA];
    int      num_jugadores;
    Recurso  recursos[NUM_RECURSOS];
} Sala;

/* ─── Estado global ─────────────────────────────────────────────── */
static Jugador jugadores[MAX_CLIENTES];
static int     num_jugadores = 0;
static Sala    salas[MAX_SALAS];
static int     num_salas     = 0;

static pthread_mutex_t mtx_jugadores = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_salas     = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_log       = PTHREAD_MUTEX_INITIALIZER;

static char archivo_logs[256];

/* forward declaration */
static void broadcast_sala(int sala_id, const char *msg, int excepto_sock);
static void enviar_recursos_a_defensor(int sock, int sala_id);

/* ─── Nombres de recursos ──────────────────────────────────── */
static const char *RECURSOS_NOMBRE[NUM_RECURSOS] = {"Servidor_A", "Servidor_B"};

static void enviar_recursos_a_defensor(int sock, int sala_id)
{
    pthread_mutex_lock(&mtx_salas);
    if (sala_id < 0 || sala_id >= num_salas) {
        pthread_mutex_unlock(&mtx_salas);
        return;
    }

    Sala *s = &salas[sala_id];
    for (int r = 0; r < NUM_RECURSOS; r++) {
        char rm[128];
        snprintf(rm, sizeof(rm),
                 "RESOURCE_INFO %d %s %d %d\n",
                 s->recursos[r].id,
                 s->recursos[r].nombre,
                 s->recursos[r].x,
                 s->recursos[r].y);
        send(sock, rm, strlen(rm), 0);
    }

    pthread_mutex_unlock(&mtx_salas);
}

/* ═══════════════════════════════════════════════════════════════════
 *  LOGGING
 * ═══════════════════════════════════════════════════════════════════ */
static void log_msg(const char *ip, int puerto, const char *msg)
{
    pthread_mutex_lock(&mtx_log);
    printf("[%s:%d] %s\n", ip, puerto, msg);
    FILE *f = fopen(archivo_logs, "a");
    if (f) { fprintf(f, "[%s:%d] %s\n", ip, puerto, msg); fclose(f); }
    pthread_mutex_unlock(&mtx_log);
}

/* Parseo estricto de enteros para evitar que "abc" termine como 0 vía atoi */
static int parse_int_strict(const char *s, int *out)
{
    if (!s || !*s) return 0;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    if (v < INT_MIN || v > INT_MAX) return 0;
    *out = (int)v;
    return 1;
}

/* Respuesta de error estándar conservando prefijo ERR_* para compatibilidad */
static void responder_error(int sock, char *resp, size_t resp_sz,
                            const char *prefijo_err,
                            const char *detalle,
                            const char *codigo)
{
    snprintf(resp, resp_sz, "%s %s [CODE=%s]\n", prefijo_err, detalle, codigo);
    send(sock, resp, strlen(resp), 0);
}

/* ═══════════════════════════════════════════════════════════════════
 *  ESTADO DE SALA (WAITING / IN_GAME / FINISHED)
 * ═══════════════════════════════════════════════════════════════════ */
static const char *estado_sala_str(int estado)
{
    if (estado == 1) return "IN_GAME";
    if (estado == 2) return "FINISHED";
    return "WAITING";
}

static void evaluar_estado_inicio_sala(int sala_id)
{
    char evento[128] = "";
    int  enviar = 0;

    pthread_mutex_lock(&mtx_salas);
    if (sala_id < 0 || sala_id >= num_salas) {
        pthread_mutex_unlock(&mtx_salas);
        return;
    }

    Sala *s = &salas[sala_id];
    if (!s->activa || s->estado == 2) {
        pthread_mutex_unlock(&mtx_salas);
        return;
    }

    int atacantes = 0, defensores = 0;
    for (int i = 0; i < s->num_jugadores; i++) {
        Jugador *j = s->jugadores[i];
        if (!j || !j->activo) continue;
        if (strcmp(j->rol, "ATACANTE") == 0) atacantes++;
        else if (strcmp(j->rol, "DEFENSOR") == 0) defensores++;
    }

    if (atacantes >= 1 && defensores >= 1) {
        if (s->estado != 1) {
            s->estado   = 1;
            s->iniciada = 1;
            snprintf(evento, sizeof(evento),
                     "EVENT GAME_START %d %d %d\n",
                     s->id, atacantes, defensores);
            enviar = 1;
        }
    } else {
        if (s->estado == 1) {
            s->estado   = 0;
            s->iniciada = 0;
            snprintf(evento, sizeof(evento),
                     "EVENT GAME_WAITING %d %d %d\n",
                     s->id, atacantes, defensores);
            enviar = 1;
        }
    }
    pthread_mutex_unlock(&mtx_salas);

    if (enviar) broadcast_sala(sala_id, evento, -1);
}

static void evaluar_fin_sala(int sala_id)
{
    char evento[128] = "";
    int  enviar = 0;

    pthread_mutex_lock(&mtx_salas);
    if (sala_id < 0 || sala_id >= num_salas) {
        pthread_mutex_unlock(&mtx_salas);
        return;
    }

    Sala *s = &salas[sala_id];
    if (!s->activa || s->estado != 1) {
        pthread_mutex_unlock(&mtx_salas);
        return;
    }

    int todos_mitigados = 1;
    for (int r = 0; r < NUM_RECURSOS; r++) {
        if (!s->recursos[r].mitigado) todos_mitigados = 0;
    }

    if (todos_mitigados) {
        s->estado = 2;
        s->iniciada = 0;
        snprintf(evento, sizeof(evento),
                 "EVENT GAME_END %d DEFENSORES\n", s->id);
        enviar = 1;
    }
    pthread_mutex_unlock(&mtx_salas);

    if (enviar) broadcast_sala(sala_id, evento, -1);
}

/* Revisa timeouts de mitigación y finaliza partidas cuando aplica */
static void revisar_timeouts_ataque(void)
{
    int salas_timeout[MAX_SALAS];
    int recursos_timeout[MAX_SALAS];
    int total = 0;

    pthread_mutex_lock(&mtx_salas);
    time_t ahora = time(NULL);

    for (int i = 0; i < num_salas; i++) {
        Sala *s = &salas[i];
        if (!s->activa || s->estado != 1) continue;

        for (int r = 0; r < NUM_RECURSOS; r++) {
            Recurso *rec = &s->recursos[r];
            if (rec->bajo_ataque && !rec->mitigado && rec->ataque_ts > 0) {
                double dt = difftime(ahora, rec->ataque_ts);
                if (dt >= LIMITE_MITIGACION_SEG) {
                    s->estado = 2;
                    s->iniciada = 0;
                    salas_timeout[total] = s->id;
                    recursos_timeout[total] = r;
                    total++;
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&mtx_salas);

    for (int i = 0; i < total; i++) {
        char evento[160];
        snprintf(evento, sizeof(evento),
                 "EVENT GAME_END %d ATACANTES TIMEOUT %d\n",
                 salas_timeout[i], recursos_timeout[i]);
        broadcast_sala(salas_timeout[i], evento, -1);
    }
}

/* Hilo monitor del servidor para revisar timeouts de ataques activos */
static void *monitor_partidas(void *arg)
{
    (void)arg;
    while (1) {
        sleep(1);
        revisar_timeouts_ataque();
    }
    return NULL;
}

static const char *obtener_host_identidad(void)
{
    const char *host = getenv("CSGS_IDENTITY_HOST");
    if (host && *host) return host;
    return IDENTITY_HOST_DEFAULT;
}

static const char *obtener_puerto_identidad(void)
{
    const char *puerto = getenv("CSGS_IDENTITY_PORT");
    if (puerto && *puerto) return puerto;
    return IDENTITY_PORT_DEFAULT;
}

static int set_socket_io_timeout(int sock, int timeout_ms)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        return -1;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
        return -1;
    return 0;
}

static int connect_with_timeout(int sock, const struct sockaddr *addr,
                                socklen_t addrlen, int timeout_ms)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

    int rc = connect(sock, addr, addrlen);
    if (rc == 0) {
        (void)fcntl(sock, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS) {
        (void)fcntl(sock, F_SETFL, flags);
        return -1;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    rc = select(sock + 1, NULL, &wfds, NULL, &tv);
    if (rc <= 0) {
        if (rc == 0) errno = ETIMEDOUT;
        (void)fcntl(sock, F_SETFL, flags);
        return -1;
    }

    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) < 0) {
        (void)fcntl(sock, F_SETFL, flags);
        return -1;
    }
    if (so_error != 0) {
        errno = so_error;
        (void)fcntl(sock, F_SETFL, flags);
        return -1;
    }

    if (fcntl(sock, F_SETFL, flags) < 0) return -1;
    return 0;
}

static int obtener_host_windows_wsl(char *out, size_t out_sz)
{
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f) return 0;

    char linea[256];
    int ok = 0;
    while (fgets(linea, sizeof(linea), f)) {
        char etiqueta[32] = "";
        char ip[64] = "";
        if (sscanf(linea, "%31s %63s", etiqueta, ip) == 2) {
            if (strcmp(etiqueta, "nameserver") == 0) {
                strncpy(out, ip, out_sz - 1);
                out[out_sz - 1] = '\0';
                ok = 1;
                break;
            }
        }
    }

    fclose(f);
    return ok;
}

/* ═══════════════════════════════════════════════════════════════════
 *  DNS + CONEXIÓN AL SERVICIO DE IDENTIDAD
 *  No usamos IP hardcodeada: resolvemos IDENTITY_HOST via getaddrinfo
 * ═══════════════════════════════════════════════════════════════════ */
static int conectar_identidad_host(const char *host, const char *puerto)
{
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, puerto, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "[DNS] Error resolviendo '%s': %s\n",
                host, gai_strerror(rc));
        return -1;
    }

    int sock = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        if (connect_with_timeout(sock, rp->ai_addr, rp->ai_addrlen,
                                 IDENTITY_CONNECT_TIMEOUT_MS) == 0 &&
            set_socket_io_timeout(sock, IDENTITY_IO_TIMEOUT_MS) == 0) {
            break;
        }

        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;   /* -1 si falló */
}

static int conectar_identidad(void)
{
    const char *host = obtener_host_identidad();
    const char *puerto = obtener_puerto_identidad();
    char host_wsl[64] = "";

    int sock = conectar_identidad_host(host, puerto);
    if (sock >= 0) return sock;

    if (strcmp(host, IDENTITY_HOST_FALLBACK) != 0)
        sock = conectar_identidad_host(IDENTITY_HOST_FALLBACK, puerto);
    if (sock >= 0) return sock;

    if (obtener_host_windows_wsl(host_wsl, sizeof(host_wsl)) &&
        strcmp(host_wsl, host) != 0 &&
        strcmp(host_wsl, IDENTITY_HOST_FALLBACK) != 0) {
        sock = conectar_identidad_host(host_wsl, puerto);
    }
    return sock;
}

/*
 * consultar_identidad – devuelve 1 (OK) y rellena rol_out,
 *                        0 (credenciales inválidas),
 *                       -1 (servicio caído / error de red)
 */
static int consultar_identidad(const char *usuario, const char *pass,
                               char *rol_out, size_t rol_sz)
{
    int sock = conectar_identidad();
    if (sock < 0) return -1;

    char peticion[256];
    snprintf(peticion, sizeof(peticion), "VALIDAR %s %s\n", usuario, pass);
    if (send(sock, peticion, strlen(peticion), 0) < 0) {
        close(sock); return -1;
    }

    char respuesta[256];
    int  n = recv(sock, respuesta, sizeof(respuesta)-1, 0);
    close(sock);
    if (n <= 0) return -1;

    respuesta[n] = '\0';
    respuesta[strcspn(respuesta, "\r\n")] = '\0';

    if (strncmp(respuesta, "OK ", 3) == 0) {
        strncpy(rol_out, respuesta + 3, rol_sz - 1);
        rol_out[rol_sz-1] = '\0';
        return 1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  ENVÍO A UNA SALA (broadcast / unicast)
 * ═══════════════════════════════════════════════════════════════════ */
/* Envía a todos en la sala excepto al remitente (-1 = enviar a todos) */
static void broadcast_sala(int sala_id, const char *msg, int excepto_sock)
{
    pthread_mutex_lock(&mtx_salas);
    if (sala_id < 0 || sala_id >= num_salas) {
        pthread_mutex_unlock(&mtx_salas);
        return;
    }
    Sala *s = &salas[sala_id];
    for (int i = 0; i < s->num_jugadores; i++) {
        Jugador *j = s->jugadores[i];
        if (j && j->activo && j->socket != excepto_sock)
            send(j->socket, msg, strlen(msg), 0);
    }
    pthread_mutex_unlock(&mtx_salas);
}

/* Envía solo a defensores de la sala */
static void broadcast_defensores(int sala_id, const char *msg)
{
    pthread_mutex_lock(&mtx_salas);
    if (sala_id < 0 || sala_id >= num_salas) {
        pthread_mutex_unlock(&mtx_salas);
        return;
    }
    Sala *s = &salas[sala_id];
    for (int i = 0; i < s->num_jugadores; i++) {
        Jugador *j = s->jugadores[i];
        if (j && j->activo && strcmp(j->rol, "DEFENSOR") == 0)
            send(j->socket, msg, strlen(msg), 0);
    }
    pthread_mutex_unlock(&mtx_salas);
}

/* ═══════════════════════════════════════════════════════════════════
 *  GESTIÓN DE SALAS
 * ═══════════════════════════════════════════════════════════════════ */
static int crear_sala(void)
{
    pthread_mutex_lock(&mtx_salas);
    int id = -1;

    /* Reutilizar primero slots de salas inactivas o vacías */
    for (int i = 0; i < num_salas; i++) {
        if (!salas[i].activa || salas[i].num_jugadores == 0) {
            id = i;
            break;
        }
    }

    /* Si no hay slot libre, crear uno nuevo */
    if (id < 0) {
        if (num_salas >= MAX_SALAS) {
            pthread_mutex_unlock(&mtx_salas);
            return -1;
        }
        id = num_salas++;
    }

    Sala *s = &salas[id];
    s->id            = id;
    s->activa        = 1;
    s->iniciada      = 0;
    s->estado        = 0; /* WAITING */
    s->num_jugadores = 0;

    /* Inicializar recursos críticos */
    for (int r = 0; r < NUM_RECURSOS; r++) {
        s->recursos[r].id          = r;
        /* Generar coordenadas aleatorias con márgenes seguridad */
        s->recursos[r].x           = 50 + (rand() % (ANCHO_MAPA - 100));
        s->recursos[r].y           = 50 + (rand() % (ALTO_MAPA - 100));
        s->recursos[r].bajo_ataque = 0;
        s->recursos[r].mitigado    = 0;
        s->recursos[r].ataque_ts   = 0;
        strncpy(s->recursos[r].nombre, RECURSOS_NOMBRE[r],
                sizeof(s->recursos[r].nombre)-1);
    }
    pthread_mutex_unlock(&mtx_salas);
    return id;
}

/* Agrega jugador a sala; devuelve 0=OK, -1=error */
static int unir_sala(int sala_id, Jugador *j)
{
    pthread_mutex_lock(&mtx_salas);
    if (sala_id < 0 || sala_id >= num_salas) {
        pthread_mutex_unlock(&mtx_salas);
        return -1;
    }
    Sala *s = &salas[sala_id];
    if (!s->activa || s->num_jugadores >= MAX_J_SALA) {
        pthread_mutex_unlock(&mtx_salas);
        return -1;
    }
    s->jugadores[s->num_jugadores++] = j;
    j->sala_id = sala_id;
    pthread_mutex_unlock(&mtx_salas);
    return 0;
}

static void remover_de_sala(Jugador *j)
{
    if (j->sala_id < 0) return;
    pthread_mutex_lock(&mtx_salas);
    int sala_id = j->sala_id;
    Sala *s = &salas[sala_id];
    for (int i = 0; i < s->num_jugadores; i++) {
        if (s->jugadores[i] == j) {
            s->jugadores[i] = s->jugadores[s->num_jugadores - 1];
            s->num_jugadores--;
            break;
        }
    }

    /* Si quedó vacía, dejarla en WAITING para que pueda listarse/reutilizarse */
    if (s->num_jugadores == 0) {
        s->iniciada = 0;
        s->estado = 0; /* WAITING */
    }

    j->sala_id = -1;
    pthread_mutex_unlock(&mtx_salas);

    /* Puede regresar a WAITING si falta un rol mínimo */
    evaluar_estado_inicio_sala(sala_id);
}

/* Genera string con lista de salas */
static void listar_salas(char *buf, size_t sz)
{
    pthread_mutex_lock(&mtx_salas);
    int activas = 0;
    for (int i = 0; i < num_salas; i++)
        if (salas[i].activa) activas++;

    if (activas == 0) {
        strncpy(buf, "NO_ROOMS\n", sz);
        pthread_mutex_unlock(&mtx_salas);
        return;
    }
    int offset = 0;
    offset += snprintf(buf + offset, sz - offset,
                       "ROOMS %d", activas);
    for (int i = 0; i < num_salas && offset < (int)sz - 64; i++) {
        Sala *s = &salas[i];
        if (!s->activa) continue;
        offset += snprintf(buf + offset, sz - offset,
                           " %d:%d:%s",
                           s->id, s->num_jugadores,
                           estado_sala_str(s->estado));
    }
    offset += snprintf(buf + offset, sz - offset, "\n");
    pthread_mutex_unlock(&mtx_salas);
}

/* ═══════════════════════════════════════════════════════════════════
 *  DISTANCIA ENTRE DOS PUNTOS
 * ═══════════════════════════════════════════════════════════════════ */
static int distancia(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2, dy = y1 - y2;
    return (int)__builtin_sqrt((double)(dx*dx + dy*dy));
}

/* ═══════════════════════════════════════════════════════════════════
 *  HILO DE CADA CLIENTE
 * ═══════════════════════════════════════════════════════════════════ */
static void *manejar_cliente(void *arg)
{
    Jugador *j = (Jugador *)arg;
    int sock   = j->socket;

    log_msg(j->ip, j->puerto, "Conexión establecida.");

    char buf[1024];
    int  n;

    while ((n = recv(sock, buf, sizeof(buf)-1, 0)) > 0) {
        buf[n] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        if (strlen(buf) == 0) continue;

        /* Log petición */
        char lm[1100];
        snprintf(lm, sizeof(lm), "REQ: %s", buf);
        log_msg(j->ip, j->puerto, lm);

        /* ── Parsear comando ── */
        char copia[1024];
        strncpy(copia, buf, sizeof(copia)-1);
        char *cmd = strtok(copia, " ");
        if (!cmd) continue;

        char resp[512] = "";

        /* ─────────────────────────────────────────────
         *  AUTH <usuario> <password>
         *  Contrato:
         *   - Pre: conexión TCP activa
         *   - Acción: valida credenciales contra servicio de identidad
         *   - OK:   OK_AUTH <usuario> <rol>
         *   - ERR:  ERR_AUTH <detalle>
         *  Nota: este servidor no almacena usuarios localmente.
         * ───────────────────────────────────────────── */
        if (strcmp(cmd, "AUTH") == 0) {
            char *usr = strtok(NULL, " ");
            char *pwd = strtok(NULL, " ");
            if (!usr || !pwd) {
                responder_error(sock, resp, sizeof(resp), "ERR_AUTH",
                                "Formato: AUTH <usuario> <password>",
                                "E_AUTH_FORMAT");
            } else {
                char rol[32] = "";
                int r = consultar_identidad(usr, pwd, rol, sizeof(rol));
                if (r == 1) {
                    strncpy(j->nombre, usr,  sizeof(j->nombre)-1);
                    strncpy(j->rol,    rol,   sizeof(j->rol)-1);
                    j->pos_x = ANCHO_MAPA / 2;
                    j->pos_y = ALTO_MAPA  / 2;
                    snprintf(resp, sizeof(resp),
                             "OK_AUTH %s %s\n", usr, rol);
                } else if (r == 0) {
                    responder_error(sock, resp, sizeof(resp), "ERR_AUTH",
                                    "Credenciales invalidas",
                                    "E_AUTH_BAD_CREDENTIALS");
                } else {
                    responder_error(sock, resp, sizeof(resp), "ERR_AUTH",
                                    "Servicio de identidad no disponible",
                                    "E_AUTH_IDENTITY_DOWN");
                }
            }
            if (strncmp(resp, "OK_", 3) == 0)
                send(sock, resp, strlen(resp), 0);

        /* ─────────────────────────────────────────────
         *  LIST_ROOMS
         *  Contrato:
         *   - Acción: lista salas activas visibles para cualquier cliente
         *   - OK: ROOMS <total> ...   |   NO_ROOMS
         * ───────────────────────────────────────────── */
        } else if (strcmp(cmd, "LIST_ROOMS") == 0) {
            listar_salas(resp, sizeof(resp));
            send(sock, resp, strlen(resp), 0);

        /* ─────────────────────────────────────────────
         *  CREATE_ROOM
         *  Contrato:
         *   - Pre: cliente autenticado (nombre no vacío)
         *   - Acción: crea sala + une jugador creador
         *   - OK:  OK_CREATE <room_id>
         *   - ERR: ERR_CREATE <detalle>
         * ───────────────────────────────────────────── */
        } else if (strcmp(cmd, "CREATE_ROOM") == 0) {
            if (j->nombre[0] == '\0') {
                responder_error(sock, resp, sizeof(resp), "ERR_CREATE",
                                "Debes autenticarte primero",
                                "E_CREATE_NOT_AUTHENTICATED");
            } else if (j->sala_id >= 0) {
                int sala_actual = j->sala_id;
                int puede_salir = 0;

                pthread_mutex_lock(&mtx_salas);
                if (sala_actual < 0 || sala_actual >= num_salas) {
                    puede_salir = 1;
                } else {
                    Sala *s = &salas[sala_actual];
                    if (!s->activa || s->estado == 2) {
                        puede_salir = 1;
                    }
                }
                pthread_mutex_unlock(&mtx_salas);

                if (puede_salir) {
                    char leave_msg[128];
                    snprintf(leave_msg, sizeof(leave_msg),
                             "PLAYER_LEAVE %s\n", j->nombre);
                    broadcast_sala(sala_actual, leave_msg, sock);
                    remover_de_sala(j);
                } else {
                    responder_error(sock, resp, sizeof(resp), "ERR_CREATE",
                                    "Ya estas en una sala activa",
                                    "E_CREATE_ALREADY_IN_ROOM");
                    goto post_cmd_log;
                }
            }

            if (resp[0] == '\0') {
                int id = crear_sala();
                if (id < 0) {
                    responder_error(sock, resp, sizeof(resp), "ERR_CREATE",
                                    "Maximo de salas alcanzado",
                                    "E_CREATE_LIMIT_REACHED");
                } else {
                    unir_sala(id, j);
                    snprintf(resp, sizeof(resp), "OK_CREATE %d\n", id);
                }
            }
            if (strncmp(resp, "OK_", 3) == 0)
                send(sock, resp, strlen(resp), 0);

            if (strncmp(resp, "OK_", 3) == 0 && strcmp(j->rol, "DEFENSOR") == 0) {
                int sala_creada = -1;
                if (sscanf(resp, "OK_CREATE %d", &sala_creada) == 1) {
                    enviar_recursos_a_defensor(sock, sala_creada);
                }
            }

        /* ─────────────────────────────────────────────
         *  JOIN <sala_id>
         *  Contrato:
         *   - Pre: cliente autenticado
         *   - Acción: une jugador a sala existente
         *   - OK:  OK_JOIN <room_id>
         *   - ERR: ERR_JOIN <detalle>
         *  Efecto adicional:
         *   - Si es DEFENSOR: recibe RESOURCE_INFO por cada recurso crítico
         *   - Sala recibe evento PLAYER_JOIN
         * ───────────────────────────────────────────── */
        } else if (strcmp(cmd, "JOIN") == 0) {
            char *sid = strtok(NULL, " ");
            if (!sid) {
                responder_error(sock, resp, sizeof(resp), "ERR_JOIN",
                                "Formato: JOIN <sala_id>",
                                "E_JOIN_FORMAT");
            } else if (j->nombre[0] == '\0') {
                responder_error(sock, resp, sizeof(resp), "ERR_JOIN",
                                "Debes autenticarte primero",
                                "E_JOIN_NOT_AUTHENTICATED");
            } else {
                int sala_id = -1;
                if (!parse_int_strict(sid, &sala_id)) {
                    responder_error(sock, resp, sizeof(resp), "ERR_JOIN",
                                    "Formato: JOIN <sala_id>",
                                    "E_JOIN_FORMAT");
                    goto post_cmd_log;
                }

                if (j->sala_id == sala_id) {
                    responder_error(sock, resp, sizeof(resp), "ERR_JOIN",
                                    "Ya perteneces a esa sala",
                                    "E_JOIN_ALREADY_IN_ROOM");
                    goto post_cmd_log;
                }

                if (j->sala_id >= 0) {
                    int sala_anterior = j->sala_id;
                    char leave_msg[128];
                    snprintf(leave_msg, sizeof(leave_msg),
                             "PLAYER_LEAVE %s\n", j->nombre);
                    broadcast_sala(sala_anterior, leave_msg, sock);
                    remover_de_sala(j);
                }

                if (unir_sala(sala_id, j) < 0) {
                    char detalle[128];
                    snprintf(detalle, sizeof(detalle),
                             "Sala %d no encontrada o llena", sala_id);
                    responder_error(sock, resp, sizeof(resp), "ERR_JOIN",
                                    detalle,
                                    "E_JOIN_ROOM_UNAVAILABLE");
                } else {
                    snprintf(resp, sizeof(resp),
                             "OK_JOIN %d\n", sala_id);
                    send(sock, resp, strlen(resp), 0);

                    /* Revisar condición de inicio de partida */
                    evaluar_estado_inicio_sala(sala_id);

                    /* Si el rol es DEFENSOR, enviarle posición de recursos */
                    if (strcmp(j->rol, "DEFENSOR") == 0) {
                        enviar_recursos_a_defensor(sock, sala_id);
                    }

                    /* Notificar a los demás que alguien entró */
                    char nota[128];
                    snprintf(nota, sizeof(nota),
                             "PLAYER_JOIN %s %s\n", j->nombre, j->rol);
                    broadcast_sala(sala_id, nota, sock);
                }
            }

        /* ─────────────────────────────────────────────
         *  MOVE <x> <y>
         *  Contrato:
         *   - Pre: jugador dentro de una sala
         *   - Acción: actualiza posición (clamp a límites del mapa)
         *   - OK: evento POS al emisor y broadcast a sala
         *   - ERR: ERR_MOVE <detalle>
         *  Efecto adicional:
         *   - Si rol=ATACANTE y está en radio: RESOURCE_FOUND al atacante
         * ───────────────────────────────────────────── */
        } else if (strcmp(cmd, "MOVE") == 0) {
            char *xs = strtok(NULL, " ");
            char *ys = strtok(NULL, " ");
            if (!xs || !ys) {
                responder_error(sock, resp, sizeof(resp), "ERR_MOVE",
                                "Formato: MOVE <x> <y>",
                                "E_MOVE_FORMAT");
            } else if (j->sala_id < 0) {
                responder_error(sock, resp, sizeof(resp), "ERR_MOVE",
                                "Debes unirte a una sala primero",
                                "E_MOVE_NOT_IN_ROOM");
            } else {
                int nx = 0, ny = 0;
                if (!parse_int_strict(xs, &nx) || !parse_int_strict(ys, &ny)) {
                    responder_error(sock, resp, sizeof(resp), "ERR_MOVE",
                                    "Formato: MOVE <x> <y>",
                                    "E_MOVE_FORMAT");
                    goto post_cmd_log;
                }
                /* Clamp dentro del mapa */
                if (nx < 0)          nx = 0;
                if (nx > ANCHO_MAPA) nx = ANCHO_MAPA;
                if (ny < 0)          ny = 0;
                if (ny > ALTO_MAPA)  ny = ALTO_MAPA;
                j->pos_x = nx;
                j->pos_y = ny;

                /* Broadcast posición a toda la sala */
                snprintf(resp, sizeof(resp),
                         "POS %s %d %d %s\n",
                         j->nombre, nx, ny, j->rol);
                send(sock, resp, strlen(resp), 0);
                broadcast_sala(j->sala_id, resp, sock);

                /* Si es atacante, verificar cercanía a recursos */
                if (strcmp(j->rol, "ATACANTE") == 0) {
                    pthread_mutex_lock(&mtx_salas);
                    Sala *s = &salas[j->sala_id];
                    for (int r = 0; r < NUM_RECURSOS; r++) {
                        int d = distancia(nx, ny,
                                          s->recursos[r].x,
                                          s->recursos[r].y);
                        if (d <= RADIO_DETECCION) {
                            char rf[128];
                            snprintf(rf, sizeof(rf),
                                     "RESOURCE_FOUND %d %s %d %d\n",
                                     r,
                                     s->recursos[r].nombre,
                                     s->recursos[r].x,
                                     s->recursos[r].y);
                            send(sock, rf, strlen(rf), 0);
                        }
                    }
                    pthread_mutex_unlock(&mtx_salas);
                }
            }

        /* ─────────────────────────────────────────────
         *  ATTACK <resource_id>
         *  Contrato:
         *   - Pre: rol ATACANTE + sala activa + recurso válido
         *   - Validación: distancia <= RADIO_DETECCION
         *   - OK:  OK_ATTACK <rid> + ATTACK_ALERT a defensores/sala
         *   - ERR: ERR_ATTACK <detalle>
         * ───────────────────────────────────────────── */
        } else if (strcmp(cmd, "ATTACK") == 0) {
            char *rid = strtok(NULL, " ");
            if (!rid) {
                responder_error(sock, resp, sizeof(resp), "ERR_ATTACK",
                                "Formato: ATTACK <resource_id>",
                                "E_ATTACK_FORMAT");
            } else if (strcmp(j->rol, "ATACANTE") != 0) {
                responder_error(sock, resp, sizeof(resp), "ERR_ATTACK",
                                "Solo los ATACANTES pueden atacar",
                                "E_ATTACK_ROLE");
            } else if (j->sala_id < 0) {
                responder_error(sock, resp, sizeof(resp), "ERR_ATTACK",
                                "Sin sala activa",
                                "E_ATTACK_NO_ROOM");
            } else {
                pthread_mutex_lock(&mtx_salas);
                Sala *estado = &salas[j->sala_id];
                int estado_actual = estado->estado;
                pthread_mutex_unlock(&mtx_salas);

                if (estado_actual == 0) {
                    responder_error(sock, resp, sizeof(resp), "ERR_ATTACK",
                                    "Partida en espera de roles",
                                    "E_ATTACK_GAME_WAITING");
                    goto post_cmd_log;
                }
                if (estado_actual == 2) {
                    responder_error(sock, resp, sizeof(resp), "ERR_ATTACK",
                                    "Partida finalizada",
                                    "E_ATTACK_GAME_FINISHED");
                    goto post_cmd_log;
                }

                int r = -1;
                if (!parse_int_strict(rid, &r)) {
                    responder_error(sock, resp, sizeof(resp), "ERR_ATTACK",
                                    "Formato: ATTACK <resource_id>",
                                    "E_ATTACK_FORMAT");
                    goto post_cmd_log;
                }
                pthread_mutex_lock(&mtx_salas);
                Sala *s = &salas[j->sala_id];
                int ok = 0;
                if (r >= 0 && r < NUM_RECURSOS
                    && !s->recursos[r].bajo_ataque
                    && !s->recursos[r].mitigado)
                {
                    /* Verificar que el atacante esté cerca */
                    int d = distancia(j->pos_x, j->pos_y,
                                      s->recursos[r].x,
                                      s->recursos[r].y);
                    if (d <= RADIO_DETECCION) {
                        s->recursos[r].bajo_ataque = 1;
                        s->recursos[r].ataque_ts   = time(NULL);
                        strncpy(s->recursos[r].atacante, j->nombre,
                                sizeof(s->recursos[r].atacante)-1);
                        ok = 1;
                    }
                }
                pthread_mutex_unlock(&mtx_salas);

                if (ok) {
                    snprintf(resp, sizeof(resp),
                             "OK_ATTACK %d\n", r);
                    send(sock, resp, strlen(resp), 0);

                    /* Alerta a defensores */
                    char alerta[128];
                    snprintf(alerta, sizeof(alerta),
                             "ATTACK_ALERT %s %d %s\n",
                             j->nombre, r, RECURSOS_NOMBRE[r]);
                    broadcast_defensores(j->sala_id, alerta);

                    /* Evaluar condición de fin para ATACANTES */
                    evaluar_fin_sala(j->sala_id);
                } else {
                    responder_error(sock, resp, sizeof(resp), "ERR_ATTACK",
                                    "Recurso invalido, ya atacado, mitigado o demasiado lejos",
                                    "E_ATTACK_INVALID_TARGET");
                }
            }

        /* ─────────────────────────────────────────────
         *  DEFEND <resource_id>
         *  Contrato:
         *   - Pre: rol DEFENSOR + sala activa + recurso bajo ataque
         *   - Validación: distancia <= RADIO_DETECCION
         *   - OK:  OK_DEFEND <rid> + DEFEND_SUCCESS a sala
         *   - ERR: ERR_DEFEND <detalle>
         * ───────────────────────────────────────────── */
        } else if (strcmp(cmd, "DEFEND") == 0) {
            char *rid = strtok(NULL, " ");
            if (!rid) {
                responder_error(sock, resp, sizeof(resp), "ERR_DEFEND",
                                "Formato: DEFEND <resource_id>",
                                "E_DEFEND_FORMAT");
            } else if (strcmp(j->rol, "DEFENSOR") != 0) {
                responder_error(sock, resp, sizeof(resp), "ERR_DEFEND",
                                "Solo los DEFENSORES pueden defender",
                                "E_DEFEND_ROLE");
            } else if (j->sala_id < 0) {
                responder_error(sock, resp, sizeof(resp), "ERR_DEFEND",
                                "Sin sala activa",
                                "E_DEFEND_NO_ROOM");
            } else {
                pthread_mutex_lock(&mtx_salas);
                Sala *estado = &salas[j->sala_id];
                int estado_actual = estado->estado;
                pthread_mutex_unlock(&mtx_salas);

                if (estado_actual == 0) {
                    responder_error(sock, resp, sizeof(resp), "ERR_DEFEND",
                                    "Partida en espera de roles",
                                    "E_DEFEND_GAME_WAITING");
                    goto post_cmd_log;
                }
                if (estado_actual == 2) {
                    responder_error(sock, resp, sizeof(resp), "ERR_DEFEND",
                                    "Partida finalizada",
                                    "E_DEFEND_GAME_FINISHED");
                    goto post_cmd_log;
                }

                int r = -1;
                if (!parse_int_strict(rid, &r)) {
                    responder_error(sock, resp, sizeof(resp), "ERR_DEFEND",
                                    "Formato: DEFEND <resource_id>",
                                    "E_DEFEND_FORMAT");
                    goto post_cmd_log;
                }
                pthread_mutex_lock(&mtx_salas);
                Sala *s = &salas[j->sala_id];
                int ok = 0;
                if (r >= 0 && r < NUM_RECURSOS
                    && s->recursos[r].bajo_ataque
                    && !s->recursos[r].mitigado)
                {
                    /* Defensor debe estar cerca del recurso */
                    int d = distancia(j->pos_x, j->pos_y,
                                      s->recursos[r].x,
                                      s->recursos[r].y);
                    if (d <= RADIO_DETECCION) {
                        s->recursos[r].bajo_ataque = 0;
                        s->recursos[r].mitigado    = 1;
                        s->recursos[r].ataque_ts   = 0;
                        ok = 1;
                    }
                }
                pthread_mutex_unlock(&mtx_salas);

                if (ok) {
                    snprintf(resp, sizeof(resp),
                             "OK_DEFEND %d\n", r);
                    send(sock, resp, strlen(resp), 0);

                    char notif[128];
                    snprintf(notif, sizeof(notif),
                             "DEFEND_SUCCESS %s %d %s\n",
                             j->nombre, r, RECURSOS_NOMBRE[r]);
                    broadcast_sala(j->sala_id, notif, sock);

                    /* Evaluar condición de fin para DEFENSORES */
                    evaluar_fin_sala(j->sala_id);
                } else {
                    responder_error(sock, resp, sizeof(resp), "ERR_DEFEND",
                                    "Recurso no esta bajo ataque, ya mitigado o demasiado lejos",
                                    "E_DEFEND_INVALID_TARGET");
                }
            }

        /* ─────────────────────────────────────────────
         *  QUIT
         *  Contrato:
         *   - Acción: cierre voluntario de sesión
         *   - OK: OK_QUIT Hasta luego
         *   - Efecto: salida del loop y limpieza de estado
         * ───────────────────────────────────────────── */
        } else if (strcmp(cmd, "QUIT") == 0) {
            snprintf(resp, sizeof(resp), "OK_QUIT Hasta luego\n");
            send(sock, resp, strlen(resp), 0);
            break;

        /* ─────────────────────────────────────────────
         *  Comando desconocido
         *  Contrato:
         *   - Cualquier verbo no definido explícitamente
         *   - ERR: ERR_UNKNOWN Comando '<cmd>' no reconocido
         * ───────────────────────────────────────────── */
        } else {
            char detalle[160];
            snprintf(detalle, sizeof(detalle),
                     "Comando '%s' no reconocido", cmd);
            responder_error(sock, resp, sizeof(resp), "ERR_UNKNOWN",
                            detalle,
                            "E_UNKNOWN_COMMAND");
        }

    post_cmd_log:

        /* Log respuesta */
        snprintf(lm, sizeof(lm), "RESP: %.200s", resp);
        /* quitar salto de línea para el log */
        lm[strcspn(lm, "\n")] = '\0';
        log_msg(j->ip, j->puerto, lm);
    }

    /* ── Limpieza al desconectar ── */
    if (j->sala_id >= 0) {
        char nota[128];
        snprintf(nota, sizeof(nota),
                 "PLAYER_LEAVE %s\n", j->nombre);
        broadcast_sala(j->sala_id, nota, sock);
        remover_de_sala(j);
    }
    log_msg(j->ip, j->puerto, "Desconectado.");

    pthread_mutex_lock(&mtx_jugadores);
    j->activo = 0;
    pthread_mutex_unlock(&mtx_jugadores);

    close(sock);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════
 *  MAIN
 * ═══════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int  puerto = atoi(argv[1]);
    strncpy(archivo_logs, argv[2], sizeof(archivo_logs)-1);

    /* Evita que send() a sockets cerrados termine el proceso con SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    /* Inicializar generador de números aleatorios */
    srand(time(NULL));

    /* Inicializar jugadores */
    memset(jugadores, 0, sizeof(jugadores));
    for (int i = 0; i < MAX_CLIENTES; i++) jugadores[i].sala_id = -1;

    /* Crear socket servidor */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(puerto);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv); exit(EXIT_FAILURE);
    }
    if (listen(srv, 10) < 0) {
        perror("listen"); close(srv); exit(EXIT_FAILURE);
    }

    printf("═══════════════════════════════════════════\n");
    printf(" Servidor CSGS iniciado en el puerto %d\n", puerto);
    printf(" Logs: %s\n", archivo_logs);
    printf(" Recursos: %d fijos en el mapa\n", NUM_RECURSOS);
    printf(" Timeout de mitigación: %d segundos\n", LIMITE_MITIGACION_SEG);
    printf("═══════════════════════════════════════════\n");

    pthread_t hilo_monitor;
    if (pthread_create(&hilo_monitor, NULL, monitor_partidas, NULL) == 0)
        pthread_detach(hilo_monitor);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli_sock = accept(srv, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_sock < 0) {
            perror("accept"); continue;
        }

        /* Buscar slot libre */
        pthread_mutex_lock(&mtx_jugadores);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTES; i++) {
            if (!jugadores[i].activo) { slot = i; break; }
        }
        if (slot < 0) {
            pthread_mutex_unlock(&mtx_jugadores);
            const char *full =
                "ERR_FULL Servidor lleno [CODE=E_SERVER_FULL]\n";
            send(cli_sock, full, strlen(full), 0);
            close(cli_sock);
            continue;
        }

        Jugador *j = &jugadores[slot];
        memset(j, 0, sizeof(*j));
        j->socket   = cli_sock;
        j->sala_id  = -1;
        j->activo   = 1;
        j->pos_x    = ANCHO_MAPA / 2;
        j->pos_y    = ALTO_MAPA  / 2;
        inet_ntop(AF_INET, &cli_addr.sin_addr, j->ip, sizeof(j->ip));
        j->puerto = ntohs(cli_addr.sin_port);
        if (num_jugadores < MAX_CLIENTES) num_jugadores++;
        pthread_mutex_unlock(&mtx_jugadores);

        pthread_t hilo;
        if (pthread_create(&hilo, NULL, manejar_cliente, j) != 0) {
            perror("pthread_create");
            j->activo = 0;
            close(cli_sock);
        } else {
            pthread_detach(hilo);
        }
    }

    close(srv);
    return 0;
}
