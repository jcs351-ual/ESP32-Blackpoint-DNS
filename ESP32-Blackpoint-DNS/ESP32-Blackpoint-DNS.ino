#include <WiFi.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

// Instanciacion de las variables de Red
String wifi_ssid = "";
String wifi_passwd = "";

const char* AP_SSID = "SF_DNS_BUDDY_01";
const char* AP_PASSWD = "@Test01";

WiFiUDP dnsServer;
const unsigned int DNS_PORT = 53;
const IPAddress PUBLIC_DNS(94, 140, 14, 14); // DNS de forwarding (CloudFlare)

WiFiUDP dnsClient;
const char* whitelist[] = {
    "powvideo",
    "streamplay"
};
// Estructura para gestionar peticiones en paralelo sin bloquear el microcontrolador
struct PeticionPendiente {
    uint16_t id;
    IPAddress clienteIP;
    uint16_t clientePuerto;
    unsigned long timestamp;
    bool activa;
};
const int MAX_PETICIONES_SIMULTANEAS = 15;
PeticionPendiente transacciones[MAX_PETICIONES_SIMULTANEAS];

uint8_t packetBuffer[512]; 
uint8_t clientResponseBuffer[512];

// Instanciacion de Filtro (64 KB)
const size_t BLOOM_FILTER_SIZE = 65536; 
uint8_t bloomFilter[BLOOM_FILTER_SIZE];
bool filtroListo = false;

// ============= IMPLEMENTACION DEL PROTOCOLO DE CONFIGURACION =============

// Instancias para el Portal Cautivo de Configuracion
WebServer webServer(80);
DNSServer captiveDnsServer;
Preferences preferences;

//Boton de Reinicio de Fabrica
const int PIN_BUTTON_RESET = 0;

// Portal de Login
const char* HTML_PORTAL = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif;background:#f0f2f5;padding:20px;text-align:center;}"
".card{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.1);max-width:400px;margin:auto;}"
"h2{color:#1a73e8;}input[type=text],input[type=password]{width:90%;padding:10px;margin:10px 0;border:1px solid #ccc;border-radius:6px;}"
"input[type=submit]{background:#1a73e8;color:white;border:none;padding:12px;width:95%;border-radius:6px;cursor:pointer;font-size:16px;}</style>"
"</head><body><div class='card'><h2>Configuración SF_DNS</h2><p>Introduce los datos de tu Wi-Fi para activar el DNS:</p>"
"<form action='/save' method='POST'>"
"<input type='text' name='ssid' placeholder='Nombre del Wi-Fi (SSID)' required><br>"
"<input type='password' name='pass' placeholder='Contraseña' required><br><br>"
"<input type='submit' value='Conectar Dispositivo'>"
"</form></div></body></html>";

// Web Handlers
void handleRoot() {
    webServer.send(200, "text/html", HTML_PORTAL);
}
void handleSave() {
    if (webServer.hasArg("ssid") && webServer.hasArg("pass")) {
        wifi_ssid = webServer.arg("ssid");
        wifi_passwd = webServer.arg("pass");

        // Guardar credenciales de forma permanente en la Flash
        preferences.begin("net-config", false);
        preferences.putString("ssid", wifi_ssid);
        preferences.putString("pass", wifi_passwd);
        preferences.end();

        webServer.send(200, "text/html", "<p style='text-align:center;font-family:sans-serif;'><b>Configuraci&oacute;n guardada. El dispositivo se est&aacute; reiniciando...</b><br>Ya puedes cerrar esta ventana.</p>");
        delay(2000);
        ESP.restart();
    }
}
void iniciarPortalCautivo() {
    Serial.println("Activando Portal de Configuración...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWD);

    // Se redirige por defecto al portal de inicio de sesion
    captiveDnsServer.start(53, "*", WiFi.softAPIP());

    webServer.on("/", handleRoot);
    webServer.on("/save", handleSave);
    webServer.onNotFound(handleRoot);
    webServer.begin();

    Serial.printf("Por favor, conéctate al Wi-Fi '%s' desde tu móvil para configurarlo.\n", AP_SSID);

    while (true) {
        captiveDnsServer.processNextRequest();
        webServer.handleClient();
        delay(10);
    }
}
// Control de Reinicio de Fabrica
void verificarFactoryResetFisico() {
    pinMode(PIN_BUTTON_RESET, INPUT_PULLUP);
    if (digitalRead(PIN_BUTTON_RESET) == LOW) {
        Serial.print("Botón de Reset pulsado. Comprobando tiempo...");
        unsigned long startTime = millis();
        while (digitalRead(PIN_BUTTON_RESET) == LOW) {
            delay(100);
            if (millis() - startTime > 4000) { // 4 Segundos
                Serial.println("\n [FACTORY RESET] ¡Borrando memoria y restaurando de fábrica!");
                preferences.begin("net-config", false);
                preferences.clear();
                preferences.end();
                delay(1000);
                ESP.restart();
            }
        }
        Serial.println(" Cancelado (Tiempo insuficiente).");
    }
}
// ============= Implementacion DNS =============

// Carga de filtros en memoria
bool inicializarFiltroBloom() {
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: No se pudo montar LittleFS.");
        return false;
    }

    if (!LittleFS.exists("/filtro.bin")) {
        Serial.println("ERROR: El archivo /filtro.bin NO existe en la Flash. ¡Vuelve a subirlo con Ctrl+Shift+P!");
        return false;
    }

    File file = LittleFS.open("/filtro.bin", "r");
    if (!file) {
        Serial.println("ERROR: No se pudo abrir /filtro.bin para lectura.");
        return false;
    }

    size_t bytesLeidos = file.read(bloomFilter, BLOOM_FILTER_SIZE);
    file.close();

    if (bytesLeidos != BLOOM_FILTER_SIZE) {
        Serial.printf("ADVERTENCIA: Tamaño incorrecto del filtro binario. Leídos %d de %d bytes.\n", bytesLeidos, BLOOM_FILTER_SIZE);
        return false;
    }

    Serial.printf("Filtro Bloom de 64KB cargado con éxito en RAM (%d bytes).\n", bytesLeidos);
    return true;
}
// Parser de Formato Binario
void extraerDominio(uint8_t* buffer, int startIdx, char* dominioOut) {
    int i = startIdx;
    int outIdx = 0;
    while (buffer[i] != 0 && i < 512) {
        int labelLength = buffer[i];
        i++;
        for (int j = 0; j < labelLength && i < 512; j++) {
            dominioOut[outIdx++] = tolower(buffer[i++]);
        }
        if (buffer[i] != 0) {
            dominioOut[outIdx++] = '.';
        }
    }
    dominioOut[outIdx] = '\0';
}
// Comprobacion de diccionario de Hashes
uint32_t hash_djb2(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}
uint32_t hash_fnv1a(const char* str) {
    uint32_t hash = 2166136261U;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619U;
    }
    return hash;
}
// Comprobaciones del bitmap en filtro
bool verificarEnFiltro(const char* dominio) {
    uint32_t h1 = hash_djb2(dominio);
    uint32_t h2 = hash_fnv1a(dominio);
    uint32_t totalBits = BLOOM_FILTER_SIZE * 8;

    uint32_t bit1 = h1 % totalBits;
    uint32_t bit2 = h2 % totalBits;

    if ((bloomFilter[bit1 / 8] & (1 << (bit1 % 8))) == 0) return false; 
    if ((bloomFilter[bit2 / 8] & (1 << (bit2 % 8))) == 0) return false; 

    return true; 
}
// Comprobacion de direcciones en filtro
bool esDominioBloqueado(const char* dominio) {
    if (!filtroListo) return false; // Modo degradado: si falla el archivo, deja navegar

    //Inicialización de la whitelist
    for (const char* excepcion : whitelist) {
        if (strstr(dominio, excepcion) != NULL) {
            Serial.printf("[WHITELIST] -> Acceso permitido por excepción a: %s\n", dominio);
            return false; // Permite el tráfico de inmediato
        }
    }

    // 1. Probar el dominio completo tal cual llega
    if (verificarEnFiltro(dominio)) return true;

    // 2. Si tiene subdominios, descomponer y probar raíces (ej: de ://example.com a example.com)
    const char* punto = strchr(dominio, '.');
    while (punto != NULL) {
        const char* subDominio = punto + 1;
        if (strchr(subDominio, '.') != NULL) {
            if (verificarEnFiltro(subDominio)) return true;
        }
        punto = strchr(subDominio, '.');
    }
    return false; 
}
// Respuesta de Protocolo
void responderSinkhole(IPAddress clienteIP, uint16_t clientePuerto, uint8_t* originalPacket, int packetSize, int queryEndIdx, bool esIPv6) {
    uint8_t response[512];
    memcpy(response, originalPacket, packetSize);

    response[2] = 0x81; response[3] = 0x80; // Flags: Respuesta, No error
    response[6] = 0x00; response[7] = 0x01; // Answer Count = 1

    int idx = queryEndIdx; 
    response[idx++] = 0xC0; response[idx++] = 0x0C; // Puntero al nombre original
    
    if (esIPv6) {
        response[idx++] = 0x00; response[idx++] = 0x1C; // Tipo AAAA
        response[idx++] = 0x00; response[idx++] = 0x01; // Clase IN
        response[idx++] = 0x00; response[idx++] = 0x00; response[idx++] = 0x0E; response[idx++] = 0x10; // TTL: 3600s
        response[idx++] = 0x00; response[idx++] = 0x10; // Longitud = 16 bytes (IPv6)
        for(int i=0; i<16; i++) response[idx++] = 0;    // IP: :: (Bloqueado)
    } else {
        response[idx++] = 0x00; response[idx++] = 0x01; // Tipo A
        response[idx++] = 0x00; response[idx++] = 0x01; // Clase IN
        response[idx++] = 0x00; response[idx++] = 0x00; response[idx++] = 0x0E; response[idx++] = 0x10; // TTL: 3600s
        response[idx++] = 0x00; response[idx++] = 0x04; // Longitud = 4 bytes (IPv4)
        response[idx++] = 0; response[idx++] = 0; response[idx++] = 0; response[idx++] = 0; // IP: 0.0.0.0
    }

    dnsServer.beginPacket(clienteIP, clientePuerto);
    dnsServer.write(response, idx);
    dnsServer.endPacket();
}
// DNS Forward luz verde
void reenviarADnsPublicoAsincrono(IPAddress clienteIP, uint16_t clientePuerto, uint16_t id, uint8_t* paquete, int tamaño) {
    int slotLibre = -1;
    for (int i = 0; i < MAX_PETICIONES_SIMULTANEAS; i++) {
        if (!transacciones[i].activa) {
            slotLibre = i;
            break;
        }
    }

    if (slotLibre != -1) {
        transacciones[slotLibre] = {id, clienteIP, clientePuerto, millis(), true};
        dnsClient.beginPacket(PUBLIC_DNS, 53);
        dnsClient.write(paquete, tamaño);
        dnsClient.endPacket();
    }
}
// Respuesta dinamica de Red
void procesarRespuestasSalientes() {
    int respSize = dnsClient.parsePacket();
    if (respSize > 0 && respSize <= 512) {
        dnsClient.read(clientResponseBuffer, respSize);
        uint16_t resId = (clientResponseBuffer[0] << 8) | clientResponseBuffer[1];

        for (int i = 0; i < MAX_PETICIONES_SIMULTANEAS; i++) {
            if (transacciones[i].activa && transacciones[i].id == resId) {
                dnsServer.beginPacket(transacciones[i].clienteIP, transacciones[i].clientePuerto);
                dnsServer.write(clientResponseBuffer, respSize);
                dnsServer.endPacket();
                transacciones[i].activa = false;
                return;
            }
        }
    }

    // Limpieza de transacciones colgadas (Timeout de 4 segundos)
    for (int i = 0; i < MAX_PETICIONES_SIMULTANEAS; i++) {
        if (transacciones[i].activa && (millis() - transacciones[i].timestamp > 4000)) {
            transacciones[i].activa = false;
        }
    }
}

// ENTORNO DE PROD
void setup() {
    Serial.begin(115200);
    delay(1500); 
    Serial.println("\n\n=== SISTEMA DE ARRANQUE DNS SINKHOLE ===");
    
    filtroListo = inicializarFiltroBloom();
        if (!filtroListo) {
        Serial.println("MODO DE DEGRADACIÓN: Arrancando sin protección de Filtro Bloom.");
    }

    // Comprobacion de credenciales de red
    preferences.begin("net-config", true);
    wifi_ssid = preferences.getString("ssid", "");
    wifi_passwd = preferences.getString("pass", "");
    preferences.end();

    if (wifi_ssid == "" || wifi_passwd == "") {
        Serial.println("ℹ️ Dispositivo sin configurar.");
        iniciarPortalCautivo();
    } else {
        Serial.printf("Intentando conectar a: %s\n", wifi_ssid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_ssid.c_str(), wifi_passwd.c_str());

        // Prueba de conectividad
        int intentos = 0;
        while (WiFi.status() != WL_CONNECTED && intentos < 30) {
            delay(500);
            Serial.print(".");
            intentos++;
        }

        // Re-lanzamiento del portal de configuracion en caso de fallo
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("\n Error de Conexion: Re-lanzando portal de configuracion.");
            iniciarPortalCautivo();
        }
        Serial.printf("\n Red Configurada. IP de tu Servidor DNS: %s\n", WiFi.localIP().toString().c_str());
    }

    dnsServer.begin(DNS_PORT);
    dnsClient.begin(0); // El sistema elige un puerto local UDP dinámico libre
    
    for (int i = 0; i < MAX_PETICIONES_SIMULTANEAS; i++) transacciones[i].activa = false;
    Serial.println(" Servidor DNS escuchando de forma asíncrona...");
}

void loop() {
    verificarFactoryResetFisico();
    procesarRespuestasSalientes(); 

    int packetSize = dnsServer.parsePacket();
    if (packetSize > 0 && packetSize <= 512) {
        dnsServer.read(packetBuffer, packetSize);

        uint16_t queryId = (packetBuffer[0] << 8) | packetBuffer[1];
        
        char dominio[256]; 
        extraerDominio(packetBuffer, 12, dominio);
        
        // Localización dinámica de los bytes del Tipo de consulta (Query Type)
        int idxBusqueda = 12;
        while (packetBuffer[idxBusqueda] != 0 && idxBusqueda < packetSize) {
            idxBusqueda += packetBuffer[idxBusqueda] + 1;
        }
        int queryTypeIdx = idxBusqueda + 1; 

        if (queryTypeIdx + 2 <= packetSize) {
            uint16_t queryType = (packetBuffer[queryTypeIdx] << 8) | packetBuffer[queryTypeIdx + 1];
            int queryEndIdx = queryTypeIdx + 4;
            bool esIPv6 = (queryType == 0x001C);

            if (esDominioBloqueado(dominio)) {
                Serial.printf("🛑 [SINKHOLE %s] -> %s\n", esIPv6 ? "IPv6" : "IPv4", dominio);
                responderSinkhole(dnsServer.remoteIP(), dnsServer.remotePort(), packetBuffer, packetSize, queryEndIdx, esIPv6);
            } else {
                Serial.printf("🟢 [FORWARD %s] -> %s\n", esIPv6 ? "IPv6" : "IPv4", dominio);
                reenviarADnsPublicoAsincrono(dnsServer.remoteIP(), dnsServer.remotePort(), queryId, packetBuffer, packetSize);
            }
        }
    }
}