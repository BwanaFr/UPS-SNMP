Import("env")
import os
import struct
from pathlib import Path

#Extract bootloader information from bootloader.bin file
def get_bl_version(bl_path):
    with open(bl_path, "rb") as bl_in:
        if bl_in.read(1) != b'\xE9':
            print("BAD bootloader magic (esp_image_header_t)!")
            return None
        bl_in.seek(24 + 8) # Skip esp_image_header_t and esp_image_segment_header_t
        bl_struct_fmt = '<B2xBI32s24s16x'
        buffer = bl_in.read(struct.calcsize(bl_struct_fmt))
        data = struct.unpack(bl_struct_fmt, buffer)
        bl_magic = data[0]
        if bl_magic != 80:
            print("BAD bootloader magic (esp_bootloader_desc_t)!", bl_magic)
            return None

        secure_version = data[1]
        version = data[2]
        idf_version = data[3]        
    return dict(secure_version=secure_version, version=version, idf_version=idf_version)

#Build an OTA firmware file
def build_ota_file(source, target, env):
    program_path = os.path.dirname(target[0].get_abspath())
    git_ver = env['GIT_VER']
    output_file = env['PIOENV'] + "_" + git_ver + ".fw"
    bl_file = program_path + "/bootloader.bin"
    fw_file = program_path + "/firmware.bin"

    print('OTA file: ', output_file)
    bootloader_size = os.path.getsize(bl_file)
    print(f'Bootloader size : {bootloader_size}')
    bl_info = get_bl_version(bl_file)
    if bl_info is not None:
        print("Bootloader IDF version: ", bl_info['idf_version'].decode("utf-8"))
        #Remove existing .fw files
        for p in Path(program_path + "/").glob("*.fw"):
            p.unlink()

        #Write final fw file
        with open(program_path + "/" + output_file, 'wb') as fout:
            fout.write(struct.pack("8s", "UPS-FW".encode("utf-8")))     #Header
            fout.write(struct.pack("<L", bootloader_size))
            fout.write(struct.pack("32s", bl_info['idf_version']))      #IDF version of bootloader

            #Copy bootloader binary
            with open(bl_file, "rb") as bin:
                fout.write(bin.read())

            #Copy firmware binary
            with open(fw_file, "rb") as bin:
                fout.write(bin.read())
    else:
        print("Unable to get bootloader version!")

env.AddPostAction("$BUILD_DIR/firmware.bin", build_ota_file)