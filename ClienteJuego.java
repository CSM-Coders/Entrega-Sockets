import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.net.*;

public class ClienteJuego extends JFrame {
    private Socket socket;
    private PrintWriter out;
    private BufferedReader in;
    
    private JTextArea txtLog;
    private JTextField txtComando;
    private JPanel panelMapa;
    
    // Variables para dibujar al jugador
    private int jugadorX = -100; 
    private int jugadorY = -100;
    private String nombreJugador = "";

    public ClienteJuego() {
        setTitle("Cliente Java - Ciberseguridad");
        setSize(450, 500);
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setLayout(new BorderLayout());

        // Panel de conexión superior
        JPanel panelConexion = new JPanel();
        JTextField txtIp = new JTextField("127.0.0.1", 10);
        JTextField txtPuerto = new JTextField("8080", 5);
        JButton btnConectar = new JButton("Conectar");
        
        panelConexion.add(new JLabel("IP:"));
        panelConexion.add(txtIp);
        panelConexion.add(new JLabel("Puerto:"));
        panelConexion.add(txtPuerto);
        panelConexion.add(btnConectar);
        add(panelConexion, BorderLayout.NORTH);

        // Panel central (Mapa)
        panelMapa = new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                // Dibujar cuadrícula
                g.setColor(Color.LIGHT_GRAY);
                for(int i=0; i<getWidth(); i+=40) g.drawLine(i, 0, i, getHeight());
                for(int i=0; i<getHeight(); i+=30) g.drawLine(0, i, getWidth(), i);
                
                // Dibujar jugador si las coordenadas son válidas
                if(jugadorX >= 0 && jugadorY >= 0) {
                    g.setColor(Color.RED);
                    g.fillOval(jugadorX - 10, jugadorY - 10, 20, 20);
                    g.setColor(Color.BLACK);
                    g.drawString(nombreJugador, jugadorX - 15, jugadorY - 15);
                }
            }
        };
        panelMapa.setPreferredSize(new Dimension(400, 300));
        panelMapa.setBackground(Color.WHITE);
        add(panelMapa, BorderLayout.CENTER);

        // Panel inferior (Logs y comandos)
        JPanel panelInferior = new JPanel(new BorderLayout());
        txtLog = new JTextArea(8, 30);
        txtLog.setEditable(false);
        panelInferior.add(new JScrollPane(txtLog), BorderLayout.CENTER);
        
        JPanel panelEnvio = new JPanel(new BorderLayout());
        txtComando = new JTextField();
        JButton btnEnviar = new JButton("Enviar");
        panelEnvio.add(txtComando, BorderLayout.CENTER);
        panelEnvio.add(btnEnviar, BorderLayout.EAST);
        panelInferior.add(panelEnvio, BorderLayout.SOUTH);
        
        add(panelInferior, BorderLayout.SOUTH);

        // --- EVENTOS ---
        btnConectar.addActionListener(e -> {
            try {
                socket = new Socket(txtIp.getText(), Integer.parseInt(txtPuerto.getText()));
                out = new PrintWriter(socket.getOutputStream(), true);
                in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
                registrarLog("[SISTEMA] Conectado al servidor C.");
                btnConectar.setEnabled(false);
                
                // Hilo para escuchar al servidor
                new Thread(this::escucharServidor).start();
            } catch (Exception ex) {
                JOptionPane.showMessageDialog(this, "Error de conexión: " + ex.getMessage());
            }
        });

        ActionListener enviarAction = e -> {
            if (out != null) {
                String cmd = txtComando.getText();
                out.println(cmd);
                registrarLog("[YO] " + cmd);
                txtComando.setText("");
            }
        };
        btnEnviar.addActionListener(enviarAction);
        txtComando.addActionListener(enviarAction);
    }

    private void escucharServidor() {
        try {
            String respuesta;
            while ((respuesta = in.readLine()) != null) {
                registrarLog("[SERVIDOR] " + respuesta);
                
                // Parsear comandos del servidor
                String[] partes = respuesta.split(" ");
                if (partes[0].equals("POS") && partes.length >= 4) {
                    nombreJugador = partes[1];
                    jugadorX = Integer.parseInt(partes[2]);
                    jugadorY = Integer.parseInt(partes[3]);
                    panelMapa.repaint(); // Redibuja el mapa con el jugador
                }
            }
        } catch (IOException e) {
            registrarLog("[SISTEMA] Desconectado.");
        }
    }

    private void registrarLog(String msj) {
        SwingUtilities.invokeLater(() -> {
            txtLog.append(msj + "\n");
            txtLog.setCaretPosition(txtLog.getDocument().getLength());
        });
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> new ClienteJuego().setVisible(true));
    }
}