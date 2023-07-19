BOARD_TAG = electroniccats:mbed_rp2040:bombercat
MONITOR_PORT = /dev/cu.usbmodem11101

transcode:
	python3 web_files_converter.py

compile:
	arduino-cli compile --fqbn $(BOARD_TAG) --export-binaries

upload:
	arduino-cli upload -p $(MONITOR_PORT) --fqbn $(BOARD_TAG) --verbose

monitor:
	arduino-cli monitor -p $(MONITOR_PORT)

clean:
	arduino-cli cache clean

all: transcode compile upload