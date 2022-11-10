.PHONY: all

all: mypkg mychroot

mypkg: mypkg.c
	gcc -g $< -o $@

mychroot: mychroot.c
	gcc -g $< -o $@
