#!/usr/bin/env python
########################################################
#
#  Stack Trace Decoder
#  Author: Slavey Karadzhov <slav@attachix.com>
#
########################################################
import shlex
import subprocess
import sys
import re
import os

def usage():
    print("Usage: \n\t%s <file.elf> [<error-stack.log>]" % sys.argv[0])

def extractAddresses(data):
    # Matches 40xxxxxx, 41xxxxxx, 42xxxxxx
    m = re.findall("(4[0-2][0-9a-f]{6})", data.lower())
    if len(m) == 0:
        return m

    addresses = []
    for item in m:
        addresses.append(item)

    return addresses

if __name__ == "__main__":
    if len(sys.argv)  not in list(range(2,4)):
        usage()
        sys.exit(1)

    soc = os.environ.get('SMING_SOC', 'esp32').lower()
    
    if soc in ['esp32c3', 'esp32c2', 'esp32c6', 'esp32h2']:
        tool = 'riscv32-esp-elf-addr2line'
    elif soc == 'esp32s2':
        tool = 'xtensa-esp32s2-elf-addr2line'
    elif soc == 'esp32s3':
        tool = 'xtensa-esp32s3-elf-addr2line'
    else:
        tool = 'xtensa-esp32-elf-addr2line'

    command = "%s -aipfC -e '%s' " % (tool, sys.argv[1])
    pipe = subprocess.Popen(shlex.split(command), bufsize=1, stdin=subprocess.PIPE)

    if len(sys.argv) > 2:
        data = open(sys.argv[2]).read()
        pipe.communicate("\n".join(extractAddresses(data)).encode('ascii'))
    else:
        while True:
            data = sys.stdin.readline()
            addresses = extractAddresses(data)
            if len(addresses) == 0:
                continue

#             print ( "[",addresses,"]" )

            line = "\r\n".join(addresses)+"\r\n"
#             line = line.ljust(125," ")

            pipe.stdin.write(line.encode('ascii'))
            pipe.stdin.flush()
