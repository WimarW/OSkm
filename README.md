# Nama Kelompok: OSkm

# How to Run 
```
make run
```

## Debugging Process
Simply press F5 in Visual Studio Code or to attach gdb manually, run
```
make debug
```
To attach gdb to the OSkm instance, in a different terminal while in the project root directory run
```
gdb bin/kernel
(gdb) target remote localhost:1234
```
