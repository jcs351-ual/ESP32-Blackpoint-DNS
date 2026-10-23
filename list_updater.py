import urllib.request
import os
import time

CARPETA_DESTINO = "manuals"

FUENTES = {
    "adaway.txt": "https://raw.githubusercontent.com/AdAway/adaway.github.io/master/hosts.txt",
    "yoyo.txt": "https://pgl.yoyo.org/adservers/serverlist.php?hostformat=hosts&showintro=0&mimetype=plaintext",
    "steven.txt": "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts"
}

def comprobar_y_actualizar():
    cabeceras = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}
    cambios_detectados = 0

    print("=== COMIENZO DE LA VERIFICACIÓN DE ACTUALIZACIONES ===")

    if not os.path.exists(CARPETA_DESTINO):
        print(f"📁 Creando carpeta contenedora: '{CARPETA_DESTINO}'")
        os.makedirs(CARPETA_DESTINO)

    for nombre_archivo, url in FUENTES.items():
        ruta_completa = os.path.join(CARPETA_DESTINO, nombre_archivo)
        necesita_descarga = True
        
        if os.path.exists(ruta_completa):
            try:
                req_head = urllib.request.Request(url, headers=cabeceras, method='HEAD')
                with urllib.request.urlopen(req_head, timeout=10) as resp:
                    size_servidor = int(resp.headers.get('Content-Length', 0))
                    size_local = os.path.getsize(ruta_completa)
                    
                    if size_servidor > 0:
                        if size_servidor == size_local:
                            print(f"🔄 {nombre_archivo}: Al día. (Coincidencia por tamaño en bytes).")
                            necesita_descarga = False
                        else:
                            print(f"⚡ {nombre_archivo}: El tamaño ha cambiado en el servidor. Actualizando...")
                    
                    else:
                        tiempo_actual = time.time()
                        tiempo_archivo = os.path.getmtime(ruta_completa)
                        horas_desde_modificacion = (tiempo_actual - tiempo_archivo) / 3600
                        
                        if horas_desde_modificacion < 12:
                            print(f"🔄 {nombre_archivo}: Al día. (Protección temporal activa: verificado hace menos de 12 horas).")
                            necesita_descarga = False
                        else:
                            print(f"⚡ {nombre_archivo}: La caché local ha expirado (>12h). Verificando nueva descarga...")

            except Exception as e:
                print(f"⚠️ No se pudo verificar el servidor para {nombre_archivo}. Conservando copia local. Error: {e}")
                necesita_descarga = False

        if necesita_descarga:
            print(f"📥 {nombre_archivo}: Descargando última versión...")
            try:
                req_get = urllib.request.Request(url, headers=cabeceras)
                with urllib.request.urlopen(req_get, timeout=20) as resp:
                    contenido = resp.read()
                    with open(ruta_completa, "wb") as f:
                        f.write(contenido)
                print(f"✅ {nombre_archivo}: ¡Guardado con éxito en '{CARPETA_DESTINO}'!")
                cambios_detectados += 1
            except Exception as e:
                print(f"❌ Error crítico al intentar descargar {nombre_archivo}: {e}")

    print("\n=== RESUMEN DEL PROCESO ===")
    if cambios_detectados > 0:
        print(f"Se han actualizado {cambios_detectados} listas. Recuerda ejecutar tu script 'generador_bloom.py'.")
    else:
        print(f"Todas las listas en '{CARPETA_DESTINO}' están perfectamente optimizadas y al día.")

if __name__ == "__main__":
    comprobar_y_actualizar()
