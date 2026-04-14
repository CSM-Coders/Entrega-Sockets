"""
cliente.py — Cliente del juego de ciberseguridad (Python / tkinter)
Protocolo CSGS sobre TCP
Uso: python3 cliente.py [host] [puerto]
"""

import socket
import threading
import tkinter as tk
from tkinter import scrolledtext, messagebox, simpledialog
import sys

# ─── Dimensiones del mapa (deben coincidir con el servidor) ───────
ANCHO = 400
ALTO = 300
RADIO_RECURSO = 15

# ─── Posiciones de recursos (SOLO visibles para defensores;
#     el servidor las envía via RESOURCE_INFO) ────────────────────
COLORES_ROL = {
    "ATACANTE": "#e74c3c",
    "DEFENSOR": "#2ecc71",
    "YO": "#3498db",
}


class ClienteJuego:
    def __init__(self, root, host: str, port: int):
        self.root = root
        self.host = host
        self.port = port
        self.sock = None
        self.rol = ""
        self.nombre = ""
        self.sala_id = None

        # Posiciones de otros jugadores en pantalla {nombre: (x,y,rol)}
        self.jugadores = {}
        # Recursos críticos conocidos {id: {nombre,x,y,estado}}
        self.recursos = {}

        root.title("Cliente Juego — Ciberseguridad")
        root.configure(bg="#0d1117")
        root.resizable(False, False)
        self._build_ui()

    # ──────────────────────────────────────────────────────────────
    #  CONSTRUCCIÓN DE LA INTERFAZ
    # ──────────────────────────────────────────────────────────────
    def _build_ui(self):
        PAD = dict(padx=6, pady=4)
        FG = "#c9d1d9"
        BG = "#0d1117"
        BG2 = "#161b22"

        # ── Barra superior: conexión ──
        top = tk.Frame(self.root, bg=BG2, relief="flat", bd=1)
        top.pack(fill=tk.X, **PAD)

        tk.Label(top, text="Host:", bg=BG2, fg=FG).pack(side=tk.LEFT, padx=4)
        self.e_host = tk.Entry(
            top, width=14, bg="#21262d", fg=FG, insertbackground=FG, relief="flat"
        )
        self.e_host.insert(0, self.host)
        self.e_host.pack(side=tk.LEFT)

        tk.Label(top, text="Puerto:", bg=BG2, fg=FG).pack(side=tk.LEFT, padx=4)
        self.e_port = tk.Entry(
            top, width=6, bg="#21262d", fg=FG, insertbackground=FG, relief="flat"
        )
        self.e_port.insert(0, str(self.port))
        self.e_port.pack(side=tk.LEFT)

        self.btn_conectar = tk.Button(
            top,
            text="Conectar",
            bg="#238636",
            fg="white",
            activebackground="#2ea043",
            relief="flat",
            command=self.conectar,
        )
        self.btn_conectar.pack(side=tk.LEFT, padx=6)

        self.lbl_estado = tk.Label(top, text="⚫ Desconectado", bg=BG2, fg="#f85149")
        self.lbl_estado.pack(side=tk.RIGHT, padx=8)

        # ── Mapa del juego ──
        mapa_frame = tk.Frame(self.root, bg=BG, relief="solid", bd=1)
        mapa_frame.pack(**PAD)

        self.lbl_sala = tk.Label(
            mapa_frame,
            text="Plano del centro de datos",
            bg=BG,
            fg="#58a6ff",
            font=("Arial", 10, "bold"),
        )
        self.lbl_sala.pack()

        self.canvas = tk.Canvas(
            mapa_frame,
            width=ANCHO,
            height=ALTO,
            bg="#0d1117",
            highlightthickness=1,
            highlightbackground="#30363d",
        )
        self.canvas.pack()
        self._draw_grid()
        self._bind_mapa()

        # ── Panel derecho: controles ──
        ctrl = tk.Frame(self.root, bg=BG)
        ctrl.pack(fill=tk.X, **PAD)

        # Auth
        auth_f = tk.LabelFrame(
            ctrl, text=" Autenticación ", bg=BG2, fg="#58a6ff", relief="flat"
        )
        auth_f.pack(fill=tk.X, pady=2)

        tk.Label(auth_f, text="Usuario:", bg=BG2, fg=FG).grid(
            row=0, column=0, sticky="w", padx=4
        )
        self.e_usr = tk.Entry(
            auth_f, width=14, bg="#21262d", fg=FG, insertbackground=FG, relief="flat"
        )
        self.e_usr.grid(row=0, column=1, padx=4)

        tk.Label(auth_f, text="Password:", bg=BG2, fg=FG).grid(
            row=1, column=0, sticky="w", padx=4
        )
        self.e_pwd = tk.Entry(
            auth_f,
            width=14,
            show="*",
            bg="#21262d",
            fg=FG,
            insertbackground=FG,
            relief="flat",
        )
        self.e_pwd.grid(row=1, column=1, padx=4)

        tk.Button(
            auth_f,
            text="Iniciar sesión",
            bg="#1f6feb",
            fg="white",
            relief="flat",
            command=self.cmd_auth,
        ).grid(row=2, column=0, columnspan=2, pady=4, padx=4, sticky="ew")

        # Salas
        sala_f = tk.LabelFrame(
            ctrl, text=" Salas ", bg=BG2, fg="#58a6ff", relief="flat"
        )
        sala_f.pack(fill=tk.X, pady=2)

        tk.Button(
            sala_f,
            text="📋 Listar salas",
            bg="#21262d",
            fg=FG,
            relief="flat",
            command=self.cmd_list_rooms,
        ).pack(side=tk.LEFT, padx=4, pady=4)
        tk.Button(
            sala_f,
            text="➕ Crear sala",
            bg="#21262d",
            fg=FG,
            relief="flat",
            command=self.cmd_create_room,
        ).pack(side=tk.LEFT, padx=4, pady=4)

        tk.Label(sala_f, text="ID sala:", bg=BG2, fg=FG).pack(side=tk.LEFT, padx=4)
        self.e_sala = tk.Entry(
            sala_f, width=4, bg="#21262d", fg=FG, insertbackground=FG, relief="flat"
        )
        self.e_sala.pack(side=tk.LEFT)
        tk.Button(
            sala_f,
            text="Unirse",
            bg="#21262d",
            fg=FG,
            relief="flat",
            command=self.cmd_join,
        ).pack(side=tk.LEFT, padx=4)

        # Acciones de juego
        game_f = tk.LabelFrame(
            ctrl, text=" Acciones del juego ", bg=BG2, fg="#58a6ff", relief="flat"
        )
        game_f.pack(fill=tk.X, pady=2)

        tk.Label(game_f, text="MOVE x,y:", bg=BG2, fg=FG).grid(
            row=0, column=0, padx=4, sticky="w"
        )
        self.e_move = tk.Entry(
            game_f, width=10, bg="#21262d", fg=FG, insertbackground=FG, relief="flat"
        )
        self.e_move.grid(row=0, column=1, padx=4)
        tk.Button(
            game_f,
            text="Mover",
            bg="#21262d",
            fg=FG,
            relief="flat",
            command=self.cmd_move_entry,
        ).grid(row=0, column=2, padx=4, pady=2)

        tk.Label(game_f, text="Recurso ID:", bg=BG2, fg=FG).grid(
            row=1, column=0, padx=4, sticky="w"
        )
        self.e_recurso = tk.Entry(
            game_f, width=4, bg="#21262d", fg=FG, insertbackground=FG, relief="flat"
        )
        self.e_recurso.grid(row=1, column=1, padx=4, sticky="w")

        self.btn_attack = tk.Button(
            game_f,
            text="⚔ ATTACK",
            bg="#da3633",
            fg="white",
            relief="flat",
            command=self.cmd_attack,
            state=tk.DISABLED,
        )
        self.btn_attack.grid(row=1, column=2, padx=4, pady=2)

        self.btn_defend = tk.Button(
            game_f,
            text="🛡 DEFEND",
            bg="#1a7f37",
            fg="white",
            relief="flat",
            command=self.cmd_defend,
            state=tk.DISABLED,
        )
        self.btn_defend.grid(row=1, column=3, padx=4, pady=2)

        # Comando manual
        cmd_f = tk.Frame(ctrl, bg=BG)
        cmd_f.pack(fill=tk.X, pady=2)
        self.e_cmd = tk.Entry(
            cmd_f, bg="#21262d", fg=FG, insertbackground=FG, relief="flat"
        )
        self.e_cmd.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.e_cmd.bind("<Return>", lambda _: self.enviar_raw(self.e_cmd.get()))
        tk.Button(
            cmd_f,
            text="Enviar",
            bg="#21262d",
            fg=FG,
            relief="flat",
            command=lambda: self.enviar_raw(self.e_cmd.get()),
        ).pack(side=tk.RIGHT, padx=4)

        # Log
        self.txt_log = scrolledtext.ScrolledText(
            self.root,
            height=8,
            bg=BG2,
            fg=FG,
            insertbackground=FG,
            state=tk.DISABLED,
            font=("Courier", 9),
            relief="flat",
        )
        self.txt_log.pack(fill=tk.X, **PAD)

    # ──────────────────────────────────────────────────────────────
    #  MAPA
    # ──────────────────────────────────────────────────────────────
    def _draw_grid(self):
        for x in range(0, ANCHO, 40):
            self.canvas.create_line(x, 0, x, ALTO, fill="#1c2128")
        for y in range(0, ALTO, 30):
            self.canvas.create_line(0, y, ANCHO, y, fill="#1c2128")
        # Borde del mapa
        self.canvas.create_rectangle(
            0, 0, ANCHO - 1, ALTO - 1, outline="#58a6ff", width=2
        )

    def _bind_mapa(self):
        """Clic en mapa → enviar MOVE"""
        self.canvas.bind("<Button-1>", self._on_canvas_click)

    def _on_canvas_click(self, event):
        if self.sock:
            self.enviar(f"MOVE {event.x} {event.y}")

    def _redraw_map(self):
        """Redibuja todos los elementos del mapa."""
        self.canvas.delete("jugador", "recurso", "recurso_alert")

        # Recursos críticos
        for rid, r in self.recursos.items():
            x, y = r["x"], r["y"]
            estado = r.get("estado", "normal")
            color = (
                "#f85149"
                if estado == "atacado"
                else "#2ea043" if estado == "mitigado" else "#e3b341"
            )
            self.canvas.create_oval(
                x - RADIO_RECURSO,
                y - RADIO_RECURSO,
                x + RADIO_RECURSO,
                y + RADIO_RECURSO,
                fill=color,
                outline="white",
                width=2,
                tags="recurso",
            )
            self.canvas.create_text(
                x,
                y + RADIO_RECURSO + 10,
                text=f"#{rid} {r['nombre']}",
                fill="white",
                font=("Arial", 8),
                tags="recurso",
            )

        # Otros jugadores
        for nombre, info in self.jugadores.items():
            if nombre == self.nombre:
                continue
            jx, jy, jrol = info
            color = COLORES_ROL.get(jrol, "#888888")
            self.canvas.create_oval(
                jx - 8,
                jy - 8,
                jx + 8,
                jy + 8,
                fill=color,
                outline="white",
                tags="jugador",
            )
            self.canvas.create_text(
                jx,
                jy - 14,
                text=nombre,
                fill=color,
                font=("Arial", 8, "bold"),
                tags="jugador",
            )

        # Mi jugador (encima de todo)
        if self.nombre in self.jugadores:
            mx, my, _ = self.jugadores[self.nombre]
            self.canvas.create_oval(
                mx - 10,
                my - 10,
                mx + 10,
                my + 10,
                fill=COLORES_ROL["YO"],
                outline="white",
                width=2,
                tags="jugador",
            )
            self.canvas.create_text(
                mx,
                my - 16,
                text=f"YO {self.nombre}",
                fill=COLORES_ROL["YO"],
                font=("Arial", 9, "bold"),
                tags="jugador",
            )

    # ──────────────────────────────────────────────────────────────
    #  RED
    # ──────────────────────────────────────────────────────────────
    def conectar(self):
        host = self.e_host.get().strip()
        port = int(self.e_port.get().strip())
        try:
            # Python resuelve DNS automáticamente con el hostname
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((host, port))
            self.log(f"[SISTEMA] Conectado a {host}:{port}")
            self.btn_conectar.config(state=tk.DISABLED)
            self.lbl_estado.config(text="🟢 Conectado", fg="#2ea043")
            t = threading.Thread(target=self._escuchar, daemon=True)
            t.start()
        except Exception as e:
            messagebox.showerror("Error de conexión", str(e))

    def _escuchar(self):
        buf = ""
        while True:
            try:
                chunk = self.sock.recv(1024).decode("utf-8")
                if not chunk:
                    break
                buf += chunk
                while "\n" in buf:
                    linea, buf = buf.split("\n", 1)
                    if linea:
                        self.root.after(0, self._procesar, linea)
            except Exception:
                break
        self.root.after(0, self._on_disconnect)

    def _on_disconnect(self):
        self.log("[SISTEMA] Desconectado del servidor.")
        self.btn_conectar.config(state=tk.NORMAL)
        self.lbl_estado.config(text="⚫ Desconectado", fg="#f85149")
        self.sock = None

    def enviar(self, msg: str):
        if self.sock:
            try:
                self.sock.sendall((msg + "\n").encode("utf-8"))
                self.log(f"[YO → ] {msg}")
            except Exception as e:
                self.log(f"[ERROR] {e}")

    def enviar_raw(self, msg: str):
        msg = msg.strip()
        if msg:
            self.enviar(msg)
            self.e_cmd.delete(0, tk.END)

    # ──────────────────────────────────────────────────────────────
    #  PROCESAMIENTO DE MENSAJES DEL SERVIDOR
    # ──────────────────────────────────────────────────────────────
    def _procesar(self, msg: str):
        self.log(f"[← SRV] {msg}")
        partes = msg.split()
        if not partes:
            return
        cmd = partes[0]

        if cmd == "OK_AUTH" and len(partes) >= 3:
            rol = partes[2]
            if rol not in ("ATACANTE", "DEFENSOR"):
                self.nombre = ""
                self.rol = ""
                self.btn_attack.config(state=tk.DISABLED)
                self.btn_defend.config(state=tk.DISABLED)
                messagebox.showerror(
                    "Respuesta inválida",
                    f"Rol inválido recibido en AUTH: '{rol}'.",
                )
                return
            self.nombre = partes[1]
            self.rol = rol
            self._configurar_rol()

        elif cmd == "ERR_AUTH":
            # Evita que un intento fallido de login parezca exitoso.
            self.nombre = ""
            self.rol = ""
            self.btn_attack.config(state=tk.DISABLED)
            self.btn_defend.config(state=tk.DISABLED)
            detalle = (
                " ".join(partes[1:]) if len(partes) > 1 else "Error de autenticación"
            )
            messagebox.showerror("Autenticación fallida", detalle)

        elif cmd == "POS" and len(partes) >= 4:
            nombre = partes[1]
            try:
                x, y = int(partes[2]), int(partes[3])
                rol = partes[4] if len(partes) >= 5 else ""
                self.jugadores[nombre] = (x, y, rol)
                self._redraw_map()
            except ValueError:
                pass

        elif cmd == "RESOURCE_INFO" and len(partes) >= 5:
            # RESOURCE_INFO <id> <nombre> <x> <y>
            try:
                rid = int(partes[1])
                rnombre = partes[2]
                rx, ry = int(partes[3]), int(partes[4])
                self.recursos[rid] = {
                    "nombre": rnombre,
                    "x": rx,
                    "y": ry,
                    "estado": "normal",
                }
                self._redraw_map()
            except ValueError:
                pass

        elif cmd == "RESOURCE_FOUND" and len(partes) >= 5:
            # RESOURCE_FOUND <id> <nombre> <x> <y>
            try:
                rid = int(partes[1])
                rnombre = partes[2]
                rx, ry = int(partes[3]), int(partes[4])
                estado_actual = self.recursos.get(rid, {}).get("estado", "normal")
                self.recursos[rid] = {
                    "nombre": rnombre,
                    "x": rx,
                    "y": ry,
                    "estado": estado_actual,
                }
                self._redraw_map()
                self.e_recurso.delete(0, tk.END)
                self.e_recurso.insert(0, str(rid))
                messagebox.showinfo(
                    "¡Recurso encontrado!",
                    f"Encontraste: #{rid} {rnombre}\n"
                    f"Se cargó automáticamente el ID {rid} para ATTACK.",
                )
            except ValueError:
                pass

        elif cmd == "ATTACK_ALERT" and len(partes) >= 3:
            rid = partes[2]
            atacante = partes[1]
            if int(rid) in self.recursos:
                self.recursos[int(rid)]["estado"] = "atacado"
                self._redraw_map()
            messagebox.showwarning(
                "⚠ ATAQUE DETECTADO",
                f"¡{atacante} está atacando el recurso {rid}!\n" f"Envía: DEFEND {rid}",
            )

        elif cmd == "DEFEND_SUCCESS" and len(partes) >= 3:
            rid = int(partes[2])
            if rid in self.recursos:
                self.recursos[rid]["estado"] = "mitigado"
                self._redraw_map()

        elif cmd == "EVENT" and len(partes) >= 2:
            tipo = partes[1]
            if tipo == "GAME_START" and len(partes) >= 5:
                self.log(
                    f"[PARTIDA] Inició sala {partes[2]} "
                    f"(ATACANTES={partes[3]}, DEFENSORES={partes[4]})"
                )
                messagebox.showinfo(
                    "Partida iniciada",
                    f"Sala {partes[2]} iniciada con "
                    f"{partes[3]} atacante(s) y {partes[4]} defensor(es).",
                )
            elif tipo == "GAME_WAITING" and len(partes) >= 5:
                self.log(
                    f"[PARTIDA] Sala {partes[2]} en espera "
                    f"(ATACANTES={partes[3]}, DEFENSORES={partes[4]})"
                )
            elif tipo == "GAME_END" and len(partes) >= 4:
                ganador = partes[3]
                self.log(f"[PARTIDA] Finalizada sala {partes[2]} — Ganan {ganador}")
                messagebox.showinfo(
                    "Partida finalizada",
                    f"Sala {partes[2]} finalizada.\nGanan: {ganador}",
                )

        elif cmd == "OK_JOIN" and len(partes) >= 2:
            self.sala_id = partes[1]
            # Limpiar recursos y jugadores de partida anterior
            self.recursos.clear()
            self.jugadores.clear()
            self._redraw_map()
            self.lbl_sala.config(text=f"Sala {self.sala_id} — Rol: {self.rol}")

        elif cmd == "OK_CREATE" and len(partes) >= 2:
            self.sala_id = partes[1]
            # Limpiar recursos y jugadores de partida anterior
            self.recursos.clear()
            self.jugadores.clear()
            self._redraw_map()
            self.lbl_sala.config(text=f"Sala {self.sala_id} — Rol: {self.rol}")

        elif cmd.startswith("ERR_"):
            detalle = " ".join(partes[1:]) if len(partes) > 1 else msg
            messagebox.showwarning("Respuesta del servidor", detalle)

        elif cmd == "PLAYER_JOIN":
            pass  # ya se muestra en el log

        elif cmd == "PLAYER_LEAVE" and len(partes) >= 2:
            self.jugadores.pop(partes[1], None)
            self._redraw_map()

    def _configurar_rol(self):
        self.root.title(f"[{self.rol}] {self.nombre} — Juego de Ciberseguridad")
        if self.rol == "ATACANTE":
            self.btn_attack.config(state=tk.NORMAL)
            self.btn_defend.config(state=tk.DISABLED)
            self.log("[ROL] Eres ATACANTE 🔴 — Muévete para encontrar recursos")
        else:
            self.btn_defend.config(state=tk.NORMAL)
            self.btn_attack.config(state=tk.DISABLED)
            self.log("[ROL] Eres DEFENSOR 🟢 — Recibirás alertas de ataque")

    # ──────────────────────────────────────────────────────────────
    #  COMANDOS DE JUEGO
    # ──────────────────────────────────────────────────────────────
    def cmd_auth(self):
        usr = self.e_usr.get().strip()
        pwd = self.e_pwd.get().strip()
        if usr and pwd:
            self.enviar(f"AUTH {usr} {pwd}")

    def cmd_list_rooms(self):
        self.enviar("LIST_ROOMS")

    def cmd_create_room(self):
        self.enviar("CREATE_ROOM")

    def cmd_join(self):
        sid = self.e_sala.get().strip()
        if sid:
            self.enviar(f"JOIN {sid}")

    def cmd_move_entry(self):
        txt = self.e_move.get().strip().replace(",", " ")
        partes = txt.split()
        if len(partes) == 2:
            self.enviar(f"MOVE {partes[0]} {partes[1]}")

    def cmd_attack(self):
        rid = self.e_recurso.get().strip()
        if rid:
            self.enviar(f"ATTACK {rid}")

    def cmd_defend(self):
        rid = self.e_recurso.get().strip()
        if rid:
            self.enviar(f"DEFEND {rid}")

    # ──────────────────────────────────────────────────────────────
    #  LOG
    # ──────────────────────────────────────────────────────────────
    def log(self, msg: str):
        self.txt_log.config(state=tk.NORMAL)
        self.txt_log.insert(tk.END, msg + "\n")
        self.txt_log.see(tk.END)
        self.txt_log.config(state=tk.DISABLED)


# ─── Entry point ─────────────────────────────────────────────────
if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

    root = tk.Tk()
    app = ClienteJuego(root, host, port)
    root.mainloop()
