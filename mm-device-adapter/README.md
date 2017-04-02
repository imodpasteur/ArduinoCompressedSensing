Device Adapter for Micro-Manager
--------------------------------

## Where the hell am I?

A device adapter can be seen as a driver. This folder contains :

- Code to adapt the Arduino device adapter to a CS-ready device adapter
- A Makefile to compile the guy standalone (note that paths are hardcoded)
- This README

## How it works
It allows to develop the plugin without recompiling the whole micromanager, and thus save a lot of time. It has to be softly linked *in lieu* of the `Arduino` source file in the micromanager source tree.

Once this is done, from the source file, one has to call the commands:

```{shell}
make clean
make -j4
make -j4 install
```

This will compile the library and copy it at the location specified in the Makefile (which should be the path of the compiled version of micromanager).

## Disclaimer
All this process is a little bit ugly. I don't know how to do a better job... Sorry.

## Who should I blame for this sh...?
Just blame Maxime.
