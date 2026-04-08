import socket
import threading

# Base de datos simulada en memoria (Usuario: {Password, Rol})
USUARIOS = {
    "admin": {"pass": "1234", "rol": "ATACANTE"},
    "pepe": {"pass": "abcd", "rol": "DEFENSOR"},
    "maria": {"pass": "0000", "rol": "ATACANTE"},
}


def manejar_peticion(conn, addr):
    print(f"[IDENTIDAD] Conexión desde {addr}")
    try:
        datos = conn.recv(1024).decode("utf-8").strip()
        if not datos:
            return

        print(f"[IDENTIDAD] Verificando: {datos}")
        partes = datos.split(" ")

        # Protocolo interno: VALIDAR usuario password
        if len(partes) == 3 and partes[0] == "VALIDAR":
            usuario = partes[1]
            password = partes[2]

            if usuario in USUARIOS and USUARIOS[usuario]["pass"] == password:
                rol = USUARIOS[usuario]["rol"]
                respuesta = f"OK {rol}\n"
            else:
                respuesta = "ERROR_CREDENTIALS\n"
        else:
            respuesta = "ERROR_FORMATO\n"

        conn.sendall(respuesta.encode("utf-8"))
    except Exception as e:
        print(f"[IDENTIDAD] Error: {e}")
    finally:
        conn.close()


def iniciar_servicio():
    puerto = 9090
    servidor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # Permite reutilizar el puerto rápido si lo cierras
    servidor.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    servidor.bind(("0.0.0.0", puerto))
    servidor.listen(5)

    print(f"Servicio de Identidad escuchando en el puerto {puerto}...")

    while True:
        conn, addr = servidor.accept()
        hilo = threading.Thread(target=manejar_peticion, args=(conn, addr))
        hilo.start()


if __name__ == "__main__":
    iniciar_servicio()
