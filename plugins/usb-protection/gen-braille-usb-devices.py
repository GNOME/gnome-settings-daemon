#!/usr/bin/env python3
import sys
import re
import os

def parse_metainfo(xml_file, output_file):
    devices = []

    with open(xml_file, 'r') as f:
        lines = f.readlines()

    comments = []
    for line in lines:
        comment_match = re.search(r'<!--\s*(.*?)\s*-->', line)
        if comment_match:
            comment_text = comment_match.group(1)
            # Skip comments that start with BEGIN or END
            if not comment_text.startswith('BEGIN') and not comment_text.startswith('END'):
                comments.append(comment_text)
            continue

        modalias_match = re.search(r'<modalias>usb:v([0-9A-Fa-f]{4})p([0-9A-Fa-f]{4})', line)
        if modalias_match:
            vendor = int(modalias_match.group(1), 16)
            product = int(modalias_match.group(2), 16)
            combined = (vendor << 16) | product
            devices.append({
                'id': combined,
                'comments': comments.copy()
            })
            comments = []

    header_name = os.path.basename(output_file)
    header_guard_name = '__' + header_name.upper().replace('.', '_').replace('-', '_')

    array_name = os.path.splitext(header_name)[0]
    array_name = array_name.lower().replace('.', '_').replace('-', '_')

    with open(output_file, 'w') as f:
        f.write("/* Auto-generated from metainfo.xml */\n\n")
        f.write(f"#ifndef {header_guard_name}\n")
        f.write(f"#define {header_guard_name}\n\n")
        f.write("#include <glib.h>\n")
        f.write("#include <stdint.h>\n\n")
        f.write("G_BEGIN_DECLS\n\n")

        f.write(f"const uint32_t {array_name}[] = {{\n")
        for device in devices:
            # Write comments
            for comment in device['comments']:
                f.write(f"    /* {comment} */\n")
            # Write device ID
            f.write(f"    0x{device['id']:08X},\n")
            if device['comments']:
                f.write("\n")
        f.write("};\n\n")

        f.write("G_END_DECLS\n\n")
        f.write(f"#endif /* {header_guard_name} */\n")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.xml> <output.h>")
        sys.exit(1)

    parse_metainfo(sys.argv[1], sys.argv[2])
