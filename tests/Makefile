.PHONY:: all clean test

all:: test

clean::
	$(RM) *.o *.a *~

INCDIR := ../include
CFLAGS += -I$(INCDIR) -g
LIBS := ../src/libflexipoll.a

OBJS := tst.o
tst.o: $(INCDIR)/flexipoll.h

test:: tst

tst: $(OBJS) $(LIBS)
	$(CC) $(OBJS) $(LIBS) -o $@
