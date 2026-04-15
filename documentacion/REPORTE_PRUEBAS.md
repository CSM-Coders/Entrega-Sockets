# Reporte de pruebas

## 1) Objetivo

Validar rápidamente que el protocolo CSGS responde correctamente en:
- autenticación,
- error de credenciales,
- comando desconocido,
- manejo básico de error en ataque.

---

## 2) Entorno ejecutado

- Servicio identidad: `servicio_identidad.py` en puerto `9090`.
- Servidor juego: `./servidor 8080 logs_nuevo.txt`.

Estas pruebas las corrimos en local con dos terminales y clientes conectando por `localhost`.

Verificación de puertos:
- `8080` escuchando (servidor de juego).
- `9090` escuchando (identidad).

---

## 3) Resultados del smoke test

Resultado global: **PASS (4/4)**

### Caso AUTH_OK
- Request: `AUTH alice alice123`
- Response: `OK_AUTH alice ATACANTE`
- Estado: PASS

### Caso AUTH_FAIL
- Request: `AUTH alice incorrecta`
- Response: `ERR_AUTH Credenciales invalidas`
- Estado: PASS

### Caso UNKNOWN_CMD
- Request: `HOLA`
- Response: `ERR_UNKNOWN Comando 'HOLA' no reconocido`
- Estado: PASS

### Caso ATTACK_FORMAT
- Request: `ATTACK`
- Response: `ERR_ATTACK Formato: ATTACK <resource_id>`
- Estado: PASS

---

## 4) Evidencia de logging

Se observan entradas REQ/RESP con IP:puerto en `logs_nuevo.txt` y/o `logs.txt`, por ejemplo:

- `[127.0.0.1:65244] REQ: AUTH alice alice123`
- `[127.0.0.1:65244] RESP: OK_AUTH alice ATACANTE`
- `[127.0.0.1:65252] REQ: HOLA`
- `[127.0.0.1:65252] RESP: ERR_UNKNOWN Comando 'HOLA' no reconocido`

Esto respalda cumplimiento del requisito de logging de peticiones/respuestas.

---

## 5) Observaciones técnicas

1. Se confirmó validación de formato primero en comandos con argumentos (`MOVE`, `ATTACK`, `DEFEND`).
2. Se mantiene compatibilidad con clientes existentes al conservar prefijos `ERR_*`.
3. En general el comportamiento fue estable durante las pruebas multicliente.

---

## 6) Prueba integrada (atacante + defensor)

También validamos en vivo la coordinación entre dos clientes:
- `MOVE` + `RESOURCE_FOUND`
- `ATTACK` + `ATTACK_ALERT`
- `DEFEND` + `DEFEND_SUCCESS`

---

## 7) Actualización: prueba integrada ejecutada

La prueba integrada nos dio este resultado:

- **PASS (0 fallos)**

Validaciones obtenidas:
1. Login atacante y defensor correcto.
2. `CREATE_ROOM` y `JOIN` correctos.
3. Defensor recibe `RESOURCE_INFO`.
4. Atacante detecta recurso con `RESOURCE_FOUND`.
5. `ATTACK 0` produce `OK_ATTACK 0`.
6. Defensor recibe `ATTACK_ALERT`.
7. `DEFEND 0` produce `OK_DEFEND 0`.
8. Sala recibe `DEFEND_SUCCESS`.

### Corrección aplicada durante validación

Se detectó duplicidad de `ATTACK_ALERT` (el defensor lo recibía 2 veces).

- Se corrigió en el servidor eliminando el segundo broadcast redundante.
- Se reejecutó la prueba integrada y quedó confirmada sin duplicados.

### Mejora adicional aplicada (consistencia de errores)

Se estandarizó la prioridad de validación para comandos con argumentos (`MOVE`,
`ATTACK`, `DEFEND`) de modo que primero respondan error de **formato** y luego
validen rol/estado.

Validación observada:
- Request: `ATTACK`
- Response: `ERR_ATTACK Formato: ATTACK <resource_id>`

Resultado: mayor predictibilidad del protocolo para clientes Python/Java.

---

## 8) Nueva mejora funcional: estado formal de partida

Se implementó máquina de estados de sala:
- `WAITING`
- `IN_GAME`
- `FINISHED`

Nuevos eventos emitidos por el servidor:
- `EVENT GAME_START <room_id> <atacantes> <defensores>`
- `EVENT GAME_WAITING <room_id> <atacantes> <defensores>`
- `EVENT GAME_END <room_id> <ganador>`

### Validación observada en prueba integrada

Durante `JOIN` con ambos roles presentes:
- defensor y atacante recibieron `EVENT GAME_START ...`

Tras salida del atacante:
- defensor recibió `EVENT GAME_WAITING ...`

Resultado: la transición de estado de sala queda explícita y trazable en protocolo.

---

## 9) Validación de temporizador de mitigación

Se implementó y probó la regla de tiempo límite para atención de ataques.

- Configuración actual del servidor: `LIMITE_MITIGACION_SEG = 10`.
- Tiempo de espera usado para la validación: 13 segundos.
- Resultado: **PASS (0 fallos)**.

Evidencia observada:
- `EVENT GAME_END 1 ATACANTES TIMEOUT 0`

Interpretación:
- El recurso `0` permaneció bajo ataque sin mitigación más allá del límite.
- El servidor finalizó la partida y declaró victoria de atacantes por timeout.

---

## 10) Estandarización de errores con códigos

Se agregó convención de salida para errores sin romper compatibilidad:

- Formato: `ERR_<COMANDO> <detalle> [CODE=<codigo>]`

Evidencia en smoke tests:
- `ERR_AUTH Credenciales invalidas [CODE=E_AUTH_BAD_CREDENTIALS]`
- `ERR_ATTACK Formato: ATTACK <resource_id> [CODE=E_ATTACK_FORMAT]`
- `ERR_UNKNOWN Comando 'HOLA' no reconocido [CODE=E_UNKNOWN_COMMAND]`

Resultado:
- Clientes actuales siguen operando (parsean `ERR_`).
- Se mejora trazabilidad y diagnóstico de fallos para sustentación y pruebas.

---

## 11) Robustez de concurrencia y desconexiones

Se ejecutó prueba concurrente con 8 clientes simultáneos.

- Resultado: **OK**
	- `AUTH OK`: 8/8
	- `JOIN/CREATE OK`: 8/8
	- `Errores`: 0

Mejoras aplicadas en servidor:
1. Parseo estricto de enteros en `JOIN`, `MOVE`, `ATTACK`, `DEFEND`.
2. Prevención de comportamiento ambiguo por `atoi` en entradas mal formadas.
3. Control explícito de estado al crear/unirse sala (`already in room`).
4. Salida limpia de sala al cambiar de sala, notificando `PLAYER_LEAVE`.

---

## 12) Robustez ante desconexión abrupta en broadcast

Se añadió protección a nivel de proceso para evitar caída por `SIGPIPE` cuando el
servidor envía (`send`) hacia un socket ya cerrado por un cliente.

Implementación:
- En el arranque del servidor se configura `signal(SIGPIPE, SIG_IGN)`.

Resultado de la validación: **OK (0 fallos)**

Validación obtenida:
1. Defensor se desconecta abruptamente sin `QUIT`.
2. Atacante sigue ejecutando acciones con respuestas correctas.
3. El servidor continúa aceptando nuevas conexiones y respondiendo.

---

## 13) Integración HTTP de flujo completo

Se fortaleció el servidor web para que no solo renderice UI, sino que ejecute
flujo real con los servicios backend.

Mejoras implementadas:
1. `GET /health` con diagnóstico por servicio (`juego`, `identidad`) y latencia.
2. `POST /crear` ejecuta secuencia real contra servidor de juego (`AUTH` + `CREATE_ROOM`).
3. `POST /unirse` ejecuta secuencia real contra servidor de juego (`AUTH` + `JOIN`).
4. Corrección de conexión por DNS usando todas las direcciones retornadas por
	 `getaddrinfo` (evita fallos IPv6/IPv4 por tomar solo la primera entrada).

Resultado de la validación HTTP: **OK (0 fallos)**
	- `GET /health`: PASS
	- `GET /api/salas`: PASS
	- `POST /crear`: PASS
	- `POST /unirse`: PASS

---

## 14) Validación integral automática (entorno local)

Hicimos una corrida integral en entorno local cubriendo protocolo TCP, flujo de juego y flujo HTTP.

Resultado global: **OK**

Cobertura ejecutada en una sola corrida:
1. Smoke protocolo TCP
2. Flujo integrado ataque/defensa
3. Timeout de mitigación
4. Flujo HTTP completo

Este resultado sirve como cierre de validación funcional en entorno local.

---


## 15) Verificación final de cierre (local)

Repetimos la validación integral completa en local para confirmar estabilidad.

Resultado: **OK completo**

Incluye PASS en:
1. Smoke protocolo TCP
2. Flujo integrado ataque/defensa
3. Timeout de mitigación
4. Flujo HTTP (`/health`, `/api/salas`, `/crear`, `/unirse`)

Con esto, la base funcional local queda validada de extremo a extremo.
