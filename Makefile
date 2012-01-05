
.PHONY: all

all: dshm


dshm: main.c
	gcc -g main.c
