# Makefile for Voicetronix ctserver package
# Created David Rowe 17/10/01

version=0.3

all: targets

targets: ctserver

dist:
	rm -f ctserver-${version}.tar.gz
	rm -f ctserver-${version}
	ln -sf . ctserver-${version}
	tar -cvzf ctserver-${version}.tar.gz ctserver-${version}/*
	rm ctserver-${version}

clean:   
	 rm -f ctserver core
	 rm -f `find . -type f | grep "\~$$"`
	 rm -f CTPort/*.wav
	 rm -f CTPort/samples/*.wav
	 rm -f CTPort/tests/*.wav

install:
	mkdir -p /var/ctserver/USEngM
	cp -af UsEngM/* /var/ctserver/USEngM

uninstall:
	rm -Rf /var/ctserver

%: %.cpp 
	$(CXX) $< -o $@ -lvpb -pthread -Wall -g -lm -I/usr/include






