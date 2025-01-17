#!/usr/bin/env python3
#
# /*
#  * Copyright (c) 2021, Krisna Pranav
#  *
#  * SPDX-License-Identifier: BSD-2-Clause
# */

# Launch the script from root of the project to have the correct paths

import subprocess
import os
import sys

connections = [
    ["servers/window_server/shared/Connections/ws_connection.ipc",
        "servers/window_server/shared/Connections/WSConnection.h"],
]

for conn in connections:
    inf = conn[0]
    outf = conn[1]
    print("Compiling {0} -> {1}", inf, outf)
    cmd = ["utils/compilers/ConnectionCompiler/connc"]
    cmd.extend([inf, outf])
    result = subprocess.run(cmd, stdout=subprocess.PIPE)
    print(result.stdout.decode("ascii"))
    print()
