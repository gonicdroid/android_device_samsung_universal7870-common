#!/usr/bin/env python3
"""
patch_ril.py - Script para parchear automáticamente libsec-ril.so y libsec-ril-dsds.so
para permitir que LineageOS Audio HAL y com.sec.imsservice se conecten a OemClientReceiver.
"""
import struct
import sys
import os

def patch_ril(input_file, output_file):
    if not os.path.exists(input_file):
        print(f"Error: {input_file} no existe.")
        return False

    with open(input_file, 'rb') as f:
        b = bytearray(f.read())

    print(f"[*] Analizando {input_file} ({len(b)} bytes)...")

    # 1. Parche de ruta de Audio HAL
    audio_str = b'/vendor/bin/hw/android.hardware.audio@'
    idx = b.find(audio_str)
    if idx == -1:
        print(f"Error: No se encontró audio string en {input_file}")
        return False
    at_offset = idx + len('/vendor/bin/hw/android.hardware.audio')
    b[at_offset] = 0  # Reemplazar '@' por '\0'
    print(f"[+] Audio HAL path parcheado en offset: {hex(at_offset)}")

    # 2. Inyectar com.sec.imsservice en rodata libre (0xfa310)
    rodata_offset = 0xfa310
    imssvc_bytes = b'com.sec.imsservice\x00'
    b[rodata_offset:rodata_offset + len(imssvc_bytes)] = imssvc_bytes
    print(f"[+] String 'com.sec.imsservice' inyectada en: {hex(rodata_offset)}")

    # 3. Localizar tabla allowedProcess
    audio_ptr_bytes = struct.pack('<I', idx)
    table_entry0_ptr = b.find(audio_ptr_bytes)
    if table_entry0_ptr == -1:
        print(f"Error: No se encontró puntero a la cadena de audio.")
        return False

    table_entry0 = table_entry0_ptr - 8 # offset del UID de la entrada 0
    print(f"[+] Tabla allowedProcess localizada en: {hex(table_entry0)}")

    # Parchear UID de entrada 0 (Audio HAL) a -1 (0xFFFFFFFF)
    b[table_entry0:table_entry0 + 4] = struct.pack('<I', 0xffffffff)
    print(f"[+] allowedProcess[0].uid establecido a 0xFFFFFFFF (-1)")

    # Parchear entrada 5 (IMS Service)
    entry5 = table_entry0 + 5 * 12
    entry5_ptr_offset = entry5 + 8
    b[entry5_ptr_offset:entry5_ptr_offset + 4] = struct.pack('<I', rodata_offset)
    print(f"[+] allowedProcess[5].proc_path repuntado a com.sec.imsservice en: {hex(entry5_ptr_offset)}")

    with open(output_file, 'wb') as f:
        f.write(b)
    print(f"[✓] Archivo parcheado guardado exitosamente en: {output_file}\n")
    return True

if __name__ == '__main__':
    if len(sys.argv) == 3:
        patch_ril(sys.argv[1], sys.argv[2])
    else:
        # Por defecto parchear ambos si se encuentran en el directorio actual
        if os.path.exists('libsec-ril.so'):
            patch_ril('libsec-ril.so', 'libsec-ril_patched.so')
        if os.path.exists('libsec-ril-dsds.so'):
            patch_ril('libsec-ril-dsds.so', 'libsec-ril-dsds_patched.so')
        if not os.path.exists('libsec-ril.so') and not os.path.exists('libsec-ril-dsds.so'):
            print("Uso: python3 patch_ril.py <input.so> <output.so>")

