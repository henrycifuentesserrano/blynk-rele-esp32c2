# Alarma IoT — ESP32-C2 con AWS IoT Core

Firmware para control remoto de relé desde cualquier lugar via AWS IoT Core y MQTT sobre TLS. Desarrollado sobre ESP-IDF 5.5 sin Arduino, parte del ecosistema **YUMA — Intelligent Systems Flow**.

---

## Hardware usado

- ESPC2-12 DevKit CozyLife (ESP32-C2, cristal 26MHz, flash 2MB)
- Relé 5V DC / 10A 120-220VAC (5 pines)
- Optoacoplador PC817
- Transistor 2N2222
- Diodo 1N4007
- 2x LED (GPIO10 = estado relé, GPIO18 = estado WiFi/AWS)
- 1x Resistencia 200Ω
- 3x Resistencia 1kΩ
- Botón BOOT IO9 (reset de provisioning)

---

## Esquema de conexión

### ESP32-C2 → PC817 (aislamiento)
```
GPIO5 → R200Ω → Pin 1 PC817 (ánodo)
GND   →         Pin 2 PC817 (cátodo)
```

### PC817 → 2N2222 (amplificación)
```
Pin 3 PC817 (emisor)   → GND
Pin 4 PC817 (colector) → R1kΩ → 5V  (pull-up)
Pin 4 PC817 (colector) → Base 2N2222 (mismo punto)
```

### 2N2222 → Relé
```
Emisor 2N2222   → GND
Colector 2N2222 → Pin IN del relé
```

### Relé
```
VCC → 5V
GND → GND
IN  → Colector 2N2222
COM → cable común de la carga
NO  → otro cable de la carga
```

### Diodo 1N4007 (protección bobina)
```
Cátodo (banda) → VCC relé (5V)
Ánodo          → GND relé
```

### LEDs
```
GPIO10 → R1kΩ → LED estado relé    → GND
GPIO18 → R1kΩ → LED conexión WiFi  → GND
```

### GND común
```
GND ESP32 + Pin2 PC817 + Pin3 PC817 + Emisor 2N2222 + GND relé
```

---

## Configuración del entorno

### Requisitos
- Windows 10/11
- ESP-IDF 5.5.3 instalado
- Cuenta AWS con IoT Core configurado
- Certificados AWS IoT (ver sección Certificados)

### Instalación ESP-IDF
Descarga el instalador oficial desde:
https://dl.espressif.com/dl/esp-idf/

Durante la instalación se crea acceso directo a **ESP-IDF 5.5 PowerShell** en el menú inicio. Usar siempre ese entorno para compilar y flashear.

---

## Configuración del proyecto

### 1. Clonar el repositorio
```bash
git clone https://github.com/henrycifuentesserrano/alarma-iot-esp32c2.git
cd alarma-iot-esp32c2
```

### 2. Agregar certificados AWS IoT
Crea la carpeta `certs/` en la raíz del proyecto y agrega los tres archivos:
```
certs/
├── certificate.pem.crt   ← certificado del dispositivo
├── private.pem.key       ← clave privada
└── root_ca.pem           ← certificado raíz de Amazon
```
Estos archivos se generan en la consola de AWS IoT Core al crear un Thing.

### 3. Configurar el target
El ESPC2-12 de CozyLife usa cristal de 26MHz — crítico para que el WiFi funcione.
```bash
idf.py set-target esp32c2
idf.py menuconfig
```

Dentro del menuconfig:
- **Component config → Hardware Settings → Main XTAL frequency → 26 MHz**
- **Component config → Wi-Fi → WiFi SoftAP Support → Activar**

Guardar con `S`, salir con `Q`.

### 4. Configurar endpoint de AWS IoT
En `main/alarma_main.c` actualiza:
```c
#define AWS_ENDPOINT   "tu-endpoint.iot.us-east-1.amazonaws.com"
#define AWS_THING_NAME "nombre-de-tu-thing"
#define TOPIC_CONTROL  "tu/topic/control"
#define TOPIC_STATUS   "tu/topic/status"
```

### 5. Compilar y flashear
```bash
idf.py build flash monitor -p COM5 --monitor-baud 74880
```
El `74880` es la velocidad correcta para el cristal de 26MHz del ESPC2-12.

---

## WiFi Provisioning

El dispositivo incluye un portal de configuración web. Al arrancar sin credenciales WiFi guardadas:

1. El ESP32 crea una red WiFi llamada **"Alarma-Config"** (contraseña: `alarma123`)
2. Conéctate desde tu celular a esa red
3. Se abre automáticamente el portal de configuración YUMA
4. Ingresa: red WiFi, contraseña, latitud y longitud del dispositivo
5. El ESP32 guarda todo y se reinicia conectado a tu red

### Reset de provisioning
Mantén presionado el botón **IO9** por 5 segundos — el dispositivo borra las credenciales y vuelve al modo de configuración.

---

## Máquina de estados

| Estado | Descripción | LED GPIO18 | LED GPIO10 | Relé |
|--------|-------------|-----------|-----------|------|
| INICIANDO | Arrancando, buscando WiFi | Parpadea 500ms | OFF | OFF |
| CONECTANDO_WIFI | Conectando a red WiFi | Parpadea 500ms | OFF | OFF |
| CONECTANDO_AWS | WiFi OK, conectando a AWS | Parpadea 250ms | OFF | OFF |
| OPERANDO | Todo conectado, listo | ON fijo | según relé | según app |
| RECONECTANDO | WiFi o AWS caído | Parpadea 1000ms | OFF | OFF |
| PROVISIONING | Modo configuración AP | Parpadea rápido doble | OFF | OFF |

---

## Flujo de datos
```
App YUMA Connect → AWS IoT Core → ESP32-C2 → PC817 → 2N2222 → Relé
```

### Topics MQTT
| Topic | Dirección | Payload |
|-------|-----------|---------|
| `finca/rele/control` | App → ESP32 | `{"rele":1}` o `{"rele":0}` |
| `finca/rele/status` | ESP32 → App | `{"rele":0,"lat":"x","lon":"y"}` |
| `finca/rele/conexion` | ESP32 → App | `{"c":1}` conectado, `{"c":0}` desconectado (LWT) |

---

## Notas importantes

- La red WiFi debe ser **2.4GHz** — el ESP32-C2 no soporta 5GHz
- El cristal de **26MHz** es específico del ESPC2-12 CozyLife — otros módulos pueden usar 40MHz
- El relé siempre **inicia en OFF** al encender
- Los certificados AWS van en `certs/` que está en `.gitignore` — nunca se suben al repositorio
- El **LWT** con keepalive de 30 segundos permite detectar desconexión en ~45 segundos
- El **PC817** aísla eléctricamente el ESP32 del circuito del relé

---

## Estructura del proyecto
```
alarma-iot-esp32c2/
├── main/
│   ├── alarma_main.c         # código principal
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild     # menú de configuración
│   └── idf_component.yml
├── certs/                    # certificados AWS (en .gitignore)
│   ├── certificate.pem.crt
│   ├── private.pem.key
│   └── root_ca.pem
├── CMakeLists.txt
├── README.md
├── sdkconfig.defaults
└── .gitignore
```

---

## App compatible

La aplicación móvil YUMA Connect está disponible en:
[yuma-connect](https://github.com/henrycifuentesserrano/yuma-connect)

---

## Autor

Henry Cifuentes Serrano
[github.com/henrycifuentesserrano](https://github.com/henrycifuentesserrano)