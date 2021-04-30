# FIXME use cmake

# NOTE: This one Makefile works with Microsoft nmake and GNU make.
# They use different conditional syntax, but each can be nested and inverted within the other.

all: check

ifdef MAKEDIR:
!ifdef MAKEDIR

#
# Microsoft nmake on Windows with desktop CLR, Visual C++.
#

RM_F = del 2>nul /f
#ILDASM = ildasm /nobar /out:$@
#ILASM = ilasm /quiet
#RUN_EACH=for %%a in (
#RUN_EACH_END=) do @$Q$(MONO)$Q %%a

!else
else

#
# GNU/Posix make on Unix with mono, g++, clang, etc.
#
RM_F = rm -f

#ILDASM = ikdasm >$@
#ILASM = ilasm
#MONO ?= mono
#RUN_EACH=for a in
#RUN_EACH_END=;do $(MONO) $${a}; done

endif
!endif :

check:

clean:

exe:

ifdef MAKEDIR:
!ifdef MAKEDIR

!if !exist (./config.mk)
!if [.\config.cmd]
!endif
!endif
!if exist (./config.mk)
!include ./config.mk
!endif

#!message AMD64=$(AMD64)
#!message 386=$(386)

!if !defined (AMD64) && !defined (386) && !defined (ARM)
AMD64=1
386=0
ARM=0
!endif

!if $(AMD64)
win=winamd64.exe
386=0
ARM=0
!elseif $(386)
win=winx86.exe
AMD64=0
ARM=0
!elseif $(ARM)
win=winarm.exe
AMD64=0
386=0
!endif

!ifndef win
win=win.exe
!endif

all: $(win)

config:
	.\config.cmd

check:

run: $(win)
	.\$(win) ./1.rs

debug: $(win)
!if $(AMD64)
	\bin\amd64\windbg .\$(win) ./1.rs
!elseif $(386)
	\bin\x86\windbg .\$(win) ./1.rs
!endif

clean:
	$(RM_F) $(win) r1.obj *.ilk win32 win32.exe win64 win64.exe win win.exe winarm.exe winx86.exe winamd64.exe *.pdb lin *.i winamd64.exe.manifest

# TODO clang cross
#
#mac: r1.cpp
#	g++ -g r1.cpp -o $@
#

# TODO /Qspectre

$(win): r1.cpp
	@-del $(@R).pdb $(@R).ilk
	@rem TODO /GX on old, /EHsc on new
	rem cl /Gy /O2s $(Wall) $(Qspectre) /W4 /MD /Zi /GX $** /link /out:$@ /incremental:no /opt:ref,icf
	cl /Gy $(Wall) $(Qspectre) /W4 /MD /Zi /GX $** /link /out:$@ /incremental:no /opt:ref /pdb:$(@B).pdb

!else
else

UNAME_S = $(shell uname -s)

ifeq ($(UNAME_S), Cygwin)
Cygwin=1
NativeTarget=cyg
else
Cygwin=0
Linux=0
endif

ifeq ($(UNAME_S), Linux)
Linux=1
NativeTarget=lin
else
Cygwin=0
endif

# TODO Darwin, Linux, etc.

# FIXME winarm64 etc.
all: $(NativeTarget) win32.exe win64.exe

run: $(NativeTarget)
	./$(NativeTarget) ./1.rs

debug: mac
	lldb -- ./$(NativeTarget) ./1.rs

clean:
	$(RM_F) mac win32 win32.exe win64 win64.exe win win.exe cyg cyg.exe *.ilk lin win.exe winarm.exe winx86.exe winamd64.exe winamd64.exe.manifest

mac: r1.cpp
	g++ -g r1.cpp -o $@ -Bsymbolic -bind_at_load

cyg: r1.cpp
	g++ -g r1.cpp -o $@ -Bsymbolic -znow -zrelro

lin: r1.cpp
	g++ -Wall -g r1.cpp -o $@ -Bsymbolic -znow -zrelro

win32.exe: r1.cpp
	i686-w64-mingw32-g++ -g r1.cpp -o $@ -Bsymbolic

win64.exe: r1.cpp
	x86_64-w64-mingw32-g++ -g r1.cpp -o $@ -Bsymbolic

test:

endif
!endif :
