obj-m += usbd.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

test: usbd_test
	gcc usbd_test.c -o usbd_test

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	-rm usbd_test

