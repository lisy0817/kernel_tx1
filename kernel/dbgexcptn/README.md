Refer to `<path_to_kernel_src>/Documentation/kbuild/modules.txt`.

Build with:

```
$ make -C ../kernel-4.4 M=$PWD
```

Install with:

```
$ sudo make -C ../kernel-4.4 M=$PWD modules_install
```

The module `dbgexcptn.ko` will be placed under `/lib/modules/4.4.38/extra`.

Insert the module with:

```
$ sudo insmod dbgexcptn.ko
```
