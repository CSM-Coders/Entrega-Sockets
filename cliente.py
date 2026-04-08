import socket
import threading
import tkinter as tk
from tkinter import scrolledtext, messagebox


class ClienteJuego:
    def __init__(self, root):
        self.root = root
        self.root.title("Cliente Python - Ciberseguridad")
        self.sock = None

        # --- PANEL DE CONEXIÓN ---
        frame_conexion = tk.Frame(root, padx=10, pady=10)
        frame_conexion.pack(fill=tk.X)

        tk.Label(frame_conexion, text="IP Servidor:").pack(side=tk.LEFT)
        self.entry_ip = tk.Entry(frame_conexion, width=15)
        self.entry_ip.insert(0, "127.0.0.1")
        self.entry_ip.pack(side=tk.LEFT, padx=5)

        tk.Label(frame_conexion, text="Puerto:").pack(side=tk.LEFT)
        self.entry_puerto = tk.Entry(frame_conexion, width=8)
        self.entry_puerto.insert(0, "8080")
        self.entry_puerto.pack(side=tk.LEFT, padx=5)

        self.btn_conectar = tk.Button(
            frame_conexion, text="Conectar", command=self.conectar
        )
        self.btn_conectar.pack(side=tk.LEFT, padx=5)

        # --- PANEL DEL PLANO (MAPA DEL JUEGO) ---
        frame_mapa = tk.Frame(root, padx=10, pady=5)
        frame_mapa.pack()
        tk.Label(frame_mapa, text="Plano del Juego").pack()

        self.canvas = tk.Canvas(
            frame_mapa,
            width=400,
            height=300,
            bg="white",
            highlightthickness=1,
            highlightbackground="black",
        )
        self.canvas.pack()
        self.dibujar_cuadricula()

        # --- PANEL DE LOGS Y COMANDOS ---
        frame_log = tk.Frame(root, padx=10, pady=10)
        frame_log.pack(fill=tk.BOTH, expand=True)

        self.txt_log = scrolledtext.ScrolledText(
            frame_log, height=10, state=tk.DISABLED
        )
        self.txt_log.pack(fill=tk.BOTH, expand=True)

        frame_envio = tk.Frame(root, padx=10, pady=10)
        frame_envio.pack(fill=tk.X)
        self.entry_comando = tk.Entry(frame_envio)
        self.entry_comando.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.entry_comando.bind("<Return>", lambda event: self.enviar_comando())

        tk.Button(frame_envio, text="Enviar Comando", command=self.enviar_comando).pack(
            side=tk.RIGHT, padx=5
        )

    def dibujar_cuadricula(self):
        for i in range(0, 400, 40):
            self.canvas.create_line(i, 0, i, 300, fill="lightgray")
        for i in range(0, 300, 30):
            self.canvas.create_line(0, i, 400, i, fill="lightgray")

    def registrar_log(self, mensaje):
        self.txt_log.config(state=tk.NORMAL)
        self.txt_log.insert(tk.END, mensaje + "\n")
        self.txt_log.see(tk.END)
        self.txt_log.config(state=tk.DISABLED)

    def conectar(self):
        ip = self.entry_ip.get()
        puerto = int(self.entry_puerto.get())
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((ip, puerto))
            self.registrar_log(f"[SISTEMA] Conectado a {ip}:{puerto}")
            self.btn_conectar.config(state=tk.DISABLED)

            hilo_escucha = threading.Thread(target=self.escuchar_servidor, daemon=True)
            hilo_escucha.start()
        except Exception as e:
            messagebox.showerror("Error", f"No se pudo conectar: {e}")

    def escuchar_servidor(self):
        while True:
            try:
                datos = self.sock.recv(1024).decode("utf-8")
                if not datos:
                    break

                mensajes = datos.strip().split("\n")
                for mensaje in mensajes:
                    if not mensaje:
                        continue
                    self.registrar_log(f"[SERVIDOR] {mensaje}")

                    # --- INTERPRETAR Y DIBUJAR ---
                    partes = mensaje.split()
                    if len(partes) >= 4 and partes[0] == "POS":
                        usuario = partes[1]
                        try:
                            x = int(partes[2])
                            y = int(partes[3])
                            self.actualizar_jugador(usuario, x, y)
                        except ValueError:
                            pass  # Ignorar si las coordenadas fallan

            except Exception as e:
                print(f"Error de lectura: {e}")
                break

        self.registrar_log("[SISTEMA] Desconectado del servidor.")
        self.btn_conectar.config(state=tk.NORMAL)

    def actualizar_jugador(self, usuario, x, y):
        def actualizar_ui():
            # Borramos la posición anterior que tuviera ESTE usuario específico
            self.canvas.delete(usuario)

            # Dibujamos un círculo azul para el jugador
            radio = 10
            self.canvas.create_oval(
                x - radio, y - radio, x + radio, y + radio, fill="blue", tags=usuario
            )
            # Dibujamos su nombre encima
            self.canvas.create_text(x, y - 15, text=usuario, tags=usuario, fill="black")

        # Ejecutar de forma segura en la interfaz gráfica
        self.root.after(0, actualizar_ui)

    def enviar_comando(self):
        if self.sock:
            comando = self.entry_comando.get().strip()
            if comando:
                self.sock.sendall((comando + "\n").encode("utf-8"))
                self.registrar_log(f"[YO] {comando}")
                self.entry_comando.delete(0, tk.END)


if __name__ == "__main__":
    root = tk.Tk()
    app = ClienteJuego(root)
    root.mainloop()
