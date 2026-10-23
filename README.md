# 🛡️ ESP32 DNS Sinkhole

Un servidor DNS Sinkhole ultraligero y de alto rendimiento basado en el microcontrolador **ESP32** (C++). Este proyecto intercepta y destruye anuncios, rastreadores y telemetría a nivel de red para todos los dispositivos del hogar, emulando la lógica de herramientas como Pi-hole pero optimizado para operar en hardware embebido de bajo coste sin sistema operativo.

---

## 🚀 Características Principales

- **Optimización Extrema de RAM (Filtro Bloom de 64 KB):** Ingesta matemática de más de **45,000 dominios únicos** en un mapa binario constante que ocupa apenas 64 KB de RAM, logrando una precisión cercana al 99%.
- **Soporte Nativo IPv4 e IPv6:** Interceptación de tramas de red de nueva generación (`A` y `AAAA`) para evitar evasiones de publicidad y rastreadores por canal IPv6.
- **Motor UDP Asíncrono:** Reenvío no bloqueante en paralelo hacia DNS públicos (como Cloudflare `1.1.1.1`), eliminando retrasos (*timeouts*) y manteniendo fluida la velocidad de internet.

## 📂 Estructura del Proyecto

```text
├── list_updater.py         # Script de Python: Descarga y verifica caché de fuentes
├── create_bloom_filter.py  # Script de Python: Une listas y exporta el mapa de bits
├── ESP32_Blackpoint_DNS    # Firmware de C++ para el microcontrolador ESP32
    ├── ESP32-Blackpoint-DNS.ino
    ├── data/               # Carpeta de Arduino que contiene el "filtro.bin" para LittleFS
└── manuals/                # Carpeta local autogenerada que almacena los diccionarios .txt
```

---

## 🛠️ Guía de Despliegue en 3 Pasos

### 1. Generar el Filtro Bloom

1. Instala las dependencias necesarias en tu consola:
   ```bash
   pip install fnvhash urllib3
   ```
2. Ejecuta el actualizador inteligente para descargar las listas de **AdAway, Yoyo y StevenBlack** solo si presentan cambios en el servidor:
   ```bash
   python list_updater.py
   ```
3. Ejecuta el empaquetador para procesar matemáticamente las listas de la carpeta `manuals/` y exportar el mapa binario purgado de duplicados:
   ```bash
   python create_bloom_filter.py
   ```

### 2. Configurar y Subir el Firmware al ESP32
1. Mueve el archivo resultante `filtro.bin` a la carpeta `data/` de tu proyecto de Arduino.
2. Abre `ESP32_DNS_Server.ino` en **Arduino IDE (v2.3.10 o superior)**.
3. Ve a *Herramientas -> Partition Scheme* y selecciona **"Default 4MB with spiffs/littlefs"** (u otra partición que asigne espacio a LittleFS).
4. Sube el código C++ a la placa.
5. Abre la paleta de comandos (`Ctrl + Shift + P`), busca **`Upload LittleFS to Pico/ESP8266/ESP32`** y ejecútalo para grabar de forma permanente el diccionario binario en la memoria Flash del chip.

### 3. Puesta en marcha Comercial
- Al encender el ESP32 por primera vez, conéctate a la red Wi-Fi abierta llamada **`Mini-PiHole-Config`** desde tu smartphone.
- El portal cautivo saltará automáticamente. Selecciona tu Wi-Fi doméstico del menú desplegable, escribe la contraseña y pulsa guardar.
- El ESP32 se reiniciará solo, cargará el filtro en RAM y te indicará su IP de servicio en el Monitor Serie (ej: `192.168.1.50`).
- Configura esa IP como el **servidor DNS primario** en tu Router o sistema operativo para proteger tu red.

---

## 🔬 Detalles de la Arquitectura de Software

### Filtro Bloom Binario (C++ / Python)
Para evitar bucles lineales o el desbordamiento de RAM, el script de Python y el firmware de C++ comparten dos funciones hash idénticas de 32 bits de alta velocidad: **DJB2** y **FNV-1a**.
```cpp
uint32_t hash_djb2(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}
```
Al mapear los residuos de ambos hashes contra el tamaño total de bits del filtro (262,144 bits), se iluminan dos posiciones en el array. La búsqueda pasa de una complejidad de $O(N)$ a un tiempo constante de $O(1)$ sin importar el volumen de dominios inyectados.

### Interceptación DNS Asíncrona
El bucle de red principal procesa las peticiones UDP salientes mediante buffers circulares, asociando identificadores de transacciones únicos y liberando el núcleo principal para evitar pérdidas de paquetes bajo tráfico pesado.

---

## ⚠️ Limitaciones Tecnológicas Conocidas

Es importante comprender el alcance arquitectónico de un sistema que opera exclusivamente en la **Capa de Red (DNS / Capa 3)** en comparación con los bloqueadores de la **Capa de Aplicación (Capa 7)**:

1. **Publicidad Integrada de Origen (YouTube / Twitch):** Este dispositivo no puede eliminar los anuncios de vídeo de plataformas como YouTube. Esto se debe a que dichos servicios transmiten su publicidad desde los mismos dominios dinámicos que sirven el contenido de vídeo real (ej: `r*.googlevideo.com`). Bloquear el dominio de la publicidad significaría romper el streaming por completo.
2. **Cifrado HTTPS / TLS:** Al operar a nivel de DNS, el ESP32 no realiza técnicas de *Man-in-the-Middle* invasivas (Inspección SSL). No puede leer rutas URL completas ni inyectar scripts en el navegador. Para un bloqueo total de anuncios integrados en tráfico web cifrado, se recomienda combinar este hardware con extensiones de aplicación como **uBlock Origin**.
3. **Evasión de Navegadores (DoH / DoT):** Ciertos navegadores modernos pueden intentar evadir el DNS local activando de forma automática *DNS-over-HTTPS*. Para garantizar la efectividad del dispositivo al 100%, es necesario desactivar el "DNS Seguro" en los ajustes del navegador del cliente.

---

## 🤝 Contacto y Colaboración

Este proyecto ha sido desarrollado bajo un enfoque de optimización extrema de recursos de hardware para entornos IoT y comerciales. Si estás interesado en la comercialización de este dispositivo, adaptaciones a carcasas 3D o integraciones industriales, no dudes en contactar conmigo:

- **Desarrollador principal:** Juan Cervantes
- **LinkedIn:** www.linkedin.com/in/juan-cervantes-simón-773257239
- **Email:** juansimon4@gmail.com
