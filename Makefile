obj-m += ioremap2fb.o

ifndef $(KDIR)
KDIR_RAW=$(shell uname -r | sed 's/\-.*$$//')
 ifneq ($(KDIR_RAW),)
  KDIR=/usr/src/linux-$(KDIR_RAW)
 endif
endif

ifneq ($(KDIR),)
 KDIR=/usr/src/linux
endif

all: config.h ioremap2fb.ko

ioremap2fb.ko: ioremap2fb.c
	make -C $(KDIR) M=$(PWD) modules

install:
	make -C $(KDIR) M=$(PWD) modules_install

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f modules.order test_info *~ config.h

config.h:
	./gen-version

load:
	rmmod ioremap2fb || true
	insmod ioremap2fb.ko

unload:
	rmmod ioremap2fb || true

dev:

