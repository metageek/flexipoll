.PHONY:: all clean test

all:: test

clean::
	$(RM) *.o *.a *~

INCDIR := ../include
CFLAGS += -I$(INCDIR) -g
LIBS := ../src/libflexipoll.a

tst.o pipetest.o: $(INCDIR)/flexipoll.h

test:: tst pipetest

tst: tst.o $(LIBS)
	$(CC) tst.o $(LIBS) -o $@

pipetest: pipetest.o $(LIBS)
	$(CC) pipetest.o $(LIBS) -o $@
