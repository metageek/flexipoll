.PHONY:: all clean

LIB := libflexipoll.a

all:: $(LIB)

clean::
	$(RM) *.o *.a *~

INCDIR := ../include
CFLAGS := -I$(INCDIR)

OBJS := flexipoll.o

$(LIB): $(OBJS)
	$(RM) $(LIB)
	$(AR) -cr $(LIB) $(OBJS)

flexipoll.o: $(INCDIR)/flexipoll.h
