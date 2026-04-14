import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.net.*;
import java.util.*;

/**
 * ClienteJuego.java — Cliente del juego de ciberseguridad (Java / Swing)
 * Protocolo CSGS sobre TCP
 * Uso: java ClienteJuego [host] [puerto]
 */
public class ClienteJuego extends JFrame {

    // ── Dimensiones del mapa ────────────────────────────────────
    private static final int ANCHO_MAPA    = 400;
    private static final int ALTO_MAPA     = 300;
    private static final int RADIO_RECURSO = 15;
    private static final int RADIO_JUGADOR = 10;

    // ── Red ─────────────────────────────────────────────────────
    private Socket        sock;
    private PrintWriter   salida;
    private BufferedReader entrada;

    // ── Estado del jugador ──────────────────────────────────────
    private String nombre  = "";
    private String rol     = "";
    private String salaId  = null;

    // ── Estado del juego ────────────────────────────────────────
    // nombre → int[]{x, y, colorIndex}
    private final Map<String, int[]>    jugadores = new HashMap<>();
    // id     → Object[]{nombre, x, y, estado}   estado: 0=normal,1=atacado,2=mitigado
    private final Map<Integer, Object[]> recursos  = new HashMap<>();

    // ── Widgets ─────────────────────────────────────────────────
    private MapPanel   panelMapa;
    private JTextArea  txtLog;
    private JTextField tfHost, tfPort, tfUsuario, tfSala, tfRecurso, tfCmd;
    private JPasswordField tfPass;
    private JButton    btnConectar, btnAuth, btnAttack, btnDefend;
    private JLabel     lblEstado, lblRol;

    // ────────────────────────────────────────────────────────────
    public ClienteJuego(String host, int port) {
        super("Cliente Juego — Ciberseguridad");
        setDefaultCloseOperation(EXIT_ON_CLOSE);
        setResizable(false);
        buildUI(host, port);
        pack();
        setLocationRelativeTo(null);
    }

    // ── Construcción de la UI ────────────────────────────────────
    private void buildUI(String host, int port) {
        Color BG   = new Color(13,  17, 23);
        Color BG2  = new Color(22,  27, 34);
        Color FG   = new Color(201, 209, 217);
        Color BLUE = new Color(88, 166, 255);

        getContentPane().setBackground(BG);
        setLayout(new BorderLayout(6, 6));

        // ── Panel NORTE: conexión ──
        JPanel norte = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 4));
        norte.setBackground(BG2);

        norte.add(label("Host:", FG));
        tfHost = input(host, 12, BG, FG);  norte.add(tfHost);
        norte.add(label("Puerto:", FG));
        tfPort = input(String.valueOf(port), 5, BG, FG); norte.add(tfPort);

        btnConectar = boton("Conectar", new Color(35, 134, 54), Color.WHITE);
        btnConectar.addActionListener(e -> conectar());
        norte.add(btnConectar);

        lblEstado = label("⚫ Desconectado", new Color(248, 81, 73));
        norte.add(lblEstado);

        add(norte, BorderLayout.NORTH);

        // ── Panel CENTRO: mapa ──
        panelMapa = new MapPanel();
        panelMapa.setPreferredSize(new Dimension(ANCHO_MAPA, ALTO_MAPA));
        panelMapa.setBackground(BG);
        panelMapa.addMouseListener(new MouseAdapter() {
            @Override public void mouseClicked(MouseEvent e) {
                if (sock != null) enviar("MOVE " + e.getX() + " " + e.getY());
            }
        });
        add(panelMapa, BorderLayout.CENTER);

        // ── Panel SUR: controles + log ──
        JPanel sur = new JPanel();
        sur.setLayout(new BoxLayout(sur, BoxLayout.Y_AXIS));
        sur.setBackground(BG);

        // Auth
        JPanel pAuth = grupo(" Autenticación ", BG2, BLUE);
        pAuth.setLayout(new FlowLayout(FlowLayout.LEFT, 6, 2));
        pAuth.add(label("Usuario:", FG));
        tfUsuario = input("alice", 10, BG, FG); pAuth.add(tfUsuario);
        pAuth.add(label("Pass:", FG));
        tfPass = new JPasswordField(8);
        estilizar(tfPass, BG, FG);
        pAuth.add(tfPass);
        btnAuth = boton("Login", new Color(31, 111, 235), Color.WHITE);
        btnAuth.addActionListener(e -> cmdAuth());
        pAuth.add(btnAuth);
        lblRol = label("", FG);
        pAuth.add(lblRol);
        sur.add(pAuth);

        // Salas
        JPanel pSala = grupo(" Salas ", BG2, BLUE);
        pSala.setLayout(new FlowLayout(FlowLayout.LEFT, 6, 2));
        JButton btnList = boton("📋 Listar", BG, FG);
        btnList.addActionListener(e -> enviar("LIST_ROOMS"));
        pSala.add(btnList);
        JButton btnCrea = boton("➕ Crear", BG, FG);
        btnCrea.addActionListener(e -> enviar("CREATE_ROOM"));
        pSala.add(btnCrea);
        pSala.add(label("ID:", FG));
        tfSala = input("0", 4, BG, FG); pSala.add(tfSala);
        JButton btnJoin = boton("Unirse", BG, FG);
        btnJoin.addActionListener(e -> enviar("JOIN " + tfSala.getText().trim()));
        pSala.add(btnJoin);
        sur.add(pSala);

        // Acciones del juego
        JPanel pGame = grupo(" Acciones del juego ", BG2, BLUE);
        pGame.setLayout(new FlowLayout(FlowLayout.LEFT, 6, 2));
        JButton btnMover = boton("Mover a x,y:", BG, FG);
        JTextField tfMoveX = input("200", 4, BG, FG);
        JTextField tfMoveY = input("150", 4, BG, FG);
        btnMover.addActionListener(e -> {
            try {
                int x = Integer.parseInt(tfMoveX.getText().trim());
                int y = Integer.parseInt(tfMoveY.getText().trim());
                enviar("MOVE " + x + " " + y);
            } catch (NumberFormatException ex) { log("[ERROR] Coordenadas inválidas"); }
        });
        pGame.add(label("x:", FG)); pGame.add(tfMoveX);
        pGame.add(label("y:", FG)); pGame.add(tfMoveY);
        pGame.add(btnMover);

        pGame.add(label("Recurso ID:", FG));
        tfRecurso = input("0", 4, BG, FG); pGame.add(tfRecurso);

        btnAttack = boton("⚔ ATTACK", new Color(218, 54, 51), Color.WHITE);
        btnAttack.setEnabled(false);
        btnAttack.addActionListener(e -> enviar("ATTACK " + tfRecurso.getText().trim()));
        pGame.add(btnAttack);

        btnDefend = boton("🛡 DEFEND", new Color(26, 127, 55), Color.WHITE);
        btnDefend.setEnabled(false);
        btnDefend.addActionListener(e -> enviar("DEFEND " + tfRecurso.getText().trim()));
        pGame.add(btnDefend);
        sur.add(pGame);

        // Comando manual
        JPanel pCmd = new JPanel(new BorderLayout(4, 0));
        pCmd.setBackground(BG);
        tfCmd = input("", 30, BG, FG);
        tfCmd.addActionListener(e -> { enviar(tfCmd.getText().trim()); tfCmd.setText(""); });
        pCmd.add(tfCmd, BorderLayout.CENTER);
        JButton btnSend = boton("Enviar", BG2, FG);
        btnSend.addActionListener(e -> { enviar(tfCmd.getText().trim()); tfCmd.setText(""); });
        pCmd.add(btnSend, BorderLayout.EAST);
        sur.add(pCmd);

        // Log
        txtLog = new JTextArea(7, 60);
        txtLog.setEditable(false);
        txtLog.setBackground(BG2);
        txtLog.setForeground(FG);
        txtLog.setFont(new Font("Monospaced", Font.PLAIN, 11));
        JScrollPane scroll = new JScrollPane(txtLog);
        scroll.setBorder(BorderFactory.createLineBorder(new Color(48,54,61)));
        sur.add(scroll);

        add(sur, BorderLayout.SOUTH);
    }

    // ── Panel del mapa ───────────────────────────────────────────
    private class MapPanel extends JPanel {
        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            Graphics2D g2 = (Graphics2D) g;
            g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                                RenderingHints.VALUE_ANTIALIAS_ON);

            // Fondo
            g2.setColor(new Color(13, 17, 23));
            g2.fillRect(0, 0, getWidth(), getHeight());

            // Cuadrícula
            g2.setColor(new Color(28, 33, 40));
            for (int x = 0; x < ANCHO_MAPA; x += 40)
                g2.drawLine(x, 0, x, ALTO_MAPA);
            for (int y = 0; y < ALTO_MAPA; y += 30)
                g2.drawLine(0, y, ANCHO_MAPA, y);

            // Borde
            g2.setColor(new Color(88, 166, 255));
            g2.setStroke(new BasicStroke(2));
            g2.drawRect(1, 1, ANCHO_MAPA - 2, ALTO_MAPA - 2);
            g2.setStroke(new BasicStroke(1));

            // Recursos críticos
            for (Map.Entry<Integer, Object[]> e : recursos.entrySet()) {
                int rid = e.getKey();
                Object[] r = e.getValue();
                String rnombre = (String) r[0];
                int rx = (int) r[1], ry = (int) r[2];
                int estado = (int) r[3];
                Color col = estado == 1 ? new Color(248, 81, 73)
                          : estado == 2 ? new Color(46, 160, 67)
                          : new Color(227, 179, 65);
                g2.setColor(col);
                g2.fillOval(rx - RADIO_RECURSO, ry - RADIO_RECURSO,
                            RADIO_RECURSO * 2, RADIO_RECURSO * 2);
                g2.setColor(Color.WHITE);
                g2.setStroke(new BasicStroke(2));
                g2.drawOval(rx - RADIO_RECURSO, ry - RADIO_RECURSO,
                            RADIO_RECURSO * 2, RADIO_RECURSO * 2);
                g2.setStroke(new BasicStroke(1));
                g2.setFont(new Font("Arial", Font.PLAIN, 9));
                g2.drawString("#" + rid + " " + rnombre,
                              rx - 30, ry + RADIO_RECURSO + 12);
            }

            // Otros jugadores
            synchronized (jugadores) {
                for (Map.Entry<String, int[]> e : jugadores.entrySet()) {
                    String jname = e.getKey();
                    int[] info = e.getValue();
                    int jx = info[0], jy = info[1], jrol = info[2];
                    Color col = jname.equals(nombre)
                        ? new Color(52, 152, 219)
                        : jrol == 1
                        ? new Color(231, 76, 60)
                        : new Color(46, 204, 113);
                    g2.setColor(col);
                    g2.fillOval(jx - RADIO_JUGADOR, jy - RADIO_JUGADOR,
                                RADIO_JUGADOR * 2, RADIO_JUGADOR * 2);
                    g2.setColor(Color.WHITE);
                    g2.setFont(new Font("Arial", Font.BOLD, 9));
                    String label = jname.equals(nombre) ? "YO " + jname : jname;
                    g2.drawString(label, jx - 16, jy - RADIO_JUGADOR - 4);
                }
            }
        }
    }

    // ── Red ─────────────────────────────────────────────────────
    private void conectar() {
        String host = tfHost.getText().trim();
        int    port = Integer.parseInt(tfPort.getText().trim());
        try {
            // Java resuelve DNS automáticamente con el hostname
            sock   = new Socket(host, port);
            salida = new PrintWriter(sock.getOutputStream(), true);
            entrada = new BufferedReader(
                          new InputStreamReader(sock.getInputStream()));
            log("[SISTEMA] Conectado a " + host + ":" + port);
            SwingUtilities.invokeLater(() -> {
                btnConectar.setEnabled(false);
                lblEstado.setText("🟢 Conectado");
                lblEstado.setForeground(new Color(46, 160, 67));
            });
            new Thread(this::escuchar).start();
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this,
                "Error de conexión: " + ex.getMessage());
        }
    }

    private void escuchar() {
        try {
            String linea;
            while ((linea = entrada.readLine()) != null) {
                final String msg = linea;
                SwingUtilities.invokeLater(() -> procesarMensaje(msg));
            }
        } catch (IOException e) {
            /* conexión cerrada */
        }
        SwingUtilities.invokeLater(() -> {
            log("[SISTEMA] Desconectado.");
            btnConectar.setEnabled(true);
            lblEstado.setText("⚫ Desconectado");
            lblEstado.setForeground(new Color(248, 81, 73));
        });
    }

    private synchronized void enviar(String msg) {
        if (salida != null && !msg.isEmpty()) {
            salida.println(msg);
            log("[YO →] " + msg);
        }
    }

    // ── Procesamiento de mensajes del servidor ──────────────────
    private void procesarMensaje(String msg) {
        log("[← SRV] " + msg);
        String[] p = msg.split(" ");
        if (p.length == 0) return;
        String cmd = p[0];

        switch (cmd) {
            case "OK_AUTH":
                if (p.length >= 3) {
                    String rolRecibido = p[2];
                    if (!rolRecibido.equals("ATACANTE") && !rolRecibido.equals("DEFENSOR")) {
                        nombre = "";
                        rol = "";
                        btnAttack.setEnabled(false);
                        btnDefend.setEnabled(false);
                        lblRol.setText("No autenticado");
                        lblRol.setForeground(new Color(248, 81, 73));
                        JOptionPane.showMessageDialog(this,
                            "Rol inválido recibido en AUTH: " + rolRecibido,
                            "Respuesta inválida",
                            JOptionPane.ERROR_MESSAGE);
                        break;
                    }
                    nombre = p[1];
                    rol    = rolRecibido;
                    configurarRol();
                }
                break;

            case "ERR_AUTH":
                nombre = "";
                rol = "";
                btnAttack.setEnabled(false);
                btnDefend.setEnabled(false);
                lblRol.setText("No autenticado");
                lblRol.setForeground(new Color(248, 81, 73));
                JOptionPane.showMessageDialog(this,
                    p.length > 1 ? msg.substring("ERR_AUTH ".length())
                                 : "Autenticación fallida",
                    "Error de autenticación",
                    JOptionPane.ERROR_MESSAGE);
                break;

            case "POS":
                // POS <nombre> <x> <y> <rol>
                if (p.length >= 4) {
                    try {
                        String jn = p[1];
                        int jx = Integer.parseInt(p[2]);
                        int jy = Integer.parseInt(p[3]);
                        int jr = (p.length >= 5 && p[4].equals("ATACANTE")) ? 1 : 0;
                        synchronized (jugadores) {
                            jugadores.put(jn, new int[]{jx, jy, jr});
                        }
                        panelMapa.repaint();
                    } catch (NumberFormatException ignored) {}
                }
                break;

            case "RESOURCE_INFO":
                // RESOURCE_INFO <id> <nombre> <x> <y>
                if (p.length >= 5) {
                    try {
                        int rid = Integer.parseInt(p[1]);
                        String rn = p[2];
                        int rx = Integer.parseInt(p[3]);
                        int ry = Integer.parseInt(p[4]);
                        recursos.put(rid, new Object[]{rn, rx, ry, 0});
                        panelMapa.repaint();
                    } catch (NumberFormatException ignored) {}
                }
                break;

            case "RESOURCE_FOUND":
                // RESOURCE_FOUND <id> <nombre> <x> <y>
                if (p.length >= 5) {
                    try {
                        int rid = Integer.parseInt(p[1]);
                        String rn = p[2];
                        int rx = Integer.parseInt(p[3]);
                        int ry = Integer.parseInt(p[4]);
                        int estado = 0;
                        if (recursos.containsKey(rid))
                            estado = (int) ((Object[]) recursos.get(rid))[3];
                        recursos.put(rid, new Object[]{rn, rx, ry, estado});
                        panelMapa.repaint();
                        tfRecurso.setText(String.valueOf(rid));
                        JOptionPane.showMessageDialog(this,
                            "¡Recurso encontrado: #" + rid + " " + rn + "!\n" +
                            "Se cargó automáticamente el ID " + rid + " para ATTACK.",
                            "Recurso detectado",
                            JOptionPane.INFORMATION_MESSAGE);
                    } catch (NumberFormatException ignored) {}
                }
                break;

            case "ATTACK_ALERT":
                // ATTACK_ALERT <atacante> <rid> <nombre_recurso>
                if (p.length >= 3) {
                    try {
                        int rid = Integer.parseInt(p[2]);
                        if (recursos.containsKey(rid))
                            ((Object[]) recursos.get(rid))[3] = 1;
                        panelMapa.repaint();
                        JOptionPane.showMessageDialog(this,
                            "⚠ ¡ATAQUE! " + p[1] + " ataca recurso " +
                            (p.length >= 4 ? p[3] : rid) + "\n" +
                            "Envía: DEFEND " + rid,
                            "ALERTA DE ATAQUE",
                            JOptionPane.WARNING_MESSAGE);
                    } catch (NumberFormatException ignored) {}
                }
                break;

            case "DEFEND_SUCCESS":
                if (p.length >= 3) {
                    try {
                        int rid = Integer.parseInt(p[2]);
                        if (recursos.containsKey(rid))
                            ((Object[]) recursos.get(rid))[3] = 2;
                        panelMapa.repaint();
                    } catch (NumberFormatException ignored) {}
                }
                break;

            case "EVENT":
                if (p.length >= 2) {
                    String tipo = p[1];
                    if ("GAME_START".equals(tipo) && p.length >= 5) {
                        log("[PARTIDA] Inició sala " + p[2] +
                            " (ATACANTES=" + p[3] + ", DEFENSORES=" + p[4] + ")");
                        JOptionPane.showMessageDialog(this,
                            "Sala " + p[2] + " iniciada con " + p[3] +
                            " atacante(s) y " + p[4] + " defensor(es).",
                            "Partida iniciada",
                            JOptionPane.INFORMATION_MESSAGE);
                    } else if ("GAME_WAITING".equals(tipo) && p.length >= 5) {
                        log("[PARTIDA] Sala " + p[2] + " en espera " +
                            "(ATACANTES=" + p[3] + ", DEFENSORES=" + p[4] + ")");
                    } else if ("GAME_END".equals(tipo) && p.length >= 4) {
                        log("[PARTIDA] Finalizada sala " + p[2] + " — Ganan " + p[3]);
                        JOptionPane.showMessageDialog(this,
                            "Sala " + p[2] + " finalizada.\nGanan: " + p[3],
                            "Partida finalizada",
                            JOptionPane.INFORMATION_MESSAGE);
                    }
                }
                break;

            case "OK_JOIN":
            case "OK_CREATE":
                if (p.length >= 2) {
                    salaId = p[1];
                    // Limpiar recursos y jugadores de partida anterior
                    recursos.clear();
                    synchronized (jugadores) {
                        jugadores.clear();
                    }
                    panelMapa.repaint();
                    setTitle("[" + rol + "] " + nombre + " — Sala " + salaId);
                }
                break;

            case "PLAYER_LEAVE":
                if (p.length >= 2) {
                    synchronized (jugadores) { jugadores.remove(p[1]); }
                    panelMapa.repaint();
                }
                break;

            default:
                if (cmd.startsWith("ERR_")) {
                    String detalle = msg.contains(" ")
                        ? msg.substring(msg.indexOf(' ') + 1)
                        : msg;
                    JOptionPane.showMessageDialog(this,
                        detalle,
                        "Respuesta del servidor",
                        JOptionPane.WARNING_MESSAGE);
                }
                break;
        }
    }

    private void configurarRol() {
        setTitle("[" + rol + "] " + nombre + " — Juego de Ciberseguridad");
        if (rol.equals("ATACANTE")) {
            btnAttack.setEnabled(true);
            btnDefend.setEnabled(false);
            lblRol.setText("🔴 ATACANTE");
            lblRol.setForeground(new Color(248, 81, 73));
            log("[ROL] Eres ATACANTE — Muévete para encontrar recursos");
        } else {
            btnDefend.setEnabled(true);
            btnAttack.setEnabled(false);
            lblRol.setText("🟢 DEFENSOR");
            lblRol.setForeground(new Color(46, 160, 67));
            log("[ROL] Eres DEFENSOR — Recibirás alertas de ataque");
        }
    }

    // ── Comandos ─────────────────────────────────────────────────
    private void cmdAuth() {
        String usr = tfUsuario.getText().trim();
        String pwd = new String(tfPass.getPassword()).trim();
        if (!usr.isEmpty() && !pwd.isEmpty())
            enviar("AUTH " + usr + " " + pwd);
    }

    // ── Log ──────────────────────────────────────────────────────
    private void log(String msg) {
        SwingUtilities.invokeLater(() -> {
            txtLog.append(msg + "\n");
            txtLog.setCaretPosition(txtLog.getDocument().getLength());
        });
    }

    // ── Helpers de UI ────────────────────────────────────────────
    private JLabel label(String t, Color fg) {
        JLabel l = new JLabel(t);
        l.setForeground(fg);
        return l;
    }
    private JTextField input(String def, int cols, Color bg, Color fg) {
        JTextField tf = new JTextField(def, cols);
        estilizar(tf, bg, fg);
        return tf;
    }
    private void estilizar(JTextField tf, Color bg, Color fg) {
        tf.setBackground(bg);
        tf.setForeground(fg);
        tf.setCaretColor(fg);
        tf.setBorder(BorderFactory.createCompoundBorder(
            BorderFactory.createLineBorder(new Color(48, 54, 61)),
            BorderFactory.createEmptyBorder(3, 6, 3, 6)));
    }
    private JButton boton(String t, Color bg, Color fg) {
        JButton b = new JButton(t);
        b.setBackground(bg);
        b.setForeground(fg);
        b.setFocusPainted(false);
        b.setBorderPainted(false);
        b.setOpaque(true);
        return b;
    }
    private JPanel grupo(String titulo, Color bg, Color titleColor) {
        JPanel p = new JPanel();
        p.setBackground(bg);
        p.setBorder(BorderFactory.createTitledBorder(
            BorderFactory.createLineBorder(new Color(48, 54, 61)),
            titulo,
            javax.swing.border.TitledBorder.LEFT,
            javax.swing.border.TitledBorder.TOP,
            new Font("Arial", Font.BOLD, 11),
            titleColor));
        return p;
    }

    // ── Main ─────────────────────────────────────────────────────
    public static void main(String[] args) {
        String host = args.length > 0 ? args[0] : "localhost";
        int    port = args.length > 1 ? Integer.parseInt(args[1]) : 8080;
        try { UIManager.setLookAndFeel(UIManager.getCrossPlatformLookAndFeelClassName()); }
        catch (Exception ignored) {}
        SwingUtilities.invokeLater(() -> new ClienteJuego(host, port).setVisible(true));
    }
}
