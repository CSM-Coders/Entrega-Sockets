# Protocolo CSGS v1.0

**CyberSecurity Game Server Protocol**  
Estado: Implementado (v1.0)  
Transporte: TCP (`SOCK_STREAM`)  
Codificación: Texto UTF-8, una línea por mensaje, terminada en `\n`

---

## 1. Propósito y alcance

Este documento define, de forma **formal y verificable**, cómo se comunican:

- Cliente de juego (Python/Java)
- Servidor maestro del juego (C)
- Servicio de identidad (servicio separado)

La meta es que cualquier implementación compatible pueda interoperar sin ambigüedades.

### ¿Por qué este protocolo existe?
Porque el enunciado exige un protocolo de capa de aplicación propio, con:
- vocabulario de mensajes,
- reglas de procedimiento,
- manejo de errores,
- flujo de operación completo.

---

## 2. Modelo arquitectónico

### 2.1 Modelo lógico
- **Cliente de juego**: abre conexión TCP persistente con el servidor maestro.
- **Servidor maestro (C)**: coordina salas, jugadores, recursos y eventos en tiempo real.
- **Servicio de identidad**: valida credenciales y devuelve rol.
- **Servidor HTTP**: interfaz web para listar salas e iniciar flujo de acceso.

### 2.2 Razón de usar TCP
Se usa TCP porque el juego requiere:
- entrega confiable,
- orden de mensajes,
- canal persistente para eventos (`ATTACK_ALERT`, `POS`, etc.).

Esto es coherente con `SOCK_STREAM` y evita pérdida/desorden que sí podría ocurrir con datagramas.

---

## 3. Convenciones generales de formato

## 3.1 Forma de mensaje
Cada mensaje es una línea de texto:

`COMANDO arg1 arg2 ... argN\n`

### Reglas
1. Separador de campos: un espacio (` `).
2. Fin de mensaje: `\n` (admite `\r\n` al recibir).
3. Nombres de comando en mayúscula.
4. Si un comando exige argumentos y faltan, responde error de formato.

## 3.2 Tipos de mensajes
- **Petición**: cliente → servidor.
- **Respuesta**: servidor → cliente que pidió la acción.
- **Evento**: servidor → uno o varios clientes para notificar estado del juego.

## 3.3 Convención de códigos de error

CSGS v1 mantiene compatibilidad con prefijos históricos `ERR_*` y agrega un
identificador de error al final del mensaje:

`ERR_<COMANDO> <detalle> [CODE=<codigo>]`

Ejemplos:
- `ERR_AUTH Credenciales invalidas [CODE=E_AUTH_BAD_CREDENTIALS]`
- `ERR_ATTACK Formato: ATTACK <resource_id> [CODE=E_ATTACK_FORMAT]`
- `ERR_UNKNOWN Comando 'HOLA' no reconocido [CODE=E_UNKNOWN_COMMAND]`

Esto permite:
1. mantener clientes existentes (que ya parsean `ERR_*`),
2. ofrecer clasificación estable de errores para pruebas y observabilidad.

---

## 4. Primitivas del servicio (cliente → servidor)

## 4.1 `AUTH <usuario> <password>`
Autentica jugador contra servicio de identidad.

- Éxito: `OK_AUTH <usuario> <rol>`
- Error credenciales: `ERR_AUTH Credenciales invalidas`
- Error servicio identidad: `ERR_AUTH Servicio de identidad no disponible`
- Error formato: `ERR_AUTH Formato: AUTH <usuario> <password>`

**Por qué existe:** cumple requisito de no usar usuarios locales en servidor de juego.

---

## 4.2 `LIST_ROOMS`
Lista salas activas.

- Con salas: `ROOMS <total> <id>:<jugadores>:<estado> ...`
- Sin salas: `NO_ROOMS`
Ejemplo:
`ROOMS 2 0:3:WAITING 1:2:IN_GAME`

---

## 4.3 `CREATE_ROOM`
Crea sala y une al jugador autenticado.

- Éxito: `OK_CREATE <room_id>`
- Error no autenticado: `ERR_CREATE Debes autenticarte primero`
- Error capacidad: `ERR_CREATE Maximo de salas alcanzado`
- Error si ya está en sala: `ERR_CREATE Ya estas en una sala activa`

---

## 4.4 `JOIN <room_id>`
Une al jugador autenticado a una sala existente.

- Éxito: `OK_JOIN <room_id>`
- Error formato: `ERR_JOIN Formato: JOIN <sala_id>`
- Error no autenticado: `ERR_JOIN Debes autenticarte primero`
- Error sala inválida/llena: `ERR_JOIN Sala <id> no encontrada o llena`
- Error si ya está en la misma sala: `ERR_JOIN Ya perteneces a esa sala`
- Si estaba en otra sala, el servidor ejecuta salida limpia (`PLAYER_LEAVE`) y luego unión.

---

## 4.4.1 Validación estricta de enteros

En comandos numéricos (`JOIN`, `MOVE`, `ATTACK`, `DEFEND`), CSGS v1 aplica parseo
estricto de enteros.

Consecuencia:
- `ATTACK abc` o `JOIN sala1` devuelven error de formato,
- se evita conversión implícita a `0` que generaría comportamientos ambiguos.

---

## 4.5 `MOVE <x> <y>`
Actualiza posición en el plano.

- Éxito (eco + broadcast): `POS <nombre> <x> <y> <rol>`
- Error sin sala: `ERR_MOVE Debes unirte a una sala primero`
- Error formato: `ERR_MOVE Formato: MOVE <x> <y>`

Notas:
- Coordenadas se ajustan (clamp) al área válida del mapa.
- Si rol = atacante y entra en radio de detección, se emite `RESOURCE_FOUND`.

---

## 4.6 `ATTACK <resource_id>`
Solicita ataque sobre recurso crítico.

- Éxito: `OK_ATTACK <resource_id>`
- Error de rol: `ERR_ATTACK Solo los ATACANTES pueden atacar`
- Error sin sala: `ERR_ATTACK Sin sala activa`
- Error formato: `ERR_ATTACK Formato: ATTACK <resource_id>`
- Error de regla (lejos/no válido/ya mitigado):
  `ERR_ATTACK Recurso invalido, ya atacado, mitigado o demasiado lejos`

---

## 4.7 `DEFEND <resource_id>`
Solicita mitigación de ataque sobre recurso crítico.

- Éxito: `OK_DEFEND <resource_id>`
- Error de rol: `ERR_DEFEND Solo los DEFENSORES pueden defender`
- Error sin sala: `ERR_DEFEND Sin sala activa`
- Error formato: `ERR_DEFEND Formato: DEFEND <resource_id>`
- Error de regla (no atacado/lejos/ya mitigado):
  `ERR_DEFEND Recurso no esta bajo ataque, ya mitigado o demasiado lejos`

---

## 4.8 `QUIT`
Cierre voluntario de sesión del cliente.

- Respuesta: `OK_QUIT Hasta luego`

---

## 5. Eventos del servidor (servidor → cliente/s)

## 5.1 Presencia y posición
- `PLAYER_JOIN <nombre> <rol>`
- `PLAYER_LEAVE <nombre>`
- `POS <nombre> <x> <y> <rol>`

## 5.2 Recursos
- `RESOURCE_INFO <id> <nombre> <x> <y>` (defensores al unirse)
- `RESOURCE_FOUND <id> <nombre> <x> <y>` (atacante al detectar)

## 5.3 Ataque/mitigación
- `ATTACK_ALERT <atacante> <resource_id> <nombre_recurso>`
- `DEFEND_SUCCESS <defensor> <resource_id> <nombre_recurso>`

## 5.4 Ciclo de partida (estados de sala)
- `EVENT GAME_START <room_id> <atacantes> <defensores>`
- `EVENT GAME_WAITING <room_id> <atacantes> <defensores>`
- `EVENT GAME_END <room_id> <ganador>`
- `EVENT GAME_END <room_id> ATACANTES TIMEOUT <resource_id>`

Estados válidos de sala:
- `WAITING`: no hay mínimo de roles para iniciar.
- `IN_GAME`: existe al menos 1 atacante y 1 defensor activos.
- `FINISHED`: partida terminada por condición de victoria.

---

## 6. Máquina de estados del jugador

Estados lógicos del cliente respecto al servidor:

1. `CONNECTED` (conectado por TCP, aún no autenticado)
2. `AUTHENTICATED` (con rol asignado)
3. `IN_ROOM` (unido a sala)
4. `DISCONNECTED`

Transiciones principales:
- `CONNECTED --AUTH OK--> AUTHENTICATED`
- `AUTHENTICATED --CREATE_ROOM/JOIN OK--> IN_ROOM`
- `IN_ROOM --QUIT/desconexión--> DISCONNECTED`

Restricciones de validez:
- `CREATE_ROOM` y `JOIN` requieren `AUTHENTICATED`.
- `MOVE`, `ATTACK`, `DEFEND` requieren `IN_ROOM`.
- `ATTACK` sólo para rol atacante.
- `DEFEND` sólo para rol defensor.

---

## 7. Reglas de procedimiento del juego

## 7.1 Inicio de sesión
1. Cliente abre TCP.
2. Cliente envía `AUTH`.
3. Servidor consulta identidad (`VALIDAR usuario password`) en servicio separado.
4. Servidor responde `OK_AUTH` o `ERR_AUTH`.

## 7.2 Entrada a partida
1. Cliente lista o crea sala.
2. Cliente se une (`JOIN`) o crea (`CREATE_ROOM`).
3. Servidor responde `OK_*`.
4. Si jugador es defensor, servidor envía `RESOURCE_INFO` para cada recurso.

## 7.3 Movimiento y exploración
1. Cliente envía `MOVE x y`.
2. Servidor valida y actualiza posición.
3. Servidor emite `POS` al emisor y a la sala.
4. Si atacante entra a radio de recurso, servidor emite `RESOURCE_FOUND` al atacante.

## 7.4 Ciclo de ataque y defensa
1. Atacante detecta recurso y envía `ATTACK rid`.
2. Servidor valida rol, sala y distancia.
3. Si válido:
   - marca recurso bajo ataque,
   - responde `OK_ATTACK rid` al atacante,
   - emite `ATTACK_ALERT` a defensores (y sala, según implementación).
4. Defensor se aproxima y envía `DEFEND rid`.
5. Servidor valida rol, estado y distancia.
6. Si válido:
   - marca mitigado,
   - responde `OK_DEFEND rid`,
   - emite `DEFEND_SUCCESS` a la sala.

## 7.5 Reglas de transición de estado de sala
1. Al unirse jugadores, el servidor evalúa roles de la sala.
2. Si hay al menos 1 atacante y 1 defensor:
   - estado pasa a `IN_GAME`,
   - se emite `EVENT GAME_START ...`.
3. Si durante la partida se pierde la condición mínima de roles:
   - estado vuelve a `WAITING`,
   - se emite `EVENT GAME_WAITING ...`.
4. Condición de fin defensores:
   - todos los recursos quedan mitigados,
   - se emite `EVENT GAME_END <room_id> DEFENSORES`.
5. Condición de fin atacantes:
   - un recurso permanece bajo ataque sin mitigación más allá del límite,
   - se emite `EVENT GAME_END <room_id> ATACANTES TIMEOUT <resource_id>`.

## 7.6 Temporizador de mitigación

CSGS v1 implementa un temporizador de atención por ataque:
- Parámetro servidor: `LIMITE_MITIGACION_SEG = 10`.
- Cuando un atacante ejecuta `ATTACK` válido, se registra timestamp de ataque.
- Un monitor en segundo plano revisa cada segundo ataques activos no mitigados.
- Si el tiempo excede el límite, la sala pasa a `FINISHED` y gana `ATACANTES`.

---

## 8. Manejo de excepciones y robustez

El protocolo contempla manejo explícito para:

1. **Formato inválido** de comando (`ERR_* Formato ...`).
2. **Comando desconocido** (`ERR_UNKNOWN Comando '<cmd>' no reconocido`).
3. **Permisos por rol** (`ERR_ATTACK` / `ERR_DEFEND`).
4. **Estado inválido** (sin autenticación o sin sala).
5. **Conexiones fallidas** al servicio de identidad.
6. **Desconexión abrupta de cliente**:
   - remoción de sala,
   - notificación `PLAYER_LEAVE`.

Regla de prioridad de validación (CSGS v1):
1. Primero se valida el **formato** del comando (argumentos requeridos).
2. Luego se valida **rol** y **estado** del jugador.

Esto garantiza respuestas de error más consistentes para clientes heterogéneos.

Esto cumple el requisito del enunciado de control de excepciones.

### 8.1 Códigos de error implementados (resumen)

- `E_AUTH_FORMAT`, `E_AUTH_BAD_CREDENTIALS`, `E_AUTH_IDENTITY_DOWN`
- `E_CREATE_NOT_AUTHENTICATED`, `E_CREATE_LIMIT_REACHED`, `E_CREATE_ALREADY_IN_ROOM`
- `E_JOIN_FORMAT`, `E_JOIN_NOT_AUTHENTICATED`, `E_JOIN_ROOM_UNAVAILABLE`,
  `E_JOIN_ALREADY_IN_ROOM`
- `E_MOVE_FORMAT`, `E_MOVE_NOT_IN_ROOM`
- `E_ATTACK_FORMAT`, `E_ATTACK_ROLE`, `E_ATTACK_NO_ROOM`,
  `E_ATTACK_GAME_WAITING`, `E_ATTACK_GAME_FINISHED`,
  `E_ATTACK_INVALID_TARGET`
- `E_DEFEND_FORMAT`, `E_DEFEND_ROLE`, `E_DEFEND_NO_ROOM`,
  `E_DEFEND_GAME_WAITING`, `E_DEFEND_GAME_FINISHED`,
  `E_DEFEND_INVALID_TARGET`
- `E_UNKNOWN_COMMAND`, `E_SERVER_FULL`

---

## 9. Protocolo interno de identidad

Canal TCP texto (servicio separado):

- Request: `VALIDAR <usuario> <password>`
- Response éxito: `OK <ROL>`
- Response fallo credenciales: `ERROR_CREDENTIALS`
- Response formato inválido: `ERROR_FORMATO`

**Razón:** separar autenticación del servidor del juego y evitar usuarios locales.

---

## 10. Ejemplos de secuencia

## 10.1 Login y unión a sala

Cliente:
`AUTH alice alice123`

Servidor:
`OK_AUTH alice ATACANTE`

Cliente:
`LIST_ROOMS`

Servidor:
`ROOMS 1 0:1:WAITING`

Cliente:
`JOIN 0`

Servidor:
`OK_JOIN 0`

---

## 10.2 Ataque y mitigación

Atacante:
`MOVE 98 77`

Servidor:
`POS alice 98 77 ATACANTE`
`RESOURCE_FOUND 0 Servidor_A 100 80`

Atacante:
`ATTACK 0`

Servidor (atacante):
`OK_ATTACK 0`

Servidor (defensores):
`ATTACK_ALERT alice 0 Servidor_A`

Defensor:
`MOVE 102 82`
`DEFEND 0`

Servidor (defensor):
`OK_DEFEND 0`

Servidor (sala):
`DEFEND_SUCCESS bob 0 Servidor_A`
