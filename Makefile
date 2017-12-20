obj-m := skeleton.o
skeleton-objs:=skel-core.o skel-video.o 

KERNEL_SRC:=/lib/modules/$(shell uname -r)/build 
SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)  -lgcc


modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
