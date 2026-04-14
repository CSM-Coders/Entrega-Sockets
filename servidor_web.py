"""
servidor_web.py — Servidor HTTP básico para gestión de partidas
Puerto: 8000
Rutas:
  GET  /              → HTML con lista de salas activas
  GET  /api/salas     → JSON con salas
  POST /unirse        → Formulario de login + unirse a sala

El servidor interpreta correctamente cabeceras HTTP y devuelve
códigos de estado adecuados (200, 404, 405, 500).
"""

from http.server import BaseHTTPRequestHandler, HTTPServer
import html
import json
import socket
import time
import urllib.parse

JUEGO_HOST = "localhost"  # Nombre de dominio — sin IP hardcodeada
JUEGO_PORT = 8080
IDENTIDAD_HOST = "localhost"
IDENTIDAD_PORT = 9090


# ─── Consultar salas al servidor de juego ────────────────────────
def consultar_salas():
    """Abre una conexión TCP al servidor de juego y pide LIST_ROOMS."""
    try:
        s = conectar_tcp_por_dns(JUEGO_HOST, JUEGO_PORT, timeout=3)
        s.sendall(b"LIST_ROOMS\n")
        resp = s.recv(1024).decode("utf-8").strip()
        s.close()
        return resp
    except Exception as e:
        return f"ERROR {e}"


def conectar_tcp_por_dns(host: str, port: int, timeout: float = 3.0):
    """Resuelve host por DNS y conecta por TCP probando direcciones candidatas."""
    info = socket.getaddrinfo(host, port, socket.AF_UNSPEC, socket.SOCK_STREAM)
    ultimo_error = None
    for familia, tipo, proto, _, addr in info:
        s = socket.socket(familia, tipo, proto)
        s.settimeout(timeout)
        try:
            s.connect(addr)
            return s
        except Exception as e:
            ultimo_error = e
            s.close()
    raise ConnectionError(f"No se pudo conectar a {host}:{port}: {ultimo_error}")


def health_check_tcp(host: str, port: int, probe: bytes | None = None):
    """Devuelve estado de salud de un servicio TCP vía DNS."""
    inicio = time.time()
    try:
        s = conectar_tcp_por_dns(host, port, timeout=2.5)
        if probe:
            s.sendall(probe)
            _ = s.recv(1024)
        s.close()
        ms = int((time.time() - inicio) * 1000)
        return {"up": True, "latency_ms": ms, "error": None}
    except Exception as e:
        ms = int((time.time() - inicio) * 1000)
        return {"up": False, "latency_ms": ms, "error": str(e)}


def parsear_salas(resp: str):
    """Convierte 'ROOMS 2 0:3:ESPERANDO 1:10:JUGANDO' en lista de dicts."""
    salas = []
    if resp.startswith("NO_ROOMS") or resp.startswith("ERROR"):
        return salas
    partes = resp.split()
    if len(partes) < 2:
        return salas
    for token in partes[2:]:
        campos = token.split(":")
        if len(campos) == 3:
            salas.append(
                {
                    "id": campos[0],
                    "jugadores": campos[1],
                    "estado": campos[2],
                }
            )
    return salas


def validar_credenciales(usuario: str, password: str):
    """Valida usuario/password contra el servicio de identidad."""
    try:
        s = conectar_tcp_por_dns(IDENTIDAD_HOST, IDENTIDAD_PORT, timeout=3)
        s.sendall(f"VALIDAR {usuario} {password}\n".encode("utf-8"))
        resp = s.recv(1024).decode("utf-8").strip()
        s.close()

        if resp.startswith("OK "):
            return True, resp.split(" ", 1)[1], None
        return False, None, "Credenciales inválidas"
    except Exception as e:
        return False, None, f"Servicio de identidad no disponible: {e}"


def recv_lineas(sock: socket.socket, ventana_seg: float = 0.8):
    """Lee líneas recibidas durante una ventana corta de tiempo."""
    fin = time.time() + ventana_seg
    buf = ""
    lineas = []
    while time.time() < fin:
        try:
            data = sock.recv(4096).decode("utf-8", errors="replace")
            if not data:
                break
            buf += data
            while "\n" in buf:
                linea, buf = buf.split("\n", 1)
                linea = linea.strip()
                if linea:
                    lineas.append(linea)
        except socket.timeout:
            time.sleep(0.02)
        except Exception:
            break
    return lineas


def ejecutar_flujo_juego(usuario: str, password: str, sala_id: str | None):
    """Ejecuta flujo real contra servidor de juego: AUTH + (JOIN/CREATE_ROOM)."""
    transcript = []
    try:
        s = conectar_tcp_por_dns(JUEGO_HOST, JUEGO_PORT, timeout=3)
        s.settimeout(0.25)

        s.sendall(f"AUTH {usuario} {password}\n".encode("utf-8"))
        r = recv_lineas(s)
        transcript.extend([f"SRV {x}" for x in r])
        ok_auth = any(x.startswith("OK_AUTH ") for x in r)
        if not ok_auth:
            s.close()
            return False, transcript

        if sala_id:
            s.sendall(f"JOIN {sala_id}\n".encode("utf-8"))
        else:
            s.sendall(b"CREATE_ROOM\n")

        r2 = recv_lineas(s, ventana_seg=1.0)
        transcript.extend([f"SRV {x}" for x in r2])
        ok_union = any(x.startswith("OK_JOIN ") or x.startswith("OK_CREATE ") for x in r2)

        # Cierre limpio para no dejar sesión colgada
        s.sendall(b"QUIT\n")
        _ = recv_lineas(s, ventana_seg=0.3)
        s.close()
        return ok_union, transcript
    except Exception as e:
        transcript.append(f"ERROR {e}")
        return False, transcript


# ─── Handler HTTP ─────────────────────────────────────────────────
class GameHTTPHandler(BaseHTTPRequestHandler):

    # Silenciar log de acceso por defecto (usamos el propio)
    def log_message(self, fmt, *args):
        print(f"[HTTP] {self.address_string()} - {fmt % args}")

    def send_json(self, code: int, data):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_html(self, code: int, html: str):
        body = html.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # ── GET ───────────────────────────────────────────────────────
    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path

        if path == "/":
            resp = consultar_salas()
            salas = parsear_salas(resp)
            filas = ""
            for s in salas:
                filas += (
                    f"<tr>"
                    f"<td>Sala {s['id']}</td>"
                    f"<td>{s['jugadores']}</td>"
                    f"<td><span class='estado {s['estado'].lower()}'>"
                    f"{s['estado']}</span></td>"
                    f"<td>"
                    f"<form method='POST' action='/unirse' style='display:inline'>"
                    f"<input type='hidden' name='sala' value='{s['id']}'>"
                    f"<input type='text' name='usuario' placeholder='usuario' required style='width:110px;margin-right:6px'>"
                    f"<input type='password' name='password' placeholder='contraseña' required style='width:120px;margin-right:6px'>"
                    f"<button type='submit'>Unirse</button>"
                    f"</form>"
                    f"</td>"
                    f"</tr>"
                )
            if not filas:
                filas = "<tr><td colspan='4'>No hay partidas activas.</td></tr>"

            page_html = f"""<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <title>Juego de Ciberseguridad</title>
  <style>
    body {{ font-family: Arial, sans-serif; max-width: 700px; margin: 40px auto; background: #0d1117; color: #c9d1d9; }}
    h1   {{ color: #58a6ff; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ padding: 10px 14px; border: 1px solid #30363d; text-align: left; }}
    th   {{ background: #161b22; }}
    .waiting   {{ color: #3fb950; }}
    .in_game   {{ color: #f85149; }}
    .finished  {{ color: #a5a5a5; }}
    button {{ background: #238636; color: white; border: none; padding: 6px 14px; cursor: pointer; border-radius: 4px; }}
    button:hover {{ background: #2ea043; }}
    .login {{ margin-top: 30px; background: #161b22; padding: 20px; border-radius: 8px; border: 1px solid #30363d; }}
    input  {{ width: 100%; padding: 8px; margin: 6px 0; background: #0d1117; border: 1px solid #30363d; color: #c9d1d9; border-radius: 4px; }}
  </style>
</head>
<body>
  <h1>🔐 Juego de Ciberseguridad</h1>
  <h2>Partidas activas</h2>
  <table>
    <tr><th>Sala</th><th>Jugadores</th><th>Estado</th><th>Acción</th></tr>
    {filas}
  </table>

  <div class="login">
    <h3>Crear nueva partida</h3>
    <form method="POST" action="/crear">
      <label>Usuario: <input type="text" name="usuario" required></label>
      <label>Contraseña: <input type="password" name="password" required></label>
      <button type="submit">Crear partida</button>
    </form>
  </div>

  <p><small>Servidor: {JUEGO_HOST}:{JUEGO_PORT} | Respuesta cruda: {resp}</small></p>
</body>
</html>"""
            self.send_html(200, page_html)

        elif path == "/api/salas":
            resp = consultar_salas()
            salas = parsear_salas(resp)
            self.send_json(200, {"salas": salas, "raw": resp})

        elif path == "/health":
            h_juego = health_check_tcp(JUEGO_HOST, JUEGO_PORT, probe=b"LIST_ROOMS\n")
            h_ident = health_check_tcp(IDENTIDAD_HOST, IDENTIDAD_PORT, probe=b"VALIDAR x y\n")
            up = h_juego["up"] and h_ident["up"]
            code = 200 if up else 503
            self.send_json(
                code,
                {
                    "status": "ok" if up else "degraded",
                    "services": {
                        "juego": {
                            "host": JUEGO_HOST,
                            "port": JUEGO_PORT,
                            **h_juego,
                        },
                        "identidad": {
                            "host": IDENTIDAD_HOST,
                            "port": IDENTIDAD_PORT,
                            **h_ident,
                        },
                    },
                },
            )

        else:
            self.send_html(404, "<h1>404 - Página no encontrada</h1>")

    # ── POST ──────────────────────────────────────────────────────
    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8")
        params = urllib.parse.parse_qs(body)

        if path in ("/unirse", "/crear"):
            usuario = params.get("usuario", [""])[0]
            password = params.get("password", [""])[0]
            sala_id = params.get("sala", [None])[0]

            if not usuario or not password:
                self.send_html(
                    400,
                    "<h2>Faltan credenciales</h2>"
                    "<p>Debes indicar usuario y contraseña.</p>",
                )
                return

            ok, rol, err = validar_credenciales(usuario, password)
            if not ok:
                status = 401 if err == "Credenciales inválidas" else 503
                self.send_html(
                    status,
                    f"<h2>No autorizado</h2><p>{err}</p>"
                    "<p><a href='/'>Volver</a></p>",
                )
                return

            # Integración real del flujo web -> juego (AUTH + JOIN/CREATE_ROOM)
            ok_flujo, transcript = ejecutar_flujo_juego(usuario, password, sala_id)
            if not ok_flujo:
                self.send_html(
                    503,
                    "<h2>No fue posible completar flujo con servidor de juego</h2>"
                    "<p>Verifica disponibilidad del servidor de juego.</p>"
                    "<p><a href='/'>Volver</a></p>",
                )
                return

            info = (
                f"<p>Autenticado como <strong>{usuario}</strong> ({rol}).</p>"
                f"<p>Flujo validado contra servidor de juego: <strong>OK</strong>.</p>"
                f"<p>Puedes continuar en cliente Python/Java con:<br>"
                f"<code>AUTH {usuario} &lt;tu_contraseña&gt;</code><br>"
                f"{'<code>JOIN ' + sala_id + '</code>' if sala_id else '<code>CREATE_ROOM</code>'}</p>"
            )
            transcript_html = "<br>".join(html.escape(x) for x in transcript[:10])
            page_html = f"""<!DOCTYPE html>
<html lang="es"><head><meta charset="UTF-8"><title>Info</title>
<style>body{{font-family:Arial;max-width:600px;margin:40px auto;background:#0d1117;color:#c9d1d9}}
a{{color:#58a6ff}}</style></head>
<body><h2>✅ Instrucciones de conexión</h2>{info}
<h3>Traza del flujo web → juego</h3><p><small>{transcript_html}</small></p>
<a href="/">← Volver</a></body></html>"""
            self.send_html(200, page_html)
        else:
            self.send_html(405, "<h1>405 - Método no permitido</h1>")


# ─── Main ─────────────────────────────────────────────────────────
def main():
    puerto = 8000
    srv = HTTPServer(("0.0.0.0", puerto), GameHTTPHandler)
    print(f"[WEB] Servidor HTTP en http://localhost:{puerto}")
    print(f"[WEB] Consultando juego en {JUEGO_HOST}:{JUEGO_PORT} (DNS)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    srv.server_close()
    print("[WEB] Servidor detenido.")


if __name__ == "__main__":
    main()
