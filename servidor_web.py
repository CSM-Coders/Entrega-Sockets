from http.server import BaseHTTPRequestHandler, HTTPServer
import json

# Simulamos algunas partidas activas en memoria
partidas_activas = [
    {"id_sala": "SALA_01", "jugadores": 4, "estado": "En espera"},
    {"id_sala": "SALA_02", "jugadores": 10, "estado": "Jugando"},
]


class MiServidorHTTP(BaseHTTPRequestHandler):
    def do_GET(self):
        # Si entran desde el navegador web a la ruta principal '/'
        if self.path == "/":
            self.send_response(200)
            self.send_header("Content-type", "text/html; charset=utf-8")
            self.end_headers()

            # Generamos un HTML sencillo
            html = "<html><head><title>Salas Activas</title></head><body>"
            html += "<h1>Partidas de Ciberseguridad Disponibles</h1><ul>"
            for partida in partidas_activas:
                html += f"<li><b>{partida['id_sala']}</b> - Jugadores: {partida['jugadores']} - Estado: {partida['estado']}</li>"
            html += "</ul></body></html>"

            self.wfile.write(html.encode("utf-8"))

        # Si el cliente solicita los datos en formato JSON para leerlos programáticamente
        elif self.path == "/api/salas":
            self.send_response(200)
            self.send_header("Content-type", "application/json")
            self.end_headers()

            respuesta_json = json.dumps(partidas_activas)
            self.wfile.write(respuesta_json.encode("utf-8"))
        else:
            self.send_error(404, "Página no encontrada")


if __name__ == "__main__":
    puerto_web = 8000
    servidor = HTTPServer(("0.0.0.0", puerto_web), MiServidorHTTP)
    print(f"Servidor Web HTTP iniciado en http://localhost:{puerto_web}")
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        pass
    servidor.server_close()
    print("Servidor Web detenido.")
