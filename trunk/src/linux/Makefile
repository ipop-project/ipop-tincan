# all: library.cpp main.cpp
# $@ evaluates to all, $< evaluates to library.cpp, $^ evaluates to library.cpp main.cpp
BINARY = ipop-tincan
CC = g++
IDIR_I = ../include
IDIR_E = ../../external/include
CFLAGS = -std=c++14 -Wall -fno-rtti -I$(IDIR_I) -I$(IDIR_E) -D LINUX -D WEBRTC_POSIX -D _IPOP_LINUX
SRCDIR = ../src
LIN_SRC = ../src/linux
LDIR = -L ../../external/lib/debug_x64/linux
LIBS = -lrtc_p2p -lrtc_base -ljsoncpp -lrtc_base_approved -lrtc_xmpp -lrtc_xmllite -lpthread \
-lfield_trial_default -lexpat -lboringssl -lrt -lboringssl_asm
ODIR = ../out/debug/obj
BDIR = ../out/debug
TARGET = $(patsubst %,$(BDIR)/%,$(BINARY))
_OBJ = control_dispatch.o control_listener.o peer_network.o tincan.o tincan_control.o\
tincan_main.o virtual_link.o virtual_network.o tap_frame.o
LIN_OBJ_=tapdev_lnx.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))
LIN_OBJ = $(patsubst %,$(ODIR)/%,$(LIN_OBJ_))


$(ODIR)/%.o: $(SRCDIR)/%.cc
	if [ ! -d $(ODIR) ]; then  mkdir -p $(ODIR); fi
	$(CC) -c -o $@ $< $(CFLAGS)


$(TARGET): $(OBJ) $(LIN_OBJ)
	if [ ! -d $(BDIR) ]; then  mkdir -p $(BDIR); fi
	$(CC) -o $@ $^ $(LDIR) $(LIBS)


$(LIN_OBJ):force_look
	$(info will run Makefile for linux specific objects)
	cd $(LIN_SRC); $(MAKE) $(MAKEFLAGS)


force_look:
	true
clean:
	rm -rf $(TARGET) $(ODIR) *~


