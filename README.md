# lepton-rpi-webserver
Raspberry pi http and websocket server for live visualization of FLIR Lepton thermal images using a simple web interface


Dependencies :
- Requires libpoco 1.6.1 at least  (http://pocoproject.org/download/)

... and uses GCC for compilation. Compiler C++11 support is required.

Compilation (to be done on the raspberry pi) :

    cd [folder with all the files]
    chmod +x ./compile.sh
    ./compile.sh

If successful it should generate a binary called 'thermal' in the same folder.

then just launch the server on the raspberry :
`sudo ./thermal &`

It should start listening on port 80.

To access the interface, go to http://[ip-of-your-raspberry]
