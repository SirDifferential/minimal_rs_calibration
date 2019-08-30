CC=g++
CXXFLAGS=-std=c++11 -g -I/home/gekko/librealsense/include -I/home/gekko/librscalibrationapi/usr/include
LDFLAGS=-L/home/gekko/librealsense/build -lrealsense2 -L/home/gekko/librscalibrationapi/usr/lib -lDSDynamicCalibrationAPI
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=minimal_realsense_calibration

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CXXFLAGS) -o $(EXECUTABLE) $(OBJECTS) $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CXXFLAGS) $(LDFLAGS) -c -o $@ $<

clean:
	rm -f *.o
	rm -f ${EXECUTABLE}

