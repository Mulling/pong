# Basic ping implementation

We don't required sudo to flood.

## Using:

To compile you will need a working C compiler and make (sudo is required to give permissions to the program, since only root can create RAW sockets):

```bash
$ sudo make release
```

Or:

```bash
$ sudo ./pong
```

Usage is defined as below:

```
USING:
    pong [target] [[-f] [-t time_to_live]]

OPTIONS:
    target
        Host name/destination.

    -f
        Flood ping. Send ECHO_REQUEST and wait for ECHO_REPLY. Without waiting for SO_RCVTIMEO, SO_SNDTIMEO and RCVTIMEO.

    -t
        Set the time to live, defaults to 64.

    SIGINT
        Ends the ping and prints results.
```

## TODO:
- [ ] Propper packet filter.
- [ ] Hog the network.
