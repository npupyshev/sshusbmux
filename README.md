# sshusbmux
When launched without any arguments provides a SSH shell connected to a jailbroken iOS device over USB. In tunneling mode 
utility starts a TCP server at localhost:localport and relays data from localport to a port on device (by default from 31765 to 22).

# usage
sshusbmux [Options]
- -p <port>  Set local port. Default 31765.
- -d <port>  Set remote port. Default 22.
- -t         Tunneling mode.
- --die      If device is disconnected, sshusbmux will exit.
- -h         display help message.

# build
make; sudo make install