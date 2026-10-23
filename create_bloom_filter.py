import os

BLOOM_FILTER_SIZE = 65536  
total_bits = BLOOM_FILTER_SIZE * 8
bloom_filter = bytearray(BLOOM_FILTER_SIZE)

CARPETA_ORIGEN = "manuals"
LISTAS = ["adaway.txt", "yoyo.txt", "steven.txt"]

def hash_djb2(dominio):
    hash_val = 5381
    for char in dominio:
        hash_val = ((hash_val << 5) + hash_val) + ord(char)
        hash_val &= 0xFFFFFFFF  
    return hash_val

def hash_fnv1a(dominio):
    hash_val = 2166136261
    for char in dominio:
        hash_val ^= ord(char)
        hash_val = (hash_val * 16777619) & 0xFFFFFFFF
    return hash_val

def agregar_a_filtro(dominio):
    h1 = hash_djb2(dominio)
    h2 = hash_fnv1a(dominio)

    bit1 = h1 % total_bits
    bit2 = h2 % total_bits

    bloom_filter[bit1 // 8] |= (1 << (bit1 % 8))
    bloom_filter[bit2 // 8] |= (1 << (bit2 % 8))

dominios_unicos = set()

print(f"=== INICIANDO PARSEO DESDE CARPETA '{CARPETA_ORIGEN}' ===")

for nombre_archivo in LISTAS:
    ruta_completa = os.path.join(CARPETA_ORIGEN, nombre_archivo)
    
    if not os.path.exists(ruta_completa):
        print(f"ℹ️ Archivo '{nombre_archivo}' no encontrado en '{CARPETA_ORIGEN}'. Saltando...")
        continue
        
    print(f"Procesando: {ruta_completa}...")
    try:
        with open(ruta_completa, "r", encoding="utf-8", errors="ignore") as f:
            for linea in f:
                linea_limpia = linea.strip()
                if not linea_limpia or linea_limpia.startswith("#"):
                    continue
                
                partes = linea_limpia.split()
                if len(partes) >= 2:
                    ip_origen = partes[0]
                    dominio = partes[1].lower().strip()
                    
                    if ip_origen in ["0.0.0.0", "127.0.0.1"]:
                        if "#" in dominio:
                            dominio = dominio.split("#")[0].strip()
                        if dominio and dominio != "localhost":
                            dominios_unicos.add(dominio)
    except Exception as e:
        print(f"❌ Error al leer {nombre_archivo}: {e}")

lista_final = list(dominios_unicos)
print(f"\n[Fase de Análisis] Total de dominios únicos consolidados: {len(lista_final)}")

# Límite matemático para preservar la precisión del Filtro Bloom en 64KB
limite_seguro = int(total_bits / 10) # ~52,428 dominios máximos
if len(lista_final) > limite_seguro:
    print(f"⚠️ La lista supera el límite óptimo para 64KB de RAM.")
    print(f"Reduciendo inteligentemente a los primeros {limite_seguro} dominios para evitar falsos positivos.")
    lista_final = lista_final[:limite_seguro]

print(f"Indexando matemáticamente {len(lista_final)} dominios en tu mapa binario...")
for dom in lista_final:
    agregar_a_filtro(dom)

# Exportar el binario final
with open("filtro.bin", "wb") as f:
    f.write(bloom_filter)

print("\n=== ¡ÉXITO TOTAL! ===")
print(f"Archivo 'filtro.bin' ({BLOOM_FILTER_SIZE} bytes) generado localmente listo para LittleFS.")
