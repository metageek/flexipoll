.PHONY:: all clean test

all clean::
	cd src; make $@

clean::
	cd tests; make $@

test:: all
	cd tests; make test
