all:
	gcc -Wall -Werror minimal_vm.c -o minimal-vm

clean:
	rm minimal-vm