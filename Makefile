CC=gcc
CXX=g++
RM= /bin/rm -vf
ARCH=UNDEFINED
PWD=$(shell pwd)
CDR=$(shell pwd)
ECHO=echo

EDCFLAGS:=$(CFLAGS) -I include/ -I alliedcam/include -I rtd_adio/include -Wall -std=gnu11
EDLDFLAGS:=$(LDFLAGS) -lpthread -lm -L alliedcam/lib -lVmbC -L rtd_adio/lib -lrtd-aDIO
EDDEBUG:=$(DEBUG)

ifeq ($(ARCH),UNDEFINED)
	ARCH=$(shell uname -m)
endif

UNAME_S := $(shell uname -s)

EDCFLAGS+= -I include/ -I ./ -Wall -O2 -std=gnu11 -I imgui/libs/gl3w -DIMGUI_IMPL_OPENGL_LOADER_GL3W
CXXFLAGS:= -I alliedcam/include -I rtd_adio/include -I include/ -I imgui/include -Wall -O2 -fpermissive -std=gnu++11 -I imgui/libs/gl3w -DIMGUI_IMPL_OPENGL_LOADER_GL3W $(CXXFLAGS)
LIBS = -lpthread

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += -lGL `pkg-config --static --libs glfw3`
	CXXFLAGS += `pkg-config --cflags glfw3`
endif

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	CXXFLAGS:= -arch $(ARCH) $(CXXFLAGS) `pkg-config --cflags glfw3`
	LIBS += -arch $(ARCH) -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo `pkg-config --libs glfw3`
	LIBS += -L/usr/local/lib -L/opt/local/lib
	LIBS += `pkg-config --static --libs glfw3`

	CXXFLAGS += -I/usr/local/include -I/opt/local/include `pkg-config --cflags glfw3`
	CFLAGS = $(CXXFLAGS)
endif

LIBS += -L alliedcam/lib -lVmbC -L rtd_adio/lib -lrtd-aDIO

all: CFLAGS+= -O2

GUITARGET=imagegen.out

all: clean $(GUITARGET)
	$(ECHO) "Built for $(UNAME_S), execute ./$(GUITARGET)"

$(GUITARGET): imgui/libimgui_glfw.a alliedcam/liballiedcam.a rtd_adio/lib/librtd-aDIO.a
	$(CXX) -o $@ guimain.cpp stringhasher.cpp $(CXXFLAGS) imgui/libimgui_glfw.a alliedcam/liballiedcam.a $(LIBS)
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):alliedcam/lib ./$(GUITARGET)

imgui/libimgui_glfw.a:
	cd $(PWD)/imgui && make -j$(nproc) && cd $(PWD)

alliedcam/liballiedcam.a:
	cd $(PWD)/alliedcam && make liballiedcam.a && cd $(PWD)

rtd_adio/lib/librtd-aDIO.a:
	cd $(PWD)/rtd_adio/lib && make && cd $(PWD)

%.o: %.c
	$(CC) $(EDCFLAGS) -o $@ -c $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	$(RM) $(GUITARGET)

spotless: clean
	cd $(PWD)/imgui && make spotless && cd $(PWD)
