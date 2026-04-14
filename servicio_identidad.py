"""
servicio_identidad.py — Servicio de identidad independiente
Puerto: 9090
Protocolo interno (TCP, texto):
  Cliente envía: VALIDAR <usuario> <password>\n
  Responde:      OK <ROL>\n  |  ERROR_CREDENTIALS\n  |  ERROR_FORMATO\n
"""

import socket
import threading
import os

# ─── Base de usuarios (en producción sería una BD real) ───────────
USUARIOS = {
    "alice": {"pass": "alice123", "rol": "ATACANTE"},
    "bob": {"pass": "bob456", "rol": "DEFENSOR"},
    "carlos": {"pass": "carlos789", "rol": "ATACANTE"},
    "diana": {"pass": "diana000", "rol": "DEFENSOR"},
    "admin": {"pass": "1234", "rol": "ATACANTE"},
    "pepe": {"pass": "abcd", "rol": "DEFENSOR"},
}

PUERTO = int(os.getenv("CSGS_IDENTITY_PORT", "9090"))


def manejar_peticion(conn: socket.socket, addr):
    ip, puerto = addr
    print(f"[IDENTIDAD] Conexión desde {ip}:{puerto}")
    try:
        datos = conn.recv(1024).decode("utf-8").strip()
        if not datos:
            return

        print(f"[IDENTIDAD] Consulta: {datos}")
        partes = datos.split()

        if len(partes) != 3 or partes[0] != "VALIDAR":
            conn.sendall(b"ERROR_FORMATO\n")
            return

        usuario = partes[1]
        password = partes[2]

        if usuario in USUARIOS and USUARIOS[usuario]["pass"] == password:
            rol = USUARIOS[usuario]["rol"]
            respuesta = f"OK {rol}\n"
            print(f"[IDENTIDAD] OK → {usuario} ({rol})")
        else:
            respuesta = "ERROR_CREDENTIALS\n"
            print(f"[IDENTIDAD] FAIL → {usuario}")

        conn.sendall(respuesta.encode("utf-8"))

    except Exception as e:
        print(f"[IDENTIDAD] Error: {e}")
    finally:
        conn.close()


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PUERTO))
    srv.listen(10)
    print(f"[IDENTIDAD] Escuchando en el puerto {PUERTO}...")
    print(f"[IDENTIDAD] Usuarios registrados: {list(USUARIOS.keys())}")

    while True:
        try:
            conn, addr = srv.accept()
            t = threading.Thread(
                target=manejar_peticion, args=(conn, addr), daemon=True
            )
            t.start()
        except KeyboardInterrupt:
            break

    srv.close()
    print("[IDENTIDAD] Servicio detenido.")


if __name__ == "__main__":
    main()
