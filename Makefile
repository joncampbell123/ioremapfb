obj-m += ioremap2fb.o

ifndef $(KDIR)
KDIR_RAW=$(shell uname -r | sed 's/\-.*$$//')
KDIR=/usr/src/linux-$(KDIR_RAW)
#KDIR=/usr/src/linux-2.6.28
#KDIR=/usr/src/linux-2.6.28.8
#KDIR=/mnt/sda1/ext2/usr/src/2.6.28.10
endif

all: ioremap2fb.ko

ioremap2fb.ko: ioremap2fb.c
	make -C $(KDIR) M=$(PWD) modules

install:
	make -C $(KDIR) M=$(PWD) modules_install

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f modules.order test_info *~

load:
	rmmod ioremap2fb || true
	insmod ioremap2fb.ko

unload:
	rmmod ioremap2fb || true

dev:

