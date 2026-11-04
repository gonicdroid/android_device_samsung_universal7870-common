# Memoria Técnica y Bitácora de Investigación: FM Radio en Samsung Galaxy J7 2016 (Exynos 7870 / BCM43438)

**Dispositivo Objetivo:** Samsung Galaxy J7 2016 (`j7xelte` / SM-J710MN)  
**SoC:** Samsung Exynos 7870 (Octa-core Cortex-A53)  
**Chip Combo Wi-Fi / Bluetooth / FM:** Broadcom BCM43438 (conectado vía UART / BCM vendor para BT y FM)  
**Códec de Audio:** Samsung / Wolfson COD3026X  
**Sistema:** LineageOS 17.1 (Android 10 - AOSP Q)  
**Fecha de consolidación de memoria:** 4 de Noviembre de 2026  

---

## 1. Resumen Ejecutivo de la Situación
1. **Bluetooth SCO Call Audio (NBS / WBS):**  
   - **ESTADO: 100% OPERATIVO Y RESUELTO.**
   - Integrado en el device tree `device/samsung/universal7870-common/libbt` (compilación nativa de `libbt-vendor`).
   - Parche dinámico para conmutación automática entre **WBS (mSBC 16 kHz)** y **NBS (CVSD 8 kHz)** aplicado mediante `device/samsung/universal7870-common/patches/0002-system-bt-dynamic-sco-wbs-nbs.patch` y administrado de forma automática por `universal7870-common/vendorsetup.sh`.
   - Propiedad `persist.bluetooth.force_nbs=true` disponible para forzar 8 kHz en pruebas con accesorios legacy o de automóviles.

2. **Radio FM:**  
   - **ESTADO: PROTOTIPADO Y ANÁLISIS DE HARDWARE COMPLETADO; AUDIO AÚN INAUDIBLE EN AURICULARES.**
   - Se completó la ingeniería inversa de los binarios stock de Android 9 de Samsung (`j7velte_p`).
   - Se mapearon con precisión todos los registros de control I2C del núcleo FM del chip Broadcom BCM43438, el protocolo de comandos específicos de fabricante (VSC `0xFC15`), y la fórmula de frecuencia.
   - Se descubrió la causa del silencio por suspensión en el códec de audio COD3026X y se activaron los relojes de hardware ALSA backend mediante flujo hostless en PCM 0,4.
   - En este punto se suspende el prototipo experimental en `system/bt` para conservar el árbol limpio y estable con los parches de Bluetooth probados, resguardando en este documento todo el conocimiento para retomar.

---

## 2. Topología y Ruteo de Hardware

### 2.1 Conexión Broadcom BCM43438 <-> COD3026X (Analógico)
A diferencia de los chips Qualcomm donde la FM se procesa digitalmente vía I2S / SLIMbus hacia el DSP, en el Samsung Galaxy J7 2016 con Exynos 7870 la salida de FM es **analógica**:
- **Pines del BCM43438:** `FM_OUT_L` y `FM_OUT_R`.
- **Pines del Códec COD3026X:** `LINEIN1_LP` y `LINEIN1_LN` (Entrada analógica estéreo de línea `LINEIN`).
- **Ruta interna del Códec COD3026X:**
  ```text
  BCM43438 (FM_OUT_L/R)
        │
        ▼ (Analógico)
  COD3026X LINEIN_PGA
        │
        ▼
  ADCL Mixer (LINELL Switch) & ADCR Mixer (LINERR Switch)
        ├──► [Ruta Digital ADC] ──► FM Link (FM On) ──► DAC ──► HPL/R Mixer ──► HPDRV ──► Auriculares
        └──► [Bypass Analógico Directo] ──────────────────────────► HPL/R Mixer ──► HPDRV ──► Auriculares
  ```

### 2.2 Suspensión del Códec (El Gran Hallazgo)
En reposo o cuando no hay una pista de música sonando en Android:
- El driver `cod3026x.c` ejecuta `cod3026x_runtime_suspend` y `cod3026x_disable`.
- El amplificador de auriculares (`HPDRV`) y la bomba de carga (`VMID`) se apagan por completo (`HP HP On = Off`).
- **Solución stock de Samsung:** Abrir un flujo PCM ALSA en el dispositivo `00-04: FM audio multicodec-4` (`pcm_open(0, 4, 8, pcm_config)` y `pcm_start`). Al iniciar este flujo dummy backend, el kernel ejecuta `universal7870_aif4_startup`, despierta el códec (`cod3026x_runtime_resume`), energiza `VMID` y `LINEIN_PGA`, y mantiene activo el amplificador de auriculares `HPDRV`.

---

## 3. Protocolo Broadcom BCM43438 FM (Ingeniería Inversa)

### 3.1 Comandos VSC (Vendor Specific Commands)
Broadcom implementa el acceso al núcleo de radio FM sobre la interfaz UART de Bluetooth utilizando comandos VSC:
- **Opcode VSC FM:** `0xFC15` (en el stack AOSP se invoca con `BTM_VendorSpecificCommand(0x0015, len, payload, NULL)`).
- **Formato del Payload de Escritura:**
  - `payload[0]`: Registro I2C FM (1 byte).
  - `payload[1]`: Bandera de operación (`0x01` para escritura, `0x00` para lectura).
  - `payload[2]`: Valor LSB (o byte de datos).
  - `payload[3]`: Valor MSB (para registros de 16 bits).
- **Comandos de Pinmux y Audio:**
  - Pinmux VSC: Opcode `0x0061` con payload `{0x05, 0xF9, 0xF8, 0xF8, 0xF8}`.
  - Audio Config VSC: Opcode `0x03DC` con payload `{0x01, 0x4A, 0x01, 0x4A, 0x00, 0x01, 0x00}`.
  - Audio Enable VSC: Opcode `0x007E` con payload `{0x01, 0x02, 0x00}`.

### 3.2 Mapa de Registros FM de Broadcom
| Registro | Nombre | Tamaño | Descripción / Valores |
|---|---|---|---|
| `0x00` | `I2C_FM_REG_RDS_SYS` | 8-bit | Encendido del core FM: `0x01` (o `0x03` con RDS). Para apagar: `0x00`. |
| `0x01` | `I2C_FM_REG_FM_CTRL` | 8-bit | Control de búsqueda y audio: escribir `0x02` o `0x03`. |
| `0x02` | `I2C_FM_REG_RDS_CTL0` | 8-bit | Control de RDS / audio: escribir `0x02`. |
| `0x05` | `I2C_FM_REG_AUD_CTL0` | 16-bit | Control de Audio y Mute:<br>• Bit 0: RF Mute (1 = silenciado por señal baja, 0 = abierto).<br>• Bit 1: Audio Mute (1 = silenciado, 0 = desilenciado).<br>• Bit 2: DAC_L Enable.<br>• Bit 3: DAC_R Enable.<br>• Bit 4: ROUTE_DAC Enable.<br>• Bit 5: ROUTE_I2S Enable.<br>• Bit 6: De-emphasis 75µs.<br>**Valor óptimo desmuteado analógico:** `0x007C` (o `0x005C`). Silenciado: `0x0002` o `0x0003`. |
| `0x09` | `I2C_FM_REG_SCH_TUNE` | 8-bit | Disparador de sintonía: escribir `0x01` para Preset Tune directo. |
| `0x0A` | `I2C_FM_REG_FM_FREQ` | 16-bit | Offset de frecuencia: `(freq_khz - 64000)`. |
| `0x0F` | `I2C_FM_REG_RSSI` | 8-bit | Lectura de nivel de señal RSSI recibido. |
| `0x4D` | `I2C_FM_REG_PCM_ROUTE` | 8-bit | Ruteo de salida:<br>• `0x00`: Ruteo Analógico DAC hacia pines `FM_OUT_L` y `FM_OUT_R`.<br>• `0x80`: Ruteo Digital I2S / PCM hacia el bus digital. |
| `0xDF` | `I2C_FM_REG_SNR` | 8-bit | Lectura de relación señal a ruido (SNR). |
| `0xF8` | `I2C_FM_REG_VOLUME_CTRL` | 16-bit | Volumen de salida: `0x00FF` (volumen máximo). |
| `0xF9` | `I2C_FM_REG_BLEND_MUTE` | 16-bit | Configuración de Soft Mute / Blend estéreo. |

### 3.3 Matemática de Sintonía (Tune Math)
Broadcom define el registro `0x0A` como la frecuencia en kHz menos 64 MHz (64,000 kHz):
$$\text{offset} = \text{freq\_khz} - 64000$$

En el código desensamblado de Broadcom (`bta_fm_tune_freq`):
```c
// Entrada: freq_10khz (para 89.10 MHz -> 8910)
uint16_t offset = (uint16_t)(1536 + (freq_10khz * 10));
// En aritmética de 16 bits: 1536 = 65536 - 64000
// Por ende: offset == freq_khz - 64000 == 89100 - 64000 == 25100 == 0x620C
```
- **89.1 MHz:** `89100 - 64000 = 25100` = `0x620C`.
- **95.5 MHz:** `95500 - 64000 = 31500` = `0x7B0C`.
- **100.0 MHz:** `100000 - 64000 = 36000` = `0x8CA0`.

---

## 4. Pruebas Realizadas y Hallazgos de Mezclador ALSA

### 4.1 Mezcladores Necesarios en `tinymix`
Para que el audio fluya desde el chip Broadcom hasta los auriculares cableados:
```bash
# 1. Habilitar el códec backend en segundo plano
tinyhostless -D 0 -P 4 -p 1024 -n 4 -c 2 -r 48000 &

# 2. Conmutar el ruteo analógico de entrada
tinymix 'FM Link' 'FM On'
tinymix 'LINEIN LINEIN On' '1'
tinymix 'ADCL Mixer LINELL Switch' '1'
tinymix 'ADCR Mixer LINERR Switch' '1'

# 3. Conmutar la salida a auriculares
tinymix 'HPL Mixer ADCL Switch' '1'
tinymix 'HPR Mixer ADCR Switch' '1'
tinymix 'HPL Mixer DACL Switch' '1'
tinymix 'HPR Mixer DACR Switch' '1'
tinymix 'HP HP On' '1'
tinymix 'Headphone Volume' '63' '63'
tinymix 'ADC Left Gain' '127'
tinymix 'ADC Right Gain' '127'
```

### 4.2 Verificación en Kernel (dmesg)
Al ejecutar el script de prueba, el kernel reportó:
```text
[ 1409.324414] cod3026x 0-a004: (*) cod3026x_runtime_resume
[ 1409.324442] cod3026x 0-a004: (*) cod3026x_enable
[ 1409.837695] cod3026x 0-a004: vmid_ev called
[ 1409.984844] cod3026x 0-a004: dac_ev called
[ 1409.985191] cod3026x 0-a004: linein_pga_ev called, event = 1
[ 1409.985221] cod3026x 0-a004: cod3026_power_on_linein called
[ 1409.986374] cod3026x 0-a004: hpdrv_ev called, event = 1
[ 1409.986400] cod3026x 0-a004: cod3026x_hp_playback_init called
[ 1410.214468] cod3026x 0-a004: cod3026x_adc_mute_work called -> un-mute ADC
```
Se comprobó que el flujo de audio de Android (`tinyplay beep.wav`) sí sonó perfectamente en los auriculares, confirmando que la etapa final (`HPDRV` / Jack 3.5mm) estaba activa y con volumen.

---

## 5. Hipótesis Abiertas para la Próxima Etapa
Dado que el códec estaba encendido y con volumen, pero en 89.1 MHz aún había silencio absoluto, las razones identificadas a investigar son:
1. **Detección / Conmutación de Antena FM:**  
   Los chips Broadcom utilizan el cable del auricular conectado como antena de FM mediante un pin multiplexado o un filtro LC en la placa. Es posible que el chip necesite un comando VSC para seleccionar la antena adecuada (Headset Wire vs PCB trace) o que el Jack 3.5mm requiera una línea GPIO de detección de auricular activa para habilitar la RF.
2. **Registro de Umbral de Mute RF (`0xF9` Blend / Soft Mute):**  
   Si el umbral de RSSI es alto por defecto, el circuito interno de Soft Mute del BCM43438 silencia automáticamente la salida si no supera el RSSI requerido.
3. **Flujo Asíncrono de Eventos VSC en BTM:**  
   En el firmware de Broadcom, cada comando de escritura I2C `0xFC15` genera un evento HCI Vendor Specific Event (VSE). El stack stock (`bta_fm_vcmd_enq`) no envía el siguiente comando hasta recibir el VSE de confirmación del anterior. En nuestro prototipo enviamos comandos secuenciales con `usleep()`, lo que podría haber provocado que alguno fuera descartado por saturación del buffer HCI.
4. **Alimentación LDO de la etapa FM:**  
   Verificar en el árbol de dispositivos (DTS) del kernel si existe un regulador de voltaje (ej. `vmic` o `vfm`) administrado por el PMIC (S2MU005) que deba ser encendido para energizar la sección analógica de FM del Broadcom BCM43438.

---

## 6. Archivos Respaldados en el Repositorio

Todos los archivos, diffs y herramientas desarrolladas han sido respaldados en:  
`device/samsung/universal7870-common/research/fm/`

- `btif_core_fm_experiment.diff`: El código C++ completo del hilo monitor y los comandos VSC implementados en `system/bt/btif/src/btif_core.cc`.
- `start_fm_audio.sh`: Script ejecutable para Android que lanza `tinyhostless`, configura los 17 controles del mezclador ALSA y sintoniza la radio.
- `set_audio_params.cpp`: Herramienta binaria de línea de comandos para enviar parámetros `AudioSystem::setParameters` directamente al HAL de audio.
- `frameworks_av_set_audio_params.diff`: Modificación a `frameworks/av/cmds/stagefright/Android.mk` para compilar la herramienta.
- `FM_RADIO_RESEARCH_AND_PROGRESS.md`: Esta memoria técnica.
