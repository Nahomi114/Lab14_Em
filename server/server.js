const WebSocket = require('ws');
const mongoose = require('mongoose');

mongoose.connect('mongodb+srv://esp32_user:esp32_pass@cluster0.s51mc.mongodb.net/esp32_control?retryWrites=true&w=majority');

const deviceStateSchema = new mongoose.Schema({
    servoPos: Number,
    potValue: Number,
    systemEnabled: Boolean,
    potControl: Boolean,
    isReset: Boolean,
    timestamp: { type: Date, default: Date.now },
});

const DeviceState = mongoose.model('DeviceState', deviceStateSchema);

const wss = new WebSocket.Server({ port: 8080 });

let esp32Socket = null;
let clientSockets = new Set();

wss.on('connection', (ws) => {
    console.log('Nuevo cliente conectado');
    
    ws.on('message', async (message) => {
        try {
            // Primero convertimos el mensaje a string
            const messageStr = message.toString();
            console.log('Mensaje recibido:', messageStr); // Logging del mensaje recibido
            
            // Verificamos si es el mensaje de identificación del ESP32
            if (messageStr === 'ESP32') {
                esp32Socket = ws;
                clientSockets.delete(ws);
                console.log('ESP32 conectado');
                return;
            }
            
            // Para otros mensajes, intentamos parsear como JSON
            const data = JSON.parse(messageStr);
            
            if (esp32Socket) {
                // Si el mensaje viene de un cliente web
                if (ws !== esp32Socket) {
                    console.log('Mensaje de cliente web hacia ESP32:', data);
                    esp32Socket.send(messageStr);
                } else {
                    // Si el mensaje viene del ESP32
                    console.log('Mensaje recibido del ESP32:', data);
                    
                    if (data.servoPos !== undefined) {
                        const newState = new DeviceState(data);
                        try {
                            await newState.save();
                            console.log('Estado guardado en MongoDB:', data);
                        } catch (dbError) {
                            console.error('Error guardando en MongoDB:', dbError);
                        }
                    }
                    
                    // Enviar a todos los clientes web
                    clientSockets.forEach(client => {
                        if (client.readyState === WebSocket.OPEN) {
                            console.log('Enviando datos a cliente web');
                            client.send(messageStr);
                        }
                    });
                }
            } else {
                console.log('ESP32 no está conectado, no se pueden procesar mensajes');
            }
        } catch (error) {
            console.error('Error procesando mensaje:', error);
            console.error('Mensaje que causó el error:', message.toString());
        }
    });
    
    ws.on('close', () => {
        if (ws === esp32Socket) {
            esp32Socket = null;
            console.log('ESP32 desconectado');
            // Notificar a todos los clientes que ESP32 se desconectó
            const disconnectMsg = JSON.stringify({ type: 'ESP32_DISCONNECTED' });
            clientSockets.forEach(client => {
                if (client.readyState === WebSocket.OPEN) {
                    client.send(disconnectMsg);
                }
            });
        } else {
            clientSockets.delete(ws);
        }
        console.log('Cliente desconectado');
    });

    // Si no es el ESP32, agregarlo a la lista de clientes web
    if (ws !== esp32Socket) {
        clientSockets.add(ws);
        console.log('Cliente web agregado. Total de clientes:', clientSockets.size);
    }
});

console.log('Servidor WebSocket escuchando en puerto 8080');