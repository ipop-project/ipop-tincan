# Makefile for Linux TAP device.
CC = g++
ODIR=../../out/debug/obj
IDIR_I = ../../include/
IDIR_L = ../../include/linux
IDIR_E = ../../../external/include
CFLAGS = -std=c++14 -Wall -fno-rtti -I$(IDIR_I) -I$(IDIR_E) -I$(IDIR_L) -D LINUX -D WEBRTC_POSIX -D _IPOP_LINUX
SRCDIR = .
_OBJ=tapdev_lnx.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))
$(info OBJ is $(OBJ))
all:$(OBJ)
$(ODIR)/%.o:$(SRCDIR)/%.cc
	$(info OBJ is $(OBJ))
	if [ ! -d $(ODIR) ]; then  mkdir $(ODIR); fi
	$(CC) -c $< -o $@ $(CFLAGS)
