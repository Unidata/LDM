CC = g++
INCLUDE = ../../../protocol/
LIB =
ELFFILE = UdpComm

$(ELFFILE): UdpCommTest.cpp UdpComm.cpp UdpComm.h
	#$(CC) -o $(ELFFILE) UdpCommTest.cpp UdpComm.cpp -I$(INCLUDE) -l$(LIB)
	$(CC) -o $(ELFFILE) UdpCommTest.cpp UdpComm.cpp -I$(INCLUDE)

.PHONY : clean
clean:
	rm $(ELFFILE)
