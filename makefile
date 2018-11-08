CFLAGS=-Wall -g
O_FILES1= nim-server.o transport.o
O_FILES2= nim.o transport.o

all -B: nim-server nim 

clean:
	-rm nim-server $(O_FILES1)
	-rm nim nim.o

nim-server: $(O_FILES1)
	gcc  $(CFLAGS) -o $@ $^

nim: $(O_FILES2)
	gcc  $(CFLAGS) -o $@ $^

nim-server.o: nim-server.c transport.c transport.h
	gcc -c $(CFLAGS) $*.c

nim.o: nim.c transport.c transport.h
	gcc -c $(CFLAGS) $*.c

transport.o: transport.c transport.h
	gcc -c $(CFLAGS) $*.c

