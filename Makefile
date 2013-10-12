CFLAGS+=-O0 -g3 $(shell pkg-config --cflags libdrm gbm gl egl)
CPPFLAGS+=-Wall
LDFLAGS+=
LDLIBS+=$(shell pkg-config --libs libdrm gbm gl egl) -lm

PROGS:=plane

all: $(PROGS)

plane: plane.o utils.o gutils.o term.o gl.o

clean:
	rm -f $(PROGS) *.o
